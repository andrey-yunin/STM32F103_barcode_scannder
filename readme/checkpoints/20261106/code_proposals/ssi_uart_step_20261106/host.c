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

static unsigned mode, normal_abort_byte, stop_tx_count, stop_ack_at, rx_step;
static unsigned cts_low_count, aim_low_count, stop_complete_tick, stop_complete_count;
static uint32_t old_rts_until;
static bool peer_granted, reset_tx_complete, old_injected, stop_reply_sent;
static volatile bool reset_stop_active;
static bool arm_fail_after_latch, stall_stop_tx, late_abort_callback;
static void feed(uint8_t byte, uint32_t error);
static void feed_bytes(const uint8_t *p,unsigned n);

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
    if (AppSafety_IsResetPending() && ticks < old_rts_until)return GPIO_PIN_RESET;
    if (AppSafety_IsResetPending() && mode==2)return GPIO_PIN_SET;
    return (gpioa & GPIO_PIN_10) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}
static uint32_t HAL_GetTick(void) { return ticks; }
static uint32_t osKernelGetTickCount(void) { return ticks; }
static uint32_t osKernelGetTickFreq(void) { return freq; }
static osStatus_t osDelay(uint32_t n);
void AppWatchdog_Heartbeat(AppWatchdogClient_t c) { assert((unsigned)c<3); beats[c]++; }
static int arm_failure, notify_failure;
#define HAL_UART_STATE_READY 0x20U
#define HAL_UART_STATE_BUSY_RX 0x22U
#define HAL_UART_ERROR_NONE 0U
#define HAL_UART_ERROR_NE 2U
#define HAL_UART_ERROR_ORE 8U
static HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *h,uint8_t *p,uint16_t n) {
    assert(p && n==1 && irq);rx_arms++;
    if(arm_failure || (arm_fail_after_latch && AppSafety_IsResetPending()))return HAL_ERROR;
    h->RxState=HAL_UART_STATE_BUSY_RX;h->ErrorCode=0;return HAL_OK;
}
static HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *h,const uint8_t *p,uint16_t n) {
    (void)h;assert(p && n==1 && irq);
    if(AppSafety_IsResetPending()) {
        assert(reset_stop_active && !AppSafety_IsResetPreparationExpired());
        if(mode==3)return HAL_ERROR;
        stop_tx_count++;
    }
    if(!tx_index)tx_length=*p+2U;
    assert(tx_index<sizeof(tx_packet));tx_packet[tx_index++]=*p;
    if(tx_index==tx_length){
        if(tx_packet[1]==SSI_OP_STOP_DECODE){
            const uint8_t expected[]={4,0xE5,4,0,0xFF,0x13};
            assert(tx_length==6&&!memcmp(tx_packet,expected,6));stop_sends++;
        }
        if(tx_packet[1]==SSI_OP_CMD_ACK)sent_acks++;
    }
    tx_starts++;tx_pending=1;
    if(normal_abort_byte && !AppSafety_IsResetPending() && tx_index==normal_abort_byte)
        reset_on_restore=1;
    return HAL_OK;
}
static HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *h) {
    tx_aborts++;
    if(late_abort_callback)HAL_UART_TxCpltCallback(h);
    if(tx_abort_fail)return HAL_ERROR;
    tx_pending=0;return HAL_OK;
}
static HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *h) {
    rx_aborts++;
    if(rx_abort_fail)return HAL_ERROR;
    h->RxState=HAL_UART_STATE_READY;return HAL_OK;
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

static void Error_Handler(void) { __disable_irq(); AppSafety_AllOff(); host_terminal(1); }
static void feed(uint8_t byte, uint32_t error) {
    assert(huart3.RxState==HAL_UART_STATE_BUSY_RX);
    rx_byte=byte;huart3.ErrorCode=error;huart3.RxState=HAL_UART_STATE_READY;
    HAL_UART_RxCpltCallback(&huart3);
    if(error)HAL_UART_ErrorCallback(&huart3);
}
static void feed_bytes(const uint8_t *p,unsigned n) {for(unsigned i=0;i<n;i++)feed(p[i],0);}
static const uint8_t peer_ack[]={4,0xD0,0,0,0xFF,0x2C};
static void HAL_GPIO_WritePin(void *port,uint32_t pins,GPIO_PinState level) {
    uint32_t *value=port==GPIOA?&gpioa:&gpiob;
    bool previous=(*value & pins)!=0;
    if(level==GPIO_PIN_SET)*value|=pins;else *value&=~pins;
    if(port==GPIOB&&(pins&AIMWAKE_Pin)&&level==GPIO_PIN_RESET){
        assert(irq&&!AppSafety_IsResetPending());aim_low_count++;
    }
    if(port==GPIOA&&(pins&CTS_Pin)) {
        if(level==GPIO_PIN_RESET){
            assert(irq && (!AppSafety_IsResetPending() || reset_stop_active));cts_low_count++;
            peer_granted=true;
        } else if(!previous && peer_granted) {
            peer_granted=false;
            if(reset_stop_active && tx_index==6 && tx_packet[1]==SSI_OP_STOP_DECODE) {
                assert(read_active);stop_complete_count++;stop_complete_tick=ticks;
                if(mode==0 || mode==9){
                    stop_reply_sent=true;feed_bytes(peer_ack,sizeof(peer_ack));
                    if(mode==9)feed_bytes(peer_ack,sizeof(peer_ack));
                } else if(mode==6) {
                    uint8_t nak_packet[8],cause=0;
                    unsigned n=Ssi_Encode(SSI_OP_CMD_NAK,&cause,1,nak_packet);
                    nak_packet[2]=0;uint16_t c=Ssi_Checksum(nak_packet,n-2);nak_packet[n-2]=c>>8;nak_packet[n-1]=c;
                    feed_bytes(nak_packet,n);feed_bytes(peer_ack,sizeof(peer_ack));
                    stop_reply_sent=true;
                } else if(mode==7) {
                    feed_bytes(peer_ack,5);feed(peer_ack[5]^1,0);stop_reply_sent=true;
                } else if(mode==8) {
                    feed(peer_ack[0],HAL_UART_ERROR_ORE);stop_reply_sent=true;
                }
            }
            tx_index=0;
        }
    }
}
static osStatus_t osDelay(uint32_t n) {
    assert(!irq);delay_calls++;
    if(delay_fail_call==delay_calls)return osError;
    ticks+=n;
    if(reset_hook){void(*hook)(void)=reset_hook;reset_hook=NULL;hook();}
    if(tx_pending && (!AppSafety_IsResetPending() || !stall_stop_tx)) {
        if(reset_stop_active && !old_injected) {
            // Old response still on the wire while STOP owns CTS LOW.
            feed_bytes(peer_ack,sizeof(peer_ack));old_injected=true;
            assert(!holding_len && !read_active);
        }
        tx_pending=0;HAL_UART_TxCpltCallback(&huart3);
    }
    if(stop_complete_count && !stop_reply_sent && mode==10){
        if(rx_step<sizeof(peer_ack))feed(peer_ack[rx_step++],0);
        if(rx_step==sizeof(peer_ack))stop_reply_sent=true;
    }
    if(stop_complete_count && !stop_reply_sent && mode==11){
        ticks=app_safety_reset_started+3000U;feed_bytes(peer_ack,sizeof(peer_ack));stop_reply_sent=true;
    }
    if(prepare_on_delay && !scan_reset_prepared){prepare_on_delay=0;(void)Scan_SyncResetState();}
    return osOK;
}
static uint32_t osThreadFlagsWait(uint32_t f,uint32_t opt,uint32_t n) {
    (void)f;(void)opt;(void)n;assert(!irq);AppSafety_PrepareReset();return FLAG_SCAN_UART_RX;
}
static osStatus_t osMessageQueueGet(void *q,void *out,void *priority,uint32_t n) {
    (void)priority;(void)n;assert(q==scan_queueHandle&&!irq);
    if(queue_gets++)host_terminal(3);
    ScanTask_ReserveCommand();
    ScanResult_t command={.cmd_code=queued_command,.timeout_sec=4};
    memcpy(out,&command,sizeof(command));
    if(queue_mode)AppSafety_PrepareReset();
    return osOK;
}
static void reset_model(void) {
    reply_boundary_pending=false;
    scan_pending_commands=1U;
    irq=ticks=flags_word=0;freq=1000;gpioa=CTS_Pin;gpiob=TRIG_Pin|AIMWAKE_Pin;
    rx_arms=tx_starts=tx_aborts=rx_aborts=delay_calls=0;
    done=nack=fragments=diagnostics=flash_calls=ready_tick=delay_fail_call=0;
    queue_gets=flag_sets=flag_waits=ack_mode=ack_stage=sent_acks=tx_index=tx_length=stop_sends=0;
    reset_on_restore=prepare_on_delay=0;queue_mode=0;reset_hook=NULL;
    tx_abort_fail=rx_abort_fail=clear_fail=flash_fail=tx_pending=arm_failure=notify_failure=0;
    mode=normal_abort_byte=stop_tx_count=stop_ack_at=rx_step=0;
    cts_low_count=aim_low_count=stop_complete_tick=stop_complete_count=old_rts_until=0;
    peer_granted=reset_tx_complete=old_injected=stop_reply_sent=false;
    arm_fail_after_latch=stall_stop_tx=late_abort_callback=false;
    app_safety_reset_pending=scan_reset_prepared=false;app_safety_reset_started=0;
    active_start_sequence=0;tx_byte_done=tx_last_byte=false;
    read_active=read_error=rx_cancelled=reset_stop_active=false;holding_len=0;capture_active=false;
    memset(&capture,0,sizeof(capture));memset(&rx_diag,0,sizeof(rx_diag));memset(&start_tx,0,sizeof(start_tx));
    memset(beats,0,sizeof(beats));
    huart3.RxState=HAL_UART_STATE_READY;huart3.ErrorCode=0;
    ScanCtrl_Init();assert(ScanUart_Init());
}
static void run_sync(bool success) {
    escape_active=true;int reason=setjmp(escape);
    if(!reason){assert(Scan_SyncResetState());assert(success);}
    else assert(!success&&reason==1);
    escape_active=false;
    assert(scan_reset_prepared==success);
    assert(rx_cancelled&&!read_active&&!capture_active&&!holding_len);
    assert(!done&&!fragments&&!flash_calls);
    assert(gpioa&CTS_Pin);assert(gpiob&AIMWAKE_Pin);assert(gpiob&TRIG_Pin);
    assert(!aim_low_count);
}
static void expect_failure(unsigned m,const char *label) {
    reset_model();AppSafety_PrepareReset();mode=m;run_sync(false);
    printf("PASS %s\n",label);
}
int main(void) {
    reset_model();assert(ScanUart_BeginRead());feed_bytes(peer_ack,sizeof(peer_ack));
    AppSafety_PrepareReset();run_sync(true);
    assert(stop_sends==1 && stop_complete_count==1 && stop_tx_count==6 && old_injected && stop_reply_sent);
    unsigned arms=rx_arms;rx_byte=4;HAL_UART_RxCpltCallback(&huart3);HAL_UART_ErrorCallback(&huart3);
    assert(rx_arms==arms);assert(Scan_SyncResetState()&&stop_sends==1);
    puts("PASS idle/old buffered ACK + ACK during STOP: only new STOP response prepares; late callbacks cannot rearm");

    reset_model();AppSafety_PrepareReset();mode=1;run_sync(false);assert(ticks==3000 && old_injected && !stop_reply_sent);
    puts("PASS old ACK without STOP response never prepares; total timeout 3000 ms");
    expect_failure(2,"RTS never grants STOP: local bound remains, preparation fails");
    expect_failure(3,"HAL TX error: no prepared");
    expect_failure(6,"STOP NAK followed by old ACK: failure, no search for later ACK");
    expect_failure(7,"STOP ACK checksum error");
    expect_failure(8,"STOP RX ORE error");
    expect_failure(11,"ACK exactly at common deadline rejected");
    reset_model();AppSafety_PrepareReset();mode=10;run_sync(true);assert(rx_step==6);
    puts("PASS bytewise fragmented STOP ACK");
    reset_model();AppSafety_PrepareReset();mode=9;run_sync(true);
    puts("PASS STOP ACK first, old reply after it: one prepared");

    for(unsigned byte=1;byte<=6;byte++) {
        reset_model();normal_abort_byte=byte;late_abort_callback=true;
        const uint8_t start[]={4,0xE4,4,0,0xFF,0x14};
        assert(ScanUart_BeginRead());assert(!ScanUart_SendStart(start,6));
        assert(AppSafety_IsResetPending()&&!tx_pending&&!active_start_sequence);
        unsigned aims=aim_low_count;aim_low_count=0;normal_abort_byte=0;
        // Model old partial RX interval before a new handshake can start.
        old_rts_until=ticks+200;run_sync(true);
        assert(ticks>=old_rts_until&&stop_sends==1&&aim_low_count==0);(void)aims;
    }
    puts("PASS reset at each accepted START byte, abort/late TX callback and old partial-message interval");

    reset_model();ticks=UINT32_MAX-5;AppSafety_PrepareReset();run_sync(true);
    puts("PASS shared deadline across HAL tick wrap");
    reset_model();AppSafety_PrepareReset();ticks=2500;run_sync(true);assert(ticks<3000);
    reset_model();AppSafety_PrepareReset();ticks=2998;run_sync(false);assert(ticks<=3000);
    reset_model();AppSafety_PrepareReset();ticks=2999;AppSafety_PrepareReset();ticks=3000;
    assert(AppSafety_IsResetPreparationExpired());run_sync(false);
    puts("PASS task scheduling/old exchange consume shared budget; repeated latch cannot extend it");

    reset_model();AppSafety_PrepareReset();arm_fail_after_latch=true;
    feed(0,0);run_sync(false);
    reset_model();AppSafety_PrepareReset();clear_fail=1;run_sync(false);
    reset_model();AppSafety_PrepareReset();notify_failure=1;run_sync(false);
    reset_model();AppSafety_PrepareReset();rx_abort_fail=1;run_sync(false);
    reset_model();AppSafety_PrepareReset();delay_fail_call=1;run_sync(false);
    reset_model();AppSafety_PrepareReset();stall_stop_tx=true;run_sync(false);assert(tx_aborts==1);
    puts("PASS rearm/clear/notify/RX abort/osDelay/TX completion failures: no prepared");

    reset_model();const uint8_t start[]={4,0xE4,4,0,0xFF,0x14};
    assert(!ScanUart_SendStopForReset(start,6));AppSafety_PrepareReset();
    assert(!ScanUart_Wake()&&!ScanUart_Send(start,6)&&!ScanUart_SendStart(start,6)&&!ScanUart_StartRx());
    assert(!ScanUart_SendStopForReset(start,6));assert(!tx_starts&&!aim_low_count);
    puts("PASS normal Wake/Send/START/StartRx remain forbidden; shutdown rejects non-STOP packet");

    for(unsigned factory=0;factory<=1;factory++) {
        reset_model();prepare_on_delay=1;escape_active=true;int reason=setjmp(escape);
        if(!reason)test_service(factory?CAN_CMD_SRV_FACTORY_RESET:CAN_CMD_SRV_REBOOT,
                factory?SRV_MAGIC_FACTORY_RESET:SRV_MAGIC_REBOOT);
        assert(reason==2&&scan_reset_prepared&&done==1&&!nack&&stop_sends==1&&flash_calls==factory);
        escape_active=false;
        reset_model();prepare_on_delay=1;mode=1;escape_active=true;reason=setjmp(escape);
        if(!reason)test_service(factory?CAN_CMD_SRV_FACTORY_RESET:CAN_CMD_SRV_REBOOT,
                factory?SRV_MAGIC_FACTORY_RESET:SRV_MAGIC_REBOOT);
        assert(reason==1&&!scan_reset_prepared&&!done&&!flash_calls);escape_active=false;
        reset_model();test_service(factory?CAN_CMD_SRV_FACTORY_RESET:CAN_CMD_SRV_REBOOT,0);
        assert(!AppSafety_IsResetPending()&&nack==1&&last_nack==CAN_ERR_INVALID_KEY&&!stop_sends&&!flash_calls);
    }
    puts("PASS actual F002/F006 handlers: key first, STOP before service/Flash, failed prepare blocks Flash/DONE");
    reset_model();prepare_on_delay=1;flash_fail=1;escape_active=true;int reason=setjmp(escape);
    if(!reason)test_service(CAN_CMD_SRV_FACTORY_RESET,SRV_MAGIC_FACTORY_RESET);
    assert(reason==2&&scan_reset_prepared&&nack==1&&last_nack==CAN_ERR_FLASH_WRITE&&!done);escape_active=false;
    puts("PASS factory Flash failure keeps NACK/reset policy");

    for(unsigned q=0;q<=1;q++) {
        reset_model();queued_command=CAN_CMD_SCAN_REQUEST;queue_mode=q;escape_active=true;reason=setjmp(escape);
        if(!reason)app_start_task_scan_controller(NULL);
        assert(reason==3&&scan_reset_prepared&&stop_sends==1&&nack==1&&last_nack==CAN_ERR_DEVICE_BUSY&&!done&&!fragments);
        escape_active=false;
    }
    puts("PASS actual task queued/in-flight scan cancellation: DEVICE_BUSY, no DATA/DONE, one STOP");
    puts("HAL/RTOS deterministic model of candidate C only; no BENCH and no installed firmware change.");
}
