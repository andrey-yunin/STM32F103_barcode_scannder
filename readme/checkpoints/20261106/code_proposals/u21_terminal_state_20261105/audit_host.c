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

static unsigned audit_case, scenario;
static unsigned audit_done[2], audit_nack[2], audit_data[2];
static uint16_t audit_error[2];
static ScanState_t state_after_first;
static unsigned audit_delay_errors;
static bool fail_capture_delay;
static volatile bool capture_active;
static uint16_t first_command;
static unsigned audit_tx_failure;

static void assert_busy(void) { assert(ScanTask_IsFlashCommitBusy()); }
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
    if(queue_gets==1U) {
        if(audit_tx_failure==1U)return GPIO_PIN_RESET;
        if(audit_tx_failure==2U && !(gpioa&GPIO_PIN_10))return GPIO_PIN_SET;
        if(audit_tx_failure==3U && !(gpioa&GPIO_PIN_10) && tx_starts>6U)return GPIO_PIN_SET;
        if(audit_tx_failure==6U && (gpioa&GPIO_PIN_10) && tx_starts>=12U)return GPIO_PIN_RESET;
    }
    return (gpioa & GPIO_PIN_10) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}
static uint32_t HAL_GetTick(void) { return ticks; }
static uint32_t osKernelGetTickCount(void) { return ticks; }
static uint32_t osKernelGetTickFreq(void) { return freq; }
static osStatus_t osDelay(uint32_t n) {
    assert(!irq); assert_busy(); delay_calls++;
    if(fail_capture_delay && capture_active && tx_starts>=12U && !tx_pending) {
        audit_delay_errors++;ticks++;return osError;
    }
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
    (void)h;assert(p && n==1 && irq && (!AppSafety_IsResetPending() || reset_stop_active));
    if(queue_gets==1U&&audit_tx_failure==4U)return HAL_BUSY;
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
void CAN_SendDone(uint16_t cmd,uint8_t dev) { assert_busy(); (void)cmd;(void)dev; assert(!irq);done++;if(queue_gets>=1U&&queue_gets<=2U)audit_done[queue_gets-1U]++; }
void CAN_SendNackPublic(uint16_t cmd,uint16_t err) { assert_busy(); (void)cmd;assert(!irq);nack++;if(queue_gets>=1U&&queue_gets<=2U){audit_nack[queue_gets-1U]++;audit_error[queue_gets-1U]=err;}last_nack=err;last_nack_tick=ticks; }
typedef struct {
    struct { uint32_t ExtId,IDE,RTR,DLC; } header;
    uint8_t data[8];
} CanTxFrame_t;
#define CAN_ID_EXT 4U
#define CAN_RTR_DATA 0U
#define CAN_FRAME_DLC 8U
static uint32_t AppConfig_GetPerformerID(void) { return 0x60U; }
static void CAN_QueueTxFrame(const CanTxFrame_t *frame) {
    assert(!irq&&frame&&frame->data[0]==CAN_SUB_TYPE_DATA);
    assert_busy();fragments++;
    if(queue_gets>=1U&&queue_gets<=2U)audit_data[queue_gets-1U]++;
}
#include "can_fragment.inc"
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
    (void)priority;(void)n;assert(q==scan_queueHandle&&!irq);
    assert(!ScanTask_IsFlashCommitBusy());
    unsigned index=queue_gets++;
    if(index>=2U)host_terminal(3);
    if(index==1U) {
        state_after_first=ScanCtrl_GetState();
        scenario=0U;ack_mode=ack_stage=flag_waits=0U;
        fail_capture_delay=false;
        tx_completes=1;clear_fail=0;
    }
    ScanTask_ReserveCommand();
    if(index==0U) {
        if(audit_tx_failure==5U)tx_completes=0;
        if(audit_tx_failure==7U)delay_fail_call=delay_calls+1U;
        if(audit_tx_failure==8U)clear_fail=1;
    }
    ScanResult_t command={.cmd_code=index==0U?first_command:CAN_CMD_SCAN_REQUEST,.timeout_sec=4};
    memcpy(out,&command,sizeof(command));
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
static void inject_rx_error(void) { feed(0,HAL_UART_ERROR_ORE); }
static uint32_t osThreadFlagsWait(uint32_t f,uint32_t opt,uint32_t n) {
    (void)f;(void)opt;assert(!irq);flag_waits++;
    if(scenario==16U)return osFlagsError;
    if(scenario==17U){feed(3U,0U);return FLAG_SCAN_UART_RX;}
    if(scenario==13U || scenario==14U || scenario==15U) {
        uint8_t packet[SSI_PACKET_MAX_LEN];
        uint8_t value=3U;
        uint8_t opcode=scenario==13U?SSI_OP_REPLY_REVISION:(scenario==14U?SSI_OP_CMD_NAK:SSI_OP_DECODE_DATA);
        unsigned length=Ssi_Encode(opcode,&value,1U,packet);
        packet[2]=SSI_MSG_SRC_DECODER;
        uint16_t sum=0U;for(unsigned i=0U;i<length-2U;i++)sum+=packet[i];
        sum=(uint16_t)(0U-sum);packet[length-2U]=(uint8_t)(sum>>8);packet[length-1U]=(uint8_t)sum;
        feed_bytes(packet,length);return FLAG_SCAN_UART_RX;
    }
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
    memset(audit_done,0,sizeof(audit_done));memset(audit_nack,0,sizeof(audit_nack));
    memset(audit_data,0,sizeof(audit_data));memset(audit_error,0,sizeof(audit_error));
    state_after_first=SCAN_STATE_IDLE;audit_delay_errors=0U;fail_capture_delay=false;
    first_command=CAN_CMD_SCAN_REQUEST;audit_tx_failure=0U;
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


static void run_pair(unsigned sc) {
    reset_model();scenario=sc;escape_active=true;
    int reason=setjmp(escape);
    if(!reason)app_start_task_scan_controller(NULL);
    assert(reason==3&&!ScanTask_IsFlashCommitBusy());escape_active=false;
    assert(audit_done[0]+audit_nack[0]==1U);
    assert(audit_done[1]+audit_nack[1]==1U);
}
int main(void) {
    const unsigned recoverable[]={0U,1U,2U,3U,5U,6U,7U,8U,9U,11U,12U,14U};
    for(unsigned i=0U;i<sizeof(recoverable)/sizeof(recoverable[0]);i++) {
        unsigned sc=recoverable[i];run_pair(sc);
        assert(audit_done[1]==1U&&!audit_nack[1]&&audit_data[1]>0U);
        uint16_t expected=0U;
        if(sc==1U || sc==14U)expected=CAN_ERR_SCAN_HARDWARE;
        else if(sc==3U || sc==6U)expected=CAN_ERR_SCAN_TIMEOUT;
        else if(sc!=0U && sc!=7U && sc!=8U)expected=CAN_ERR_SCAN_COMM;
        assert(expected?(audit_nack[0]==1U&&audit_error[0]==expected):audit_done[0]==1U);
        printf("PASS pair scenario%u: first NACK=%04X DONE=%u; second read DATA/DONE; one terminal each\n",sc,audit_error[0],audit_done[0]);
    }
    run_pair(13U);
    assert(audit_nack[0]==1U&&audit_error[0]==CAN_ERR_SCAN_HARDWARE);
    assert(state_after_first==SCAN_STATE_ERROR);
    assert(audit_nack[1]==0U&&audit_done[1]==1U&&audit_data[1]>0U);
    puts("FIX VERIFIED U21-A: existing E601 preserved, state ERROR, next request succeeds with DATA/DONE");

    run_pair(15U);
    assert(audit_done[0]==1U&&!audit_nack[0]&&audit_data[0]==0U);
    assert(state_after_first==SCAN_STATE_DATA_READY);
    assert(audit_done[1]==1U&&audit_data[1]>0U);
    puts("REPRODUCED U21-B: DECODE_DATA with only Type -> SSI ACK, CAN DONE with zero CAN DATA, empty saved barcode");

    for(unsigned failure=1U;failure<=8U;failure++) {
        reset_model();audit_tx_failure=failure;escape_active=true;
        int reason=setjmp(escape);
        if(!reason)app_start_task_scan_controller(NULL);
        assert(reason==3&&audit_nack[0]==1U&&audit_done[0]==0U);
        assert(audit_error[0]==(failure==8U?CAN_ERR_SCAN_COMM:CAN_ERR_SCAN_HARDWARE));
        assert(audit_done[1]==1U&&!audit_nack[1]&&audit_data[1]>0U);
        assert(!ScanTask_IsFlashCommitBusy());escape_active=false;
        printf("PASS first-command startup transport fault%u -> NACK=%04X; next read DATA/DONE\n",failure,audit_error[0]);
    }
    run_pair(16U);assert(audit_error[0]==CAN_ERR_SCAN_COMM&&audit_done[1]==1U);
    run_pair(17U);assert(audit_error[0]==CAN_ERR_SCAN_HARDWARE&&audit_done[1]==1U);
    puts("PASS RTOS flags error -> E602 and malformed SSI length -> E601; subsequent read succeeds");

    reset_model();first_command=CAN_CMD_SCAN_GET_REVISION;fail_capture_delay=true;
    escape_active=true;int reason=setjmp(escape);
    if(!reason)app_start_task_scan_controller(NULL);
    assert(reason==3&&audit_delay_errors>0U&&done==2U&&diagnostics==1U&&nack==0U);
    assert(beats[APP_WDG_CLIENT_SCAN]>=audit_delay_errors);
    escape_active=false;
    printf("REPRODUCED U21-C: capture ignores %u RTOS delay errors, continues heartbeat, emits capture response/DONE; next read succeeds\n",audit_delay_errors);
    puts("Installed C, deterministic HAL/RTOS model; injected valid SSI packets and RTOS failures; BENCH NOT RUN.");
}
