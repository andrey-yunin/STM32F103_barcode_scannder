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
static bool probe_busy, queue_failure, consume_during_put;
static unsigned app_overflows, enqueue_count;
static bool queue_timeout_first, queue_error_first, force_start_busy, preserve_barcode;
static bool startup_wake_failure, startup_tx_failure;
static unsigned command_failure;
static unsigned saved_primask_observed;
static osStatus_t osMessageQueuePut(void *, const void *, uint8_t, uint32_t);
static bool AppConfig_Commit(void);
static void CAN_Diagnostics_RecordAppQueueOverflow(void);
static void assert_busy(void) { if(probe_busy) assert(ScanTask_IsFlashCommitBusy()); }

static void Error_Handler(void);
static volatile bool reset_stop_active;
static void feed_bytes(const uint8_t *p,unsigned n);
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
    assert(!irq); assert_busy(); delay_calls++;
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
    assert_busy();
    if(command_failure==3U && queue_gets==1U)return HAL_ERROR;
    if(startup_tx_failure && *p==4U && !tx_index && !tx_starts) return HAL_ERROR;
    (void)h;assert(p && n==1 && irq && (!AppSafety_IsResetPending() || reset_stop_active));
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
void CAN_SendDone(uint16_t cmd,uint8_t dev) { assert_busy(); (void)cmd;(void)dev; assert(!irq);done++; }
void CAN_SendNackPublic(uint16_t cmd,uint16_t err) { assert_busy(); (void)cmd;assert(!irq);nack++;last_nack=err;last_nack_tick=ticks; }
void CAN_SendDataFragmented(uint16_t cmd,const uint8_t *data,uint8_t len) {
    (void)cmd;assert(!irq && data && len);if(ack_mode){assert(sent_acks==1 && len==3 && !memcmp(data,"ABC",3));}fragments++;
}
bool CAN_SendDiagnosticResponse(uint16_t cmd,const uint8_t *data,uint16_t len) { assert_busy();
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
    if(port==GPIOA && (pins&CTS_Pin) && level==GPIO_PIN_SET && reset_stop_active && read_active) {
        const uint8_t ack[]={4,0xD0,0,0,0xFF,0x2C};feed_bytes(ack,sizeof(ack));
    }
    if(port==GPIOA && (pins&CTS_Pin) && level==GPIO_PIN_SET)tx_index=0;
    if ((port==GPIOA && (pins&CTS_Pin) && level==GPIO_PIN_RESET)
            || (port==GPIOB && (pins&AIMWAKE_Pin) && level==GPIO_PIN_RESET)) {
        assert(irq && (!AppSafety_IsResetPending() || (port==GPIOA && reset_stop_active)));
    }
}
static osStatus_t osMessageQueueGet(void *q,void *out,void *priority,uint32_t n) {
    (void)priority;(void)n;assert(q==scan_queueHandle && !irq);
    if (queue_gets++) {
        assert(!ScanTask_IsFlashCommitBusy());
        assert(scan_pending_commands==0U);
        if(queue_timeout_first && queue_gets==2U)return osErrorTimeout;
        if(queue_error_first && queue_gets==2U)return osError;
        host_terminal(3);
    }
    assert(!ScanTask_IsFlashCommitBusy());
    if(force_start_busy)assert(ScanCtrl_StartScan(4U));
    if(preserve_barcode) {
        uint8_t packet[SSI_PACKET_MAX_LEN], payload[]={3,'A','B','C'};
        unsigned len=Ssi_Encode(SSI_OP_DECODE_DATA,payload,sizeof(payload),packet);
        assert(ScanCtrl_StartScan(4U));assert(ScanCtrl_ProcessReceived(packet,len));
    }
    ScanTask_ReserveCommand();
    assert(ScanTask_IsFlashCommitBusy());
    // Model preemption after queue removal, before Get returns to task.
    assert(scan_pending_commands==1U);
    ScanResult_t command={.cmd_code=queued_command,.timeout_sec=4};
    memcpy(out,&command,sizeof(command));
    if(command_failure==1U)clear_fail=1;
    if(command_failure==2U)delay_fail_call=delay_calls+1U;
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
    (void)f;(void)opt;assert(!irq);assert_busy();flag_waits++;
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
    probe_busy=queue_failure=consume_during_put=false;
    queue_timeout_first=queue_error_first=force_start_busy=preserve_barcode=false;
    startup_wake_failure=startup_tx_failure=false;
    app_overflows=enqueue_count=saved_primask_observed=0;command_failure=0;
    scan_pending_commands=1U;
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
    read_active=read_error=rx_cancelled=reset_stop_active=false;app_safety_reset_started=0;holding_len=0;capture_active=false;
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


static osStatus_t osMessageQueuePut(void *q,const void *item,uint8_t priority,uint32_t timeout) {
    assert(q==scan_queueHandle && item && !priority && !timeout && !irq);
    assert(ScanTask_IsFlashCommitBusy());
    enqueue_count++;
    if(queue_failure)return osError;
    if(consume_during_put) {
        // Successful publication can wake/complete the consumer before
        // osMessageQueuePut returns. Dispatcher must not reserve after Put.
        ScanTask_ReleaseCommand();
        assert(!ScanTask_IsFlashCommitBusy());
    }
    return osOK;
}
static void CAN_Diagnostics_RecordAppQueueOverflow(void) { app_overflows++; }
static bool AppConfig_Commit(void) {
    assert(!irq && !ScanTask_IsFlashCommitBusy());
    assert(!AppSafety_IsResetPending());
    flash_calls++;
    // Dispatcher is occupied by this synchronous call. Idle scan task can
    // time out/retry Get, but no command producer runs concurrently.
    saved_primask_observed=irq;
    return !flash_fail;
}
#include "dispatcher_blocks.inc"

static void run_one(uint16_t command,unsigned sc) {
    reset_model();probe_busy=true;scenario=sc;queued_command=command;
    escape_active=true;
    int reason=setjmp(escape);
    if(!reason)app_start_task_scan_controller(NULL);
    assert(reason==3 && scan_pending_commands==0 && !ScanTask_IsFlashCommitBusy());
    escape_active=false;
}
int main(void) {
    reset_model();assert(ScanTask_IsFlashCommitBusy());
    test_commit();assert(!flash_calls && nack==1 && last_nack==CAN_ERR_DEVICE_BUSY);
    assert(!AppSafety_IsResetPending());
    puts("PASS F003 BUSY before task startup; no Flash or reset-latch");

    for(unsigned mask=0;mask<2;mask++) {
        reset_model();irq=mask;assert(ScanTask_IsFlashCommitBusy()&&irq==mask);
        ScanTask_ReserveCommand();assert(scan_pending_commands==2U&&irq==mask);
        ScanTask_ReleaseCommand();assert(scan_pending_commands==1U&&irq==mask);
        ScanTask_ReleaseCommand();assert(!ScanTask_IsFlashCommitBusy()&&irq==mask);
    }
    puts("PASS reserve/release/snapshot preserve enabled and disabled PRIMASK");

    const uint16_t codes[]={CAN_CMD_SCAN_REQUEST,CAN_CMD_SCAN_GET_REVISION,CAN_CMD_SCAN_BEEP,CAN_CMD_SCAN_GET_DATA};
    for(unsigned i=0;i<4;i++) {
        reset_model();ScanTask_ReleaseCommand();
        test_enqueue(codes[i]);assert(scan_pending_commands==1U&&!app_overflows&&!nack);
        test_commit();assert(!flash_calls&&last_nack==CAN_ERR_DEVICE_BUSY);
        ScanTask_ReleaseCommand();
        assert(!ScanTask_IsFlashCommitBusy());
    }
    puts("PASS actual Dispatcher reserves all four domain commands before queue publication; queued F003 BUSY");

    reset_model();ScanTask_ReleaseCommand();queue_failure=true;
    test_enqueue(CAN_CMD_SCAN_REQUEST);
    assert(!ScanTask_IsFlashCommitBusy()&&app_overflows==1&&nack==1&&last_nack==CAN_ERR_DEVICE_BUSY);
    test_commit();assert(flash_calls==1&&done==1);
    puts("PASS failed enqueue rolls back exactly once; overflow and BUSY preserved; subsequent Commit succeeds");

    reset_model();ScanTask_ReleaseCommand();consume_during_put=true;
    test_enqueue(CAN_CMD_SCAN_REQUEST);assert(!ScanTask_IsFlashCommitBusy());
    test_commit();assert(flash_calls==1&&done==1&&!nack);
    puts("PASS consumer completes before Put returns: reservation ordering avoids gap or leaked busy");

    reset_model();
    for(unsigned i=0;i<4;i++)test_enqueue(codes[i]);
    assert(scan_pending_commands==5U);
    for(unsigned i=0;i<4;i++){ScanTask_ReleaseCommand();assert(ScanTask_IsFlashCommitBusy());}
    ScanTask_ReleaseCommand();assert(!ScanTask_IsFlashCommitBusy());
    puts("PASS startup plus queued/in-flight reservations remain busy until final owner releases");

    for(unsigned sc=0;sc<13;sc++)run_one(CAN_CMD_SCAN_REQUEST,sc);
    puts("PASS 13 actual scan paths: busy through startup/UART/waits/terminal/ACK/failure STOP; released once afterward");
    run_one(CAN_CMD_SCAN_GET_REVISION,0);assert(done==1&&diagnostics==1);
    run_one(CAN_CMD_SCAN_BEEP,0);assert(done==1&&diagnostics==1);
    run_one(CAN_CMD_SCAN_GET_DATA,0);assert(nack==1&&last_nack==CAN_ERR_SCAN_NO_DATA);
    run_one(0x06FF,0);assert(nack==1&&last_nack==CAN_ERR_UNKNOWN_CMD);
    puts("PASS revision/BEEP capture, GET_DATA empty and unknown command outer continue release exactly once");

    reset_model();probe_busy=true;queued_command=CAN_CMD_SCAN_GET_DATA;preserve_barcode=true;
    escape_active=true;int reason=setjmp(escape);
    if(!reason)app_start_task_scan_controller(NULL);
    assert(reason==3&&!ScanTask_IsFlashCommitBusy()&&done==1&&fragments==1&&!nack);
    escape_active=false;
    puts("PASS GET_DATA cached barcode success releases once after DATA/DONE");

    for(unsigned which=0;which<3;which++) {
        reset_model();probe_busy=true;queued_command=CAN_CMD_SCAN_REQUEST;
        queue_timeout_first=which==0;queue_error_first=which==1;force_start_busy=which==2;
        escape_active=true;reason=setjmp(escape);
        if(!reason)app_start_task_scan_controller(NULL);
        assert(reason==3&&!ScanTask_IsFlashCommitBusy());escape_active=false;
        if(which==2)assert(nack==1&&last_nack==CAN_ERR_DEVICE_BUSY);
    }
    puts("PASS queue timeout/error do not double-release; StartScan rejection releases command");

    for(unsigned failure=1;failure<=3;failure++) {
        for(unsigned query=0;query<=(failure==1U?0U:1U);query++) {
            reset_model();probe_busy=true;command_failure=failure;
            queued_command=query?CAN_CMD_SCAN_GET_REVISION:CAN_CMD_SCAN_REQUEST;
            escape_active=true;reason=setjmp(escape);
            if(!reason)app_start_task_scan_controller(NULL);
            assert(reason==3&&!ScanTask_IsFlashCommitBusy()&&nack==1&&!done);
            escape_active=false;
        }
    }
    puts("PASS BeginRead/wake/START TX and capture wake/TX early failures release exactly once");

    for(unsigned query=0;query<=1;query++) {
        reset_model();probe_busy=true;queued_command=CAN_CMD_SCAN_BEEP;
        if(query)reset_at=50U;else queue_mode=1;
        escape_active=true;reason=setjmp(escape);
        if(!reason)app_start_task_scan_controller(NULL);
        assert(reason==3&&!ScanTask_IsFlashCommitBusy()&&nack==1&&!done);
        assert(scan_reset_prepared&&last_nack==CAN_ERR_DEVICE_BUSY);
        escape_active=false;
    }
    puts("PASS capture cancellation before start and during wait retains busy until STOP/RX closure");

    for(unsigned failure=0;failure<2;failure++) {
        reset_model();probe_busy=true;queued_command=CAN_CMD_SCAN_GET_DATA;
        if(failure)startup_tx_failure=true;else delay_fail_call=1;
        escape_active=true;reason=setjmp(escape);
        if(!reason)app_start_task_scan_controller(NULL);
        assert(reason==3&&!ScanTask_IsFlashCommitBusy()&&nack==1);
        escape_active=false;
    }
    puts("PASS failed startup wake/ENABLE TX releases initial ownership after synchronous return");

    for(unsigned fail=0;fail<2;fail++) {
        reset_model();ScanTask_ReleaseCommand();flash_fail=fail;
        test_commit();assert(flash_calls==1&&!saved_primask_observed&&!AppSafety_IsResetPending());
        assert(fail?(nack==1&&last_nack==CAN_ERR_FLASH_WRITE&&!done):(done==1&&!nack));
        assert(!ScanTask_IsFlashCommitBusy());
        test_enqueue(CAN_CMD_SCAN_REQUEST);assert(ScanTask_IsFlashCommitBusy());
    }
    puts("PASS Commit bool DONE/FLASH_WRITE and next enqueue after either outcome; IRQ remain enabled");
    puts("HAL/RTOS deterministic model of extracted candidate C only; BUILD/BENCH not implied.");
    return 0;
}
