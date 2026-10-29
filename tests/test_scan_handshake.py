"""Compile the actual UART driver against a deterministic GPIO/HAL model."""
from pathlib import Path
import subprocess
import tempfile
FW = Path(__file__).resolve().parents[1]
FAKE = r'''
#ifndef FAKE_MAIN_H
#define FAKE_MAIN_H
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define USART3 ((void *)3)
#define CTS_GPIO_Port ((void *)1)
#define RTS_GPIO_Port ((void *)2)
#define AIMWAKE_GPIO_Port ((void *)4)
#define CTS_Pin 10
#define RTS_Pin 9
#define AIMWAKE_Pin 13
#define APP_WDG_CLIENT_SCAN 1
#define FLAG_SCAN_UART_RX 1
#define osOK 0
typedef enum {HAL_OK,HAL_ERROR,HAL_BUSY,HAL_TIMEOUT} HAL_StatusTypeDef;
typedef enum {GPIO_PIN_RESET,GPIO_PIN_SET} GPIO_PinState;
typedef struct {void *Instance; struct {uint32_t BaudRate;} Init; uint32_t RxState,ErrorCode,gState;} UART_HandleTypeDef;
extern UART_HandleTypeDef huart3;
static unsigned task_scan_contrHandle=1;
static unsigned clock_tick, irq_mask, sent, heartbeat, aborts, callbacks;
static GPIO_PinState cts=GPIO_PIN_SET, aim=GPIO_PIN_SET;
static unsigned mode;
static bool pending;
static uint8_t tx[134];
static uint32_t __get_PRIMASK(void){return irq_mask;}
static void __disable_irq(void){irq_mask=1;}
static void __set_PRIMASK(uint32_t v){irq_mask=v;}
static uint32_t HAL_GetTick(void){return clock_tick;}
static uint32_t osKernelGetTickFreq(void){return 1000;}
static uint32_t osThreadFlagsSet(unsigned h,unsigned f){(void)h;return f;}
static void AppWatchdog_Heartbeat(unsigned c){assert(c==1);++heartbeat;}
static void HAL_GPIO_WritePin(void *port,unsigned pin,GPIO_PinState state){
    if(port==CTS_GPIO_Port){assert(pin==CTS_Pin);cts=state;}
    else {assert(port==AIMWAKE_GPIO_Port && pin==AIMWAKE_Pin);aim=state;}
}
static GPIO_PinState HAL_GPIO_ReadPin(void *port,unsigned pin){
    assert(port==RTS_GPIO_Port && pin==RTS_Pin);
    if(mode==1)return GPIO_PIN_SET; // no grant
    if(mode==2)return GPIO_PIN_RESET; // stuck grant before request
    return cts;
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *u);
static int osDelay(uint32_t ticks){
    assert(!irq_mask);clock_tick+=ticks;
    if(pending && mode!=3){pending=false;huart3.gState=0x20;++callbacks;HAL_UART_TxCpltCallback(&huart3);}
    return osOK;
}
static HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *u,uint8_t *p,uint16_t n){
    assert(u==&huart3 && cts==GPIO_PIN_RESET && n==1);
    if(mode==4)return HAL_BUSY;
    tx[sent++]=p[0];u->gState=0x21;pending=true;return HAL_OK;
}
static HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *u){
    ++aborts;pending=false;u->gState=0x20;return HAL_OK;
}
static uint32_t HAL_UARTEx_GetRxEventType(UART_HandleTypeDef *u){(void)u;return 2;}
static HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *u,uint8_t *p,uint16_t n){
    (void)u;(void)p;assert(n==128);return HAL_OK;
}
#endif
'''
MAIN = r'''
int main(void){
 huart3.Instance=USART3;huart3.Init.BaudRate=9600;huart3.gState=0x20;
 assert(ScanUart_Init());
 assert(ScanUart_Wake() && aim==GPIO_PIN_SET && clock_tick>=12);
 uint8_t data[]={4,0xe4,4,0,0xff,0x14};
 assert(ScanUart_SendStart(data,6));
 assert(sent==6 && callbacks==6 && cts==GPIO_PIN_SET && !memcmp(tx,data,6));
 ScanUartTxDiagnostics_t d,old;ScanUart_GetTxDiagnostics(&d);
 assert(d.sequence==1 && d.requested_len==6 && (d.state&0x100));old=d;
 assert(ScanUart_Send(data,6));ScanUart_GetTxDiagnostics(&d);
 assert(!memcmp(&old,&d,sizeof d));
 mode=1;unsigned before=sent;assert(!ScanUart_Send(data,6));
 assert(sent==before && cts==GPIO_PIN_SET && heartbeat>0);
 mode=2;assert(!ScanUart_Send(data,6));assert(cts==GPIO_PIN_SET);
 mode=3;assert(!ScanUart_SendStart(data,6));
 assert(aborts==1 && !pending && cts==GPIO_PIN_SET);
 ScanUart_GetTxDiagnostics(&d);assert(!(d.state&0x100));
 mode=4;assert(!ScanUart_SendStart(data,6));assert(cts==GPIO_PIN_SET);
 ScanUart_GetTxDiagnostics(&d);assert((d.state&255)==HAL_BUSY && !(d.state&0x100));
 mode=0;assert(ScanUart_SendStart(data,6));assert(cts==GPIO_PIN_SET);
 assert(!ScanUart_Send(NULL,6));assert(!ScanUart_Send(data,0));
 puts("PASS: hardware wake, byte grants, final TC release, watchdog, stuck RTS, DMA stall/abort, HAL_BUSY and retry");
}
'''
with tempfile.TemporaryDirectory() as directory:
    d=Path(directory)
    (d/'main.h').write_text(FAKE)
    for name in ('cmsis_os.h','app_config.h','app_queues.h','task_watchdog.h'):
        (d/name).write_text('#include "main.h"\n')
    (d/'test.c').write_text('#include "main.h"\nUART_HandleTypeDef huart3;\n'+f'#include "{FW / "App/src/scan_uart_driver.c"}"\n'+MAIN)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(d),'-I'+str(FW/'App/inc'),str(d/'test.c'),'-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)
