/* Deterministic boundary/failure model. Actual functions in extracted.inc. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "dds240_global_config.h"
#include "scan_uart_driver.h"
#include "scan_controller.h"
#include "scan_ssi.h"
#include "task_watchdog.h"
#include "task_scan_controller.h"
#include "app_safety.h"

typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef int osStatus_t;
enum { osOK=0, osError=-1, osErrorTimeout=-2 };
#define osFlagsError 0x80000000U
#define osFlagsErrorTimeout 0xFFFFFFFEU
#define osFlagsWaitAny 0U
#define GPIOA ((void *)1)
#define GPIOB ((void *)2)
#define USART3 ((void *)3)
#define GPIO_PIN_1 (1U<<1)
#define GPIO_PIN_2 (1U<<2)
#define GPIO_PIN_3 (1U<<3)
#define GPIO_PIN_8 (1U<<8)
#define GPIO_PIN_9 (1U<<9)
#define GPIO_PIN_10 (1U<<10)
#define GPIO_PIN_12 (1U<<12)
#define GPIO_PIN_13 (1U<<13)
#define GPIO_PIN_14 (1U<<14)
#define GPIO_PIN_15 (1U<<15)
#define GPIO_MODE_OUTPUT_PP 1
#define GPIO_NOPULL 0
#define GPIO_SPEED_FREQ_LOW 1
typedef struct { uint32_t Pin,Mode,Pull,Speed; } GPIO_InitTypeDef;
typedef struct {unsigned disabled;} DMA_HandleTypeDef;
static DMA_HandleTypeDef rx_dma;
#define DMA_IT_HT 4U
#define __HAL_DMA_DISABLE_IT(h, flags) ((h)->disabled |= (flags))
typedef struct {void *Instance; struct {uint32_t BaudRate;} Init; uint32_t gState,RxState,ErrorCode;DMA_HandleTypeDef *hdmarx;} UART_HandleTypeDef;
UART_HandleTypeDef huart3={.Instance=USART3,.Init={9600},.hdmarx=&rx_dma};
static void *scan_queueHandle=(void *)4, *task_scan_contrHandle=(void *)5;
static uint32_t irq, ticks, freq=1000, gpioa, gpiob, flags_word;
static unsigned rx_arms, tx_starts, tx_aborts, rx_aborts, delay_calls, beats[3];
static unsigned done, nack, fragments, diagnostics, flash_calls, ready_tick, delay_fail_call;
static unsigned queue_gets, flag_sets, test_count, flag_waits, ack_mode, ack_stage, sent_acks;
static unsigned tx_index, tx_length, stop_sends;
static uint8_t tx_packet[134];
static uint32_t first_wait_tick, last_nack_tick;
static uint16_t last_nack;
static uint32_t reset_at, initial_ticks;
static int tx_abort_fail,rx_abort_fail,clear_fail,flash_fail,tx_pending,tx_completes;
static int callback_in_abort, reset_on_restore, prepare_on_delay, wait_mode, queue_mode;
static uint16_t queued_command;
static jmp_buf escape;
static bool escape_active;
static void (*reset_hook)(void);
static void Error_Handler(void);
static void HAL_UART_TxCpltCallback(UART_HandleTypeDef *h);
static void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h);
static bool Scan_SyncResetState(void);
static void host_terminal(int reason) { assert(escape_active); longjmp(escape,reason); }
static uint32_t __get_PRIMASK(void) { return irq; }
static void __disable_irq(void) { irq=1; }
static void __set_PRIMASK(uint32_t value) {
    irq=value;
    if (!irq && reset_on_restore) {
        reset_on_restore=0;
        AppSafety_PrepareReset();
    }
}
static void __DSB(void) {}
static void __ISB(void) {}
static void __DMB(void) {}
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
static void HAL_GPIO_WritePin(void *port,uint32_t pins,GPIO_PinState level);
static void HAL_GPIO_Init(void *port,GPIO_InitTypeDef *g) { (void)port; assert(g->Mode==GPIO_MODE_OUTPUT_PP); }
static GPIO_PinState HAL_GPIO_ReadPin(void *port,uint32_t pin) {
    (void)port;(void)pin;
    return (gpioa & GPIO_PIN_10) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}
static uint32_t HAL_GetTick(void) { return ticks; }
static uint32_t osKernelGetTickCount(void) { return ticks; }
static uint32_t osKernelGetTickFreq(void) { return freq; }
static osStatus_t osDelay(uint32_t n) {
    assert(!irq); delay_calls++;
    if (delay_fail_call==delay_calls) return osError;
    ticks+=n;
    if (reset_at && (uint32_t)(ticks-initial_ticks)>=reset_at) {
        reset_at=0; AppSafety_PrepareReset();
    }
    if (reset_hook) { void (*hook)(void)=reset_hook; reset_hook=NULL; hook(); }
    if (prepare_on_delay && (uint32_t)(ticks-initial_ticks)>=ready_tick) (void)Scan_SyncResetState();
    if (tx_pending && tx_completes) {
        tx_pending=0; HAL_UART_TxCpltCallback(&huart3);
    }
    return osOK;
}
void AppWatchdog_Heartbeat(AppWatchdogClient_t c) { assert((unsigned)c<3); beats[c]++; }
static int arm_failure, notify_failure;
#define HAL_UART_STATE_READY 0x20U
#define HAL_UART_STATE_BUSY_RX 0x22U
#define HAL_UART_ERROR_NONE 0U
#define HAL_UART_ERROR_NE 2U
#define HAL_UART_ERROR_ORE 8U
static HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *h,uint8_t *p,uint16_t n) {
    assert(p && n==1 && irq);rx_arms++;
    if(arm_failure)return HAL_ERROR;
    h->RxState=HAL_UART_STATE_BUSY_RX;h->ErrorCode=0;return HAL_OK;
}
static HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *h,const uint8_t *p,uint16_t n) {
    (void)h;assert(p && n==1 && irq && !AppSafety_IsResetPending());
    if(ack_stage && ack_mode==3){ack_stage=0;return HAL_ERROR;}
    if(!tx_index)tx_length=*p+2U;
    tx_packet[tx_index++]=*p;
    if(tx_index==tx_length){if(tx_packet[1]==SSI_OP_STOP_DECODE)stop_sends++;
        if(tx_packet[1]==SSI_OP_CMD_ACK){const uint8_t expected[]={4,0xD0,4,0,0xFF,0x28};assert(tx_length==6&&!memcmp(tx_packet,expected,6));sent_acks++;}
        tx_index=0;
    }
    tx_starts++;tx_pending=1;
    if(ack_stage && ack_mode==4){ack_stage=0;reset_on_restore=1;}
    return HAL_OK;
}
static HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *h) {
    tx_aborts++;
    if (callback_in_abort) HAL_UART_TxCpltCallback(h);
    if (tx_abort_fail) return HAL_ERROR;
    tx_pending=0; return HAL_OK;
}
static HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *h) {
    (void)h; rx_aborts++; return rx_abort_fail ? HAL_ERROR:HAL_OK;
}
static uint32_t HAL_UARTEx_GetRxEventType(UART_HandleTypeDef *h) { (void)h; return 0; }
static uint32_t osThreadFlagsSet(void *thread,uint32_t f) {
    assert(thread==task_scan_contrHandle);flag_sets++;if(notify_failure)return osFlagsError;flags_word|=f;return flags_word;
}
static uint32_t osThreadFlagsClear(uint32_t f) {
    if(clear_fail)return osFlagsError;
    uint32_t old=flags_word;flags_word&=~f;return old;
}
static uint32_t osThreadFlagsWait(uint32_t f,uint32_t opt,uint32_t n);
static osStatus_t osMessageQueueGet(void *q,void *out,void *priority,uint32_t n);
void CAN_SendDone(uint16_t cmd,uint8_t dev) { (void)cmd;(void)dev; assert(!irq);done++; }
void CAN_SendNackPublic(uint16_t cmd,uint16_t err) { (void)cmd;assert(!irq);nack++;last_nack=err;last_nack_tick=ticks; }
void CAN_SendDataFragmented(uint16_t cmd,const uint8_t *data,uint8_t len) {
    (void)cmd;assert(!irq && data && len);if(ack_mode){assert(sent_acks==1 && len==3 && !memcmp(data,"ABC",3));}fragments++;
}
bool CAN_SendDiagnosticResponse(uint16_t cmd,const uint8_t *data,uint16_t len) {
    (void)cmd;assert(!irq && data && len);diagnostics++;done++;
    return true;
}
static bool AppConfig_FactoryReset(void) {
    assert(AppSafety_IsResetPending() && ScanTask_IsResetPrepared());
    flash_calls++;return !flash_fail;
}
static void NVIC_SystemReset(void) { assert(AppSafety_IsResetPending() && ScanTask_IsResetPrepared());host_terminal(2); }

#include "extracted.inc"
static void HAL_GPIO_WritePin(void *port,uint32_t pins,GPIO_PinState level) {
    uint32_t *value=port==GPIOA?&gpioa:&gpiob;
    if (level==GPIO_PIN_SET) *value|=pins; else *value&=~pins;
    if ((port==GPIOA && (pins&CTS_Pin) && level==GPIO_PIN_RESET)
            || (port==GPIOB && (pins&AIMWAKE_Pin) && level==GPIO_PIN_RESET)) {
        assert(irq && !AppSafety_IsResetPending());
    }
}
static osStatus_t osMessageQueueGet(void *q,void *out,void *priority,uint32_t n) {
    (void)priority;(void)n;assert(q==scan_queueHandle && !irq);
    if (queue_gets++) host_terminal(3);
    ScanResult_t command={.cmd_code=queued_command,.timeout_sec=4};
    memcpy(out,&command,sizeof(command));
    if(queue_mode) AppSafety_PrepareReset();
    return osOK;
}

static void Error_Handler(void) { __disable_irq(); AppSafety_AllOff(); host_terminal(1); }
static void feed(uint8_t byte, uint32_t error) {
    assert(huart3.RxState==HAL_UART_STATE_BUSY_RX);
    rx_byte=byte;huart3.ErrorCode=error;huart3.RxState=HAL_UART_STATE_READY;
    HAL_UART_RxCpltCallback(&huart3);
    if(error)HAL_UART_ErrorCallback(&huart3);
}
static void feed_bytes(const uint8_t *p,unsigned n) { for(unsigned i=0;i<n;i++)feed(p[i],0); }
static unsigned scenario;
static void inject_rx_error(void) { feed(0,HAL_UART_ERROR_ORE); }
static uint32_t osThreadFlagsWait(uint32_t f,uint32_t opt,uint32_t n) {
    (void)f;(void)opt;assert(!irq);flag_waits++;
    if(scenario==3){ticks+=n;return osFlagsErrorTimeout;}
    if(scenario==4){AppSafety_PrepareReset();return FLAG_SCAN_UART_RX;}
    uint8_t packet[SSI_PACKET_MAX_LEN],payload[]={3,'9','0','2','2','2','2','6','0','3','0','0','8','1','0','7','0','1'};
    unsigned length=Ssi_Encode(SSI_OP_DECODE_DATA,payload,sizeof(payload),packet);
    if(scenario==1)packet[length-1]^=1;
    if(scenario==2){feed(0,HAL_UART_ERROR_ORE);return FLAG_SCAN_UART_RX;}
    if(scenario==5){for(unsigned i=0;i<=SCAN_UART_PENDING_SIZE;i++)feed(4,0);return FLAG_SCAN_UART_RX;}
    if(scenario==6){feed(packet[0],0);scenario=3;return FLAG_SCAN_UART_RX;}
    if(scenario==7){feed(packet[flag_waits-1],0);return FLAG_SCAN_UART_RX;}
    if(scenario==8){
        uint8_t ack[8];unsigned a=Ssi_Encode(SSI_OP_CMD_ACK,NULL,0,ack);
        feed_bytes(ack,a);feed_bytes(packet,length);return FLAG_SCAN_UART_RX;
    }
    feed_bytes(packet,length);
    if(scenario==9){ack_mode=3;ack_stage=1;}
    if(scenario==10){ack_mode=4;ack_stage=1;}
    if(scenario==11)reset_hook=inject_rx_error;
    if(scenario==12){feed(0,HAL_UART_ERROR_ORE);ticks+=4000;return osFlagsErrorTimeout;}
    return FLAG_SCAN_UART_RX;
}
static void reset_model(void) {
    irq=ticks=flags_word=0;freq=1000;gpioa=CTS_Pin;gpiob=TRIG_Pin|AIMWAKE_Pin;
    rx_arms=tx_starts=tx_aborts=rx_aborts=delay_calls=0;
    done=nack=fragments=diagnostics=flash_calls=ready_tick=delay_fail_call=0;
    queue_gets=flag_sets=last_nack=flag_waits=ack_mode=ack_stage=sent_acks=tx_index=tx_length=0;
    reset_at=0;initial_ticks=0;memset(beats,0,sizeof(beats));
    tx_abort_fail=rx_abort_fail=clear_fail=flash_fail=tx_pending=0;tx_completes=1;
    callback_in_abort=reset_on_restore=prepare_on_delay=wait_mode=queue_mode=0;
    arm_failure=notify_failure=0;scenario=0;queued_command=CAN_CMD_SCAN_REQUEST;reset_hook=NULL;
    app_safety_reset_pending=scan_reset_prepared=false;
    active_start_sequence=0;tx_byte_done=tx_last_byte=false;
    read_active=read_error=rx_cancelled=reset_stop_active=false;holding_len=0;capture_active=false;
    rx_arm_status=rx_event_type=0xFF;
    memset(&capture,0,sizeof(capture));memset(&rx_diag,0,sizeof(rx_diag));memset(&start_tx,0,sizeof(start_tx));
    huart3.RxState=HAL_UART_STATE_READY;huart3.ErrorCode=0;
    ScanCtrl_Init();assert(ScanUart_Init());
}
static void check(bool condition) { assert(condition); }
static void pass(const char *label) { puts(label); }
static void inject_reset(void) { AppSafety_PrepareReset(); }
static void test_tx(void) {
    const uint8_t packet[]={4,0xE4,4,0,0xFF,0x14};
    reset_model();assert(ScanUart_SendStart(packet,sizeof(packet)));assert(tx_starts==6 && !tx_aborts);
    pass("ordinary START transfer completes before buffer return");
    reset_model();AppSafety_PrepareReset();assert(!ScanUart_SendStart(packet,sizeof(packet)));assert(!tx_starts);
    reset_model();reset_on_restore=1;assert(!ScanUart_SendStart(packet,sizeof(packet)));assert(!tx_starts);
    pass("reset before send or between CTS and byte gate prevents UART IT launch");
    reset_model();tx_completes=0;reset_hook=inject_reset;callback_in_abort=1;
    assert(!ScanUart_SendStart(packet,sizeof(packet)));assert(tx_aborts==1 && tx_starts==1 && !tx_pending);
    assert(!(start_tx.state&0x100U) && !active_start_sequence && (gpioa&CTS_Pin));
    pass("reset in TX wait aborts; injected late callback cannot mark START complete");
    reset_model();tx_completes=0;assert(!ScanUart_SendStart(packet,sizeof(packet)));assert(tx_aborts==1 && !tx_pending);
    pass("TX timeout uses checked abort before returning buffer");
    reset_model();tx_completes=0;tx_abort_fail=1;escape_active=true;
    int result=setjmp(escape);
    if (!result) { (void)ScanUart_SendStart(packet,sizeof(packet));assert(0); }
    assert(result==1 && irq && tx_pending && !done && !nack);escape_active=false;
    pass("failed TX abort is terminal and cannot return normal operation success");
}


static void test_reset_rx_boundary(void) {
    reset_model();assert(ScanUart_BeginRead());feed('a',0);
    AppSafety_PrepareReset();unsigned arms=rx_arms,notifications=flag_sets;
    feed('b',0);assert(rx_arms==arms+1 && holding_len==1 && flag_sets==notifications);
    assert(!ScanUart_StartRx());
    feed('!',HAL_UART_ERROR_ORE);assert(huart3.RxState==HAL_UART_STATE_BUSY_RX);
    assert(!read_error && holding_len==1 && flag_sets==notifications);
    reset_stop_active=true;read_active=true;holding_len=0;
    feed('s',0);assert(holding_len==1 && flag_sets==notifications+1);
    feed('!',HAL_UART_ERROR_ORE);assert(read_error);
    assert(ScanUart_CancelRx());assert(rx_cancelled&&!reset_stop_active&&!read_active&&!holding_len);
    arms=rx_arms;notifications=flag_sets;rx_byte=42;
    HAL_UART_RxCpltCallback(&huart3);HAL_UART_ErrorCallback(&huart3);
    assert(rx_arms==arms&&flag_sets==notifications&&!holding_len);
    irq=1;assert(!ScanUart_StartRxInternal(true)&&irq==1);irq=0;
    puts("PASS partial RX step: post-latch drain, no normal result, private STOP ownership/error, closure before abort, late callback, PRIMASK");
}

int main(void) {
    test_reset_rx_boundary();
    for(unsigned sc=0;sc<=12;sc++) {
        reset_model();stop_sends=0;scenario=sc;escape_active=true;
        int reason=setjmp(escape);
        if(!reason)app_start_task_scan_controller(NULL);
        check(reason==3);escape_active=false;
        check(!read_active && !holding_len);
        if(sc==0 || sc==7 || sc==8) {
            check(done==1 && fragments==1 && !nack && sent_acks==1 && !stop_sends);
            uint8_t code[SHADOW_BUF_SIZE],length=0;
            check(ScanCtrl_GetLastBarcode(code,&length) && length==17);
            check(!memcmp(code,"90222260300810701",17));
            unsigned notifications=flag_sets;feed('x',0);
            check(!holding_len && flag_sets==notifications);
        } else if(sc==4 || sc==10) {
            check(nack==1 && last_nack==CAN_ERR_DEVICE_BUSY && !done && !fragments && !stop_sends);
            check(scan_reset_prepared);
        } else {
            check(nack==1 && !done && !fragments && stop_sends==1);
            if(sc==1)check(last_nack==CAN_ERR_SCAN_HARDWARE);
            else if(sc==3 || sc==6)check(last_nack==CAN_ERR_SCAN_TIMEOUT);
            else check(last_nack==CAN_ERR_SCAN_COMM);
        }
        printf("PASS task scenario%u: RX closed, one terminal, STOP policy preserved on failure\n",sc);
    }
    puts("PASS complete/bytewise/ACK+DATA17 digits; checksum; RX error/storage error; timeout/partial; ACK send failure; reset during wait/ACK; RX error during ACK/at deadline");
    puts("HAL/RTOS model only; no STOP ACK wait, reset/Flash redesign or BENCH");
    return 0;
}
