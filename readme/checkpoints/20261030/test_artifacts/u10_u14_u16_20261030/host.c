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
typedef struct {void *Instance; struct {uint32_t BaudRate;} Init; uint32_t gState,RxState,ErrorCode;} UART_HandleTypeDef;
UART_HandleTypeDef huart3={.Instance=USART3,.Init={9600}};
static void *scan_queueHandle=(void *)4, *task_scan_contrHandle=(void *)5;
static uint32_t irq, ticks, freq=1000, gpioa, gpiob, flags_word;
static unsigned rx_arms, tx_starts, tx_aborts, rx_aborts, delay_calls, beats[3];
static unsigned done, nack, fragments, diagnostics, flash_calls, ready_tick, delay_fail_call;
static unsigned queue_gets, flag_sets, test_count;
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
static void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *h,uint16_t n);
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
static HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *h,uint8_t *p,uint16_t n) {
    (void)h;(void)p;(void)n; assert(irq && !AppSafety_IsResetPending()); rx_arms++; return HAL_OK;
}
static HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *h,const uint8_t *p,uint16_t n) {
    (void)h; assert(p && n==1 && irq && !AppSafety_IsResetPending());tx_starts++;tx_pending=1;return HAL_OK;
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
    assert(thread==task_scan_contrHandle);flag_sets++;flags_word|=f;return flags_word;
}
static uint32_t osThreadFlagsClear(uint32_t f) {
    assert(!irq);if(clear_fail)return osFlagsError;
    uint32_t old=flags_word;flags_word&=~f;return old;
}
static uint32_t osThreadFlagsWait(uint32_t f,uint32_t opt,uint32_t n);
static osStatus_t osMessageQueueGet(void *q,void *out,void *priority,uint32_t n);
void CAN_SendDone(uint16_t cmd,uint8_t dev) { (void)cmd;(void)dev; assert(!irq);done++; }
void CAN_SendNackPublic(uint16_t cmd,uint16_t err) { (void)cmd;assert(!irq);nack++;last_nack=err; }
void CAN_SendDataFragmented(uint16_t cmd,const uint8_t *data,uint8_t len) {
    (void)cmd;assert(!irq && data && len);fragments++;
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
static uint32_t osThreadFlagsWait(uint32_t f,uint32_t opt,uint32_t n) {
    (void)f;(void)opt;assert(!irq);
    if (wait_mode==1) {
        (void)osDelay(n); AppSafety_PrepareReset(); return osFlagsErrorTimeout;
    }
    if (wait_mode==2) {
        (void)osDelay(1);AppSafety_PrepareReset();return FLAG_SCAN_UART_RX;
    }
    if (wait_mode==3) {
        (void)osDelay(1);AppSafety_PrepareReset();return osFlagsError;
    }
    const uint8_t payload[]={'A','B','C'};
    uint8_t len=Ssi_Encode(SSI_OP_DECODE_DATA,payload,sizeof(payload),dma_buffer);
    HAL_UARTEx_RxEventCallback(&huart3,len);
    return FLAG_SCAN_UART_RX;
}
static osStatus_t osMessageQueueGet(void *q,void *out,void *priority,uint32_t n) {
    (void)priority;(void)n;assert(q==scan_queueHandle && !irq);
    if (queue_gets++) host_terminal(3);
    ScanResult_t command={.cmd_code=queued_command,.timeout_sec=4};
    memcpy(out,&command,sizeof(command));
    if(queue_mode) AppSafety_PrepareReset();
    return osOK;
}
static void reset_model(void) {
    irq=ticks=flags_word=0;freq=1000;initial_ticks=0;gpioa=CTS_Pin;gpiob=TRIG_Pin|AIMWAKE_Pin;
    rx_arms=tx_starts=tx_aborts=rx_aborts=delay_calls=0;
    done=nack=fragments=diagnostics=flash_calls=ready_tick=delay_fail_call=0;
    queue_gets=flag_sets=last_nack=0;reset_at=0;
    memset(beats,0,sizeof(beats));
    tx_abort_fail=rx_abort_fail=clear_fail=flash_fail=tx_pending=0;tx_completes=1;
    callback_in_abort=reset_on_restore=prepare_on_delay=wait_mode=queue_mode=0;
    queued_command=CAN_CMD_SCAN_REQUEST;reset_hook=NULL;
    app_safety_reset_pending=scan_reset_prepared=false;
    active_start_sequence=0;tx_byte_done=tx_last_byte=false;
    holding_len=rx_prefix=0;capture_active=false;
    memset(&capture,0,sizeof(capture));memset(&rx_diag,0,sizeof(rx_diag));memset(&start_tx,0,sizeof(start_tx));
    ScanCtrl_Init();
}
static void pass(const char *label) { printf("PASS %s\n",label);test_count++; }
static void inject_reset(void) { AppSafety_PrepareReset(); }
static void test_safety_and_gates(void) {
    for(unsigned saved=0;saved<2;saved++) {
        reset_model();irq=saved;AppSafety_PrepareReset();assert(irq==saved && AppSafety_IsResetPending());
        assert((gpioa&CTS_Pin) && (gpiob&(TRIG_Pin|AIMWAKE_Pin))==(TRIG_Pin|AIMWAKE_Pin));
        assert(!(gpioa&(STBY_Pin|LED_CAN_Pin|LED_WORK_Pin)));
        assert(!ScanUart_Wake() && !ScanUart_StartRx() && irq==saved);
        assert(!Scan_TryAcceptTerminalResult() && irq==saved);
        assert(!rx_arms && !tx_starts && !delay_calls);
    }
    pass("latch safe GPIO and PRIMASK0/1; wake/RX/terminal denied after reset");
    reset_model();reset_on_restore=1;assert(Scan_TryAcceptTerminalResult());assert(AppSafety_IsResetPending());
    pass("terminal accepted before reset remains accepted after IRQ restoration");
    reset_model();assert(ScanUart_StartRx() && rx_arms==1 && !irq);
    reset_model();reset_hook=inject_reset;assert(!ScanUart_Wake());assert(gpiob&AIMWAKE_Pin);
    pass("ordinary RX start; reset during wake releases AIMWAKE and rejects success");
    reset_model();AppSafety_PrepareReset();assert(!ScanUart_WaitRts(GPIO_PIN_SET,2000));
    pass("RTS already ready does not override cancellation");
    reset_model();memcpy(dma_buffer,"xyz",3);AppSafety_PrepareReset();
    HAL_UARTEx_RxEventCallback(&huart3,3);
    assert(rx_diag.sequence==1 && !holding_len && !flag_sets && !rx_arms);
    pass("late RX retains raw diagnostic only; no domain result/rearm/notification");
    reset_model();capture_active=true;holding_len=rx_prefix=3;AppSafety_PrepareReset();
    assert(ScanUart_CancelRx() && !capture_active && !holding_len && !rx_prefix && rx_aborts==1);
    rx_abort_fail=1;assert(!ScanUart_CancelRx());
    pass("RX cancellation invalidates pending result and propagates HAL failure");
}
static void test_tx(void) {
    const uint8_t packet[]={4,0xE4,4,0,0xFF,0x14};
    reset_model();assert(ScanUart_SendStart(packet,sizeof(packet)));assert(tx_starts==6 && !tx_aborts);
    pass("ordinary START transfer completes before buffer return");
    reset_model();AppSafety_PrepareReset();assert(!ScanUart_SendStart(packet,sizeof(packet)));assert(!tx_starts);
    reset_model();reset_on_restore=1;assert(!ScanUart_SendStart(packet,sizeof(packet)));assert(!tx_starts);
    pass("reset before send or between CTS and byte gate prevents DMA launch");
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
static void test_sync(void) {
    reset_model();assert(!Scan_SyncResetState() && !rx_aborts && !scan_reset_prepared);
    assert(ScanCtrl_StartScan(4));flags_word=FLAG_SCAN_UART_RX;AppSafety_PrepareReset();
    assert(Scan_SyncResetState() && scan_reset_prepared && !(flags_word&FLAG_SCAN_UART_RX));
    assert(ScanCtrl_GetState()==SCAN_STATE_ERROR && g_stats.scan_timeouts==0);
    assert(Scan_SyncResetState() && rx_aborts==1);
    pass("domain prepare once, cancels SCANNING and old RX flag without false timeout");
    for(int failure=1;failure<=2;failure++) {
        reset_model();AppSafety_PrepareReset();rx_abort_fail=failure==1;clear_fail=failure==2;
        escape_active=true;int result=setjmp(escape);
        if(!result) { (void)Scan_SyncResetState();assert(0); }
        assert(result==1 && irq && !scan_reset_prepared);escape_active=false;
    }
    pass("RX abort or flag-clear failure cannot publish prepared");
}
static void test_dispatch(void) {
    const uint16_t cmds[]={CAN_CMD_SRV_REBOOT,CAN_CMD_SRV_FACTORY_RESET};
    for(unsigned which=0;which<2;which++) {
        uint16_t key=which?SRV_MAGIC_FACTORY_RESET:SRV_MAGIC_REBOOT;
        for(unsigned short_len=0;short_len<3;short_len++) {
            reset_model();ParsedCanCommand_t p={.cmd_code=cmds[which],.data_len=short_len<2?short_len:5};
            p.data[0]=(uint8_t)key;p.data[1]=(uint8_t)(key>>8);
            if(short_len==2)p.data[0]^=1;
            host_dispatch(p);assert(nack==1 && last_nack==CAN_ERR_INVALID_KEY && !flash_calls && !AppSafety_IsResetPending());
        }
        for(int erase_failure=0;erase_failure<=(int)which;erase_failure++) {
            reset_model();prepare_on_delay=1;ready_tick=500;flash_fail=erase_failure;
            ParsedCanCommand_t p={.cmd_code=cmds[which],.data_len=5};p.data[0]=key;p.data[1]=key>>8;
            escape_active=true;int result=setjmp(escape);
            if(!result){host_dispatch(p);assert(0);}
            assert(result==2 && scan_reset_prepared && ticks==600 && flash_calls==which);
            assert(done==(unsigned)!erase_failure && nack==(unsigned)erase_failure);
            if(erase_failure)assert(last_nack==CAN_ERR_FLASH_WRITE);
            escape_active=false;
        }
    }
    pass("F002/F006 invalid length/key has no latch/Flash; preparation before response/Flash; Flash error NACK then reset");
    for(int mode=0;mode<4;mode++) {
        reset_model();ParsedCanCommand_t p={.cmd_code=CAN_CMD_SRV_FACTORY_RESET,.data_len=5,.data={0xAD,0xDE}};
        if(mode==1)delay_fail_call=1;
        if(mode==2){prepare_on_delay=1;ready_tick=1;rx_abort_fail=1;}
        if(mode==3){prepare_on_delay=1;ready_tick=1;delay_fail_call=2;}
        escape_active=true;int result=setjmp(escape);
        if(!result){host_dispatch(p);assert(0);}
        assert(result==1 && irq);
        if(mode!=3)assert(!done && !flash_calls);
        else assert(done==1 && flash_calls==1);
        if(mode==0)assert(ticks==1000);
        if(mode==1)assert(beats[APP_WDG_CLIENT_DISPATCHER]==0);
        escape_active=false;
    }
    pass("prepare timeout/delay/RX failure prevents Flash; failed response delay is terminal");
    reset_model();ticks=initial_ticks=UINT32_MAX-200U;prepare_on_delay=1;ready_tick=500;
    Dispatcher_PrepareReset();assert(scan_reset_prepared && (uint32_t)(ticks-initial_ticks)==500);
    pass("preparation deadline works across RTOS tick wrap");
    reset_model();freq=100;prepare_on_delay=1;ready_tick=50;Dispatcher_PrepareReset();assert(ticks==50);
    pass("one-second prepare budget follows RTOS tick frequency");
}
static void test_task(void) {
    for(unsigned mode=0;mode<5;mode++) {
        reset_model();wait_mode=mode;queue_mode=mode==4;
        escape_active=true;int result=setjmp(escape);
        if(!result){app_start_task_scan_controller(NULL);assert(0);}
        assert(result==3 && !irq);
        if(mode==0){assert(done==1 && fragments==1 && g_stats.scan_success==1);}
        else {assert(!done && nack==1 && last_nack==CAN_ERR_DEVICE_BUSY && scan_reset_prepared && !g_stats.scan_timeouts && !g_stats.scan_success);}
        escape_active=false;
    }
    pass("actual Scanner task: ordinary barcode; queued cancellation; reset during RX/timeout/error never DONE or false stats");
    const uint16_t cmds[]={CAN_CMD_SCAN_GET_REVISION,CAN_CMD_SCAN_BEEP};
    for(unsigned i=0;i<2;i++) {
        reset_model();reset_at=50;escape_active=true;int result=setjmp(escape);
        if(!result)ScanQueryRevision(cmds[i]);else assert(0);
        assert(nack==1 && last_nack==CAN_ERR_DEVICE_BUSY && !done && !diagnostics && scan_reset_prepared && ticks<100);
        escape_active=false;
    }
    pass("actual 0603/0604 capture cancels before three-second deadline");
    reset_model();ScanQueryRevision(CAN_CMD_SCAN_GET_REVISION);assert(diagnostics==1 && done==1 && !nack);
    pass("ordinary capture still returns its diagnostic envelope");
}
static void test_fatal(void) {
    void (*handlers[])(void)={Error_Handler,NMI_Handler,HardFault_Handler,MemManage_Handler,BusFault_Handler,UsageFault_Handler};
    for(unsigned i=0;i<sizeof(handlers)/sizeof(handlers[0]);i++) {
        reset_model();gpioa=gpiob=0;escape_active=true;int result=setjmp(escape);
        if(!result){handlers[i]();assert(0);}
        assert(result==1 && irq && (gpioa&CTS_Pin) && (gpiob&(TRIG_Pin|AIMWAKE_Pin))==(TRIG_Pin|AIMWAKE_Pin));
        assert(!delay_calls && !done && !nack);escape_active=false;
    }
    pass("Error_Handler and five fault handlers: IRQ off, safe GPIO, terminal; no RTOS/CAN");
}
int main(void) {
    test_safety_and_gates();test_tx();test_sync();test_dispatch();test_task();test_fatal();
    printf("PASS %u test groups. MOCK HAL/RTOS; BENCH NOT RUN.\n",test_count);
    return 0;
}
