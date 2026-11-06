/* Model of a protocol-conforming SSI peer; actual candidate task/driver above. */
enum {
    NORMAL, BAD_THEN_GOOD, ALL_BAD, NO_DATA, ONLY_REVISION, EMPTY_BARCODE,
    START_DENIED, START_RESEND, START_RESEND_EXHAUSTED, NAK_AFTER_ACK,
    STOP_DENIED, FLUSH_DENIED, STOP_SILENT, FLUSH_SILENT, STALE_STOP_ACK,
    STALE_FLUSH_ACK, COMBINED_BAD_GOOD, BYTEWISE, DUPLICATE,
    RESET_PREP_STOP, RESET_PREP_FLUSH, RESET_READING, RESET_NAK, RESET_ACK,
    NAK_TX_FAIL, ACK_TX_FAIL, FLAGS_ERROR, RX_ERROR, OVERFLOW, MALFORMED_LENGTH,
    PREP_RESEND, TICK_WRAP, DEADLINE_BAD, SLOW_START, PARTIAL,
    MAX_SCENARIO
};
static uint8_t wire[134];
static unsigned wire_used,wire_expected;
static bool wire_ready,old_queue,stop_confirmed,flush_confirmed,decode_pending,data_sent;
static unsigned starts,flushes,normal_stops,reset_stops,resends_sent,host_acks;
static unsigned start_attempts,stop_attempts,flush_attempts,peer_data_attempts;
static unsigned discarded_old_bytes,read_started,stream_index;
static uint8_t byte_stream[134];
static unsigned byte_stream_len;
static bool armed_boundary_fault;

// --- Construct a decoder packet independently of the production encoder ---
static unsigned peer_packet(uint8_t opcode,const uint8_t *body,unsigned n,uint8_t status,uint8_t *out) {
    out[0]=(uint8_t)(4U+n);out[1]=opcode;out[2]=0U;out[3]=status;
    if(n)memcpy(out+4,body,n);
    uint16_t sum=0U;for(unsigned i=0U;i<n+4U;i++)sum=(uint16_t)(sum+out[i]);
    sum=(uint16_t)(0U-sum);out[n+4U]=(uint8_t)(sum>>8);out[n+5U]=(uint8_t)sum;
    return n+6U;
}
static void peer_feed(const uint8_t *bytes,unsigned count) {
    for(unsigned i=0U;i<count;i++) {
        assert(huart3.RxState==HAL_UART_STATE_BUSY_RX);
        rx_byte=bytes[i];huart3.ErrorCode=0U;huart3.RxState=HAL_UART_STATE_READY;
        HAL_UART_RxCpltCallback(&huart3);
    }
}
static void peer_ack(void) { const uint8_t packet[]={4,0xD0,0,0,0xFF,0x2C};peer_feed(packet,sizeof(packet)); }
static void peer_nak(uint8_t cause) {uint8_t packet[7];peer_feed(packet,peer_packet(SSI_OP_CMD_NAK,&cause,1U,0U,packet));}
static void peer_barcode(bool corrupted,bool duplicate,bool old) {
    const uint8_t fresh[]={3U,'N','E','W','1'},old_data[]={3U,'O','L','D','9'};
    uint8_t packet[32];unsigned len=peer_packet(SSI_OP_DECODE_DATA,old?old_data:fresh,5U,duplicate?1U:0U,packet);
    if(corrupted)packet[len-1U]^=1U;
    peer_feed(packet,len);
}
static unsigned selected(void) {return command_index==0U?peer_scenario:NORMAL;}

// --- Peer response runs when the host releases CTS after the final UART TC ---
static void peer_reply(void) {
    unsigned mode=selected();uint8_t opcode=wire[1];uint8_t status=wire[3];
    assert(wire[2]==SSI_MSG_SRC_HOST);
    uint16_t check=0U;for(unsigned i=0U;i<wire_used-2U;i++)check=(uint16_t)(check+wire[i]);
    check=(uint16_t)(check+((uint16_t)wire[wire_used-2U]<<8)+wire[wire_used-1U]);
    assert(check==0U);
    if(AppSafety_IsResetPending()) {
        assert(opcode==SSI_OP_STOP_DECODE&&reset_stop_active);
        reset_stops++;peer_ack();return;
    }
    if(opcode==SSI_OP_SCAN_ENABLE){peer_ack();return;}
    if(opcode==SSI_OP_STOP_DECODE) {
        normal_stops++;
        if(!read_active)return; // Existing post-failure best-effort STOP.
        stop_attempts++;
        if(mode==STOP_DENIED){peer_nak(SSI_NAK_DENIED);return;}
        if(mode==STOP_SILENT||mode==STALE_STOP_ACK)return;
        if(mode==PREP_RESEND&&stop_attempts<3U) {
            assert(status==(stop_attempts==1U?0U:SSI_STATUS_RETRANSMIT));
            peer_nak(SSI_NAK_RESEND);return;
        }
        stop_confirmed=true;peer_ack();
        // A stopped module can still retain a prior decoded packet.
        if(old_queue)peer_barcode(false,false,true);
        return;
    }
    if(opcode==SSI_OP_FLUSH_QUEUE) {
        flushes++;flush_attempts++;
        assert(stop_confirmed);
        if(mode==FLUSH_DENIED){peer_nak(SSI_NAK_BAD_CONTEXT);return;}
        if(mode==FLUSH_SILENT||mode==STALE_FLUSH_ACK)return;
        old_queue=false;flush_confirmed=true;peer_ack();return;
    }
    if(opcode==SSI_OP_START_DECODE) {
        assert(stop_confirmed&&flush_confirmed&&!old_queue);
        starts++;start_attempts++;read_started=ticks;
        if(mode==START_DENIED){peer_nak(SSI_NAK_DENIED);return;}
        if(mode==START_RESEND_EXHAUSTED || (mode==START_RESEND&&start_attempts<3U)) {
            assert(status==(start_attempts==1U?0U:SSI_STATUS_RETRANSMIT));
            peer_nak(SSI_NAK_RESEND);return;
        }
        if(start_attempts>1U)assert(status==SSI_STATUS_RETRANSMIT);
        decode_pending=true;data_sent=false;
        if(mode==NAK_AFTER_ACK) {peer_ack();peer_nak(SSI_NAK_RESEND);return;}
        if(mode==COMBINED_BAD_GOOD) {
            peer_ack();peer_barcode(true,false,false);peer_barcode(false,true,false);data_sent=true;return;
        }
        peer_ack();return;
    }
    if(opcode==SSI_OP_CMD_NAK) {
        assert(wire[0]==5U&&wire[4]==SSI_NAK_RESEND);resends_sent++;
        assert(decode_pending);
        if(mode==DEADLINE_BAD){ticks+=4000U;return;}
        if(peer_data_attempts<3U) {
            peer_data_attempts++;
            peer_barcode(mode==ALL_BAD,true,false);
        }
        return;
    }
    if(opcode==SSI_OP_CMD_ACK) {
        host_acks++;assert(decode_pending);
        if(mode==DUPLICATE)peer_barcode(false,true,false);
        decode_pending=false;return;
    }
    assert(0);
}

static void peer_write(void *port,uint32_t pins,GPIO_PinState level) {
    uint32_t *bits=port==GPIOA?&gpioa:&gpiob;bool before=(*bits&pins)!=0U;
    if(level==GPIO_PIN_SET)*bits|=pins;else *bits&=~pins;
    if(port==GPIOA&&(pins&CTS_Pin)&&level==GPIO_PIN_SET&&!before) {
        if(wire_ready) {wire_ready=false;peer_reply();}
        wire_used=wire_expected=0U;
    }
}
static void HAL_GPIO_WritePin(void *port,uint32_t pins,GPIO_PinState level) {peer_write(port,pins,level);}
static HAL_StatusTypeDef peer_byte(UART_HandleTypeDef *h,const uint8_t *bytes,uint16_t n) {
    (void)h;assert(irq&&bytes&&n==1U);
    assert(!AppSafety_IsResetPending()||reset_stop_active);
    unsigned mode=selected();
    if(wire_used==1U && ((*bytes==SSI_OP_CMD_NAK&&mode==NAK_TX_FAIL)
            || (*bytes==SSI_OP_CMD_ACK&&mode==ACK_TX_FAIL)))return HAL_BUSY;
    if(wire_used==0U)wire_expected=*bytes+2U;
    assert(wire_used<sizeof(wire));wire[wire_used++]=*bytes;
    tx_starts++;tx_pending=1;
    if(!AppSafety_IsResetPending() && wire_used==2U) {
        if((mode==RESET_PREP_STOP&&*bytes==SSI_OP_STOP_DECODE)
                ||(mode==RESET_PREP_FLUSH&&*bytes==SSI_OP_FLUSH_QUEUE)
                ||(mode==RESET_NAK&&*bytes==SSI_OP_CMD_NAK)
                ||(mode==RESET_ACK&&*bytes==SSI_OP_CMD_ACK))reset_on_restore=1;
    }
    if(wire_used==wire_expected)wire_ready=true;
    return HAL_OK;
}
static void peer_progress(uint32_t n) {
    ticks+=n;
    if(tx_pending) {
        unsigned mode=selected();
        if(wire_ready&&reply_boundary_pending&&!armed_boundary_fault
                && ((mode==STALE_STOP_ACK&&wire[1]==SSI_OP_STOP_DECODE)
                    ||(mode==STALE_FLUSH_ACK&&wire[1]==SSI_OP_FLUSH_QUEUE))) {
            peer_ack();discarded_old_bytes+=6U;armed_boundary_fault=true;
        }
        tx_pending=0;HAL_UART_TxCpltCallback(&huart3);
    }
}
static uint32_t osThreadFlagsWait(uint32_t f,uint32_t opt,uint32_t n) {
    (void)f;(void)opt;assert(!irq);
    unsigned mode=selected();
    if(mode==RESET_READING){AppSafety_PrepareReset();return FLAG_SCAN_UART_RX;}
    if(mode==FLAGS_ERROR)return osFlagsError;
    if(mode==NO_DATA||mode==ONLY_REVISION||mode==PARTIAL) {
        if(!data_sent) {
            data_sent=true;
            if(mode==ONLY_REVISION){uint8_t packet[16];peer_feed(packet,peer_packet(SSI_OP_REPLY_REVISION,(const uint8_t *)"v1",2U,0U,packet));return FLAG_SCAN_UART_RX;}
            if(mode==PARTIAL){const uint8_t first=9U;peer_feed(&first,1U);return FLAG_SCAN_UART_RX;}
        }
        ticks+=n;return osFlagsErrorTimeout;
    }
    if(mode==RX_ERROR) {
        huart3.ErrorCode=HAL_UART_ERROR_ORE;huart3.RxState=HAL_UART_STATE_READY;
        HAL_UART_ErrorCallback(&huart3);return FLAG_SCAN_UART_RX;
    }
    if(mode==OVERFLOW) {
        for(unsigned i=0U;i<SCAN_UART_PENDING_SIZE+1U;i++){uint8_t value=4U;peer_feed(&value,1U);}return FLAG_SCAN_UART_RX;
    }
    if(mode==MALFORMED_LENGTH){const uint8_t value=3U;peer_feed(&value,1U);return FLAG_SCAN_UART_RX;}
    if(mode==BYTEWISE) {
        if(!byte_stream_len){const uint8_t body[]={3U,'N','E','W','1'};byte_stream_len=peer_packet(SSI_OP_DECODE_DATA,body,sizeof(body),0U,byte_stream);}
        if(stream_index<byte_stream_len){peer_feed(byte_stream+stream_index++,1U);return FLAG_SCAN_UART_RX;}
    }
    if(decode_pending&&!data_sent) {
        data_sent=true;peer_data_attempts=1U;
        if(mode==EMPTY_BARCODE){uint8_t packet[8],body=3U;peer_feed(packet,peer_packet(SSI_OP_DECODE_DATA,&body,1U,0U,packet));}
        else if(mode==DEADLINE_BAD){peer_barcode(true,false,false);}
        else peer_barcode(mode==BAD_THEN_GOOD||mode==ALL_BAD||mode==RESET_NAK||mode==NAK_TX_FAIL,false,false);
        return FLAG_SCAN_UART_RX;
    }
    ticks+=n;return osFlagsErrorTimeout;
}
static osStatus_t osMessageQueueGet(void *q,void *out,void *priority,uint32_t n) {
    (void)priority;(void)n;assert(q==scan_queueHandle&&!irq);
    if(queue_gets>=command_limit)host_terminal(3);
    if(queue_gets>0U)assert(!ScanTask_IsFlashCommitBusy());
    command_index=queue_gets++;
    ScanTask_ReserveCommand();
    ScanResult_t command={.cmd_code=CAN_CMD_SCAN_REQUEST,.timeout_sec=4U};
    memcpy(out,&command,sizeof(command));
    // A previous decoded packet may still be queued at the start of any read.
    old_queue=true;stop_confirmed=flush_confirmed=decode_pending=data_sent=false;
    start_attempts=stop_attempts=flush_attempts=peer_data_attempts=0U;
    stream_index=byte_stream_len=0U;armed_boundary_fault=false;
    return osOK;
}
static void Error_Handler(void) {__disable_irq();AppSafety_AllOff();host_terminal(1);}
static void reset_model(unsigned mode,unsigned commands) {
    memset(command_done,0,sizeof(command_done));memset(command_nack,0,sizeof(command_nack));
    memset(command_data,0,sizeof(command_data));memset(command_error,0,sizeof(command_error));
    memset(command_barcode,0,sizeof(command_barcode));memset(command_barcode_len,0,sizeof(command_barcode_len));
    irq=ticks=0U;gpioa=CTS_Pin;gpiob=TRIG_Pin|AIMWAKE_Pin;flags_word=0U;
    peer_scenario=mode;command_limit=commands;command_index=0U;
    queue_gets=done=nack=fragments=diagnostics=delay_calls=tx_starts=0U;
    delay_fail_call=arm_failure=notify_failure=clear_fail=tx_abort_fail=rx_abort_fail=0;
    reset_on_restore=prepare_on_delay=tx_pending=0;reset_hook=NULL;
    wire_used=wire_expected=0U;wire_ready=false;
    starts=flushes=normal_stops=reset_stops=resends_sent=host_acks=discarded_old_bytes=0U;
    old_queue=stop_confirmed=flush_confirmed=decode_pending=data_sent=false;
    peer_data_attempts=stream_index=byte_stream_len=0U;
    app_safety_reset_pending=scan_reset_prepared=false;app_safety_reset_started=0U;
    scan_pending_commands=1U;
    read_active=read_error=rx_cancelled=reset_stop_active=reply_boundary_pending=false;
    holding_len=0U;capture_active=false;
    active_start_sequence=0U;tx_byte_done=tx_last_byte=false;
    memset(&capture,0,sizeof(capture));memset(&rx_diag,0,sizeof(rx_diag));memset(&start_tx,0,sizeof(start_tx));
    huart3.RxState=HAL_UART_STATE_READY;huart3.ErrorCode=0U;
    if(mode==TICK_WRAP)ticks=UINT32_MAX-40U;
}
static void run_model(unsigned mode,unsigned commands) {
    reset_model(mode,commands);escape_active=true;
    int reason=setjmp(escape);
    if(!reason)app_start_task_scan_controller(NULL);
    if(reason!=3)fprintf(stderr,"scenario=%u fatal=%d ticks=%u wire=%u/%u opcode=%02x\n",mode,reason,ticks,wire_used,wire_expected,wire[1]);
    assert(reason==3);escape_active=false;
    for(unsigned i=0U;i<commands;i++)assert(command_done[i]+command_nack[i]==1U);
    assert(!ScanTask_IsFlashCommitBusy());
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    const unsigned good[]={NORMAL,BAD_THEN_GOOD,START_RESEND,COMBINED_BAD_GOOD,BYTEWISE,DUPLICATE,PREP_RESEND,TICK_WRAP};
    for(unsigned i=0U;i<sizeof(good)/sizeof(good[0]);i++) {
        run_model(good[i],2U);
        for(unsigned c=0U;c<2U;c++) {
            assert(command_done[c]==1U&&!command_nack[c]&&command_data[c]==1U);
            assert(command_barcode_len[c]==4U&&!memcmp(command_barcode[c],"NEW1",4U));
        }
        assert(!old_queue);
        if(good[i]==BAD_THEN_GOOD||good[i]==COMBINED_BAD_GOOD)assert(resends_sent==1U);
        if(good[i]==START_RESEND)assert(starts==4U);
        printf("PASS recovery scenario%u: two reads, confirmed preparation, NEW1 once per read\n",good[i]);
    }
    const unsigned errors[]={ALL_BAD,NO_DATA,ONLY_REVISION,EMPTY_BARCODE,START_DENIED,
        START_RESEND_EXHAUSTED,NAK_AFTER_ACK,STOP_DENIED,FLUSH_DENIED,STOP_SILENT,
        FLUSH_SILENT,STALE_STOP_ACK,STALE_FLUSH_ACK,NAK_TX_FAIL,ACK_TX_FAIL,
        FLAGS_ERROR,RX_ERROR,OVERFLOW,MALFORMED_LENGTH,DEADLINE_BAD,PARTIAL};
    for(unsigned i=0U;i<sizeof(errors)/sizeof(errors[0]);i++) {
        unsigned mode=errors[i];run_model(mode,2U);
        if(command_nack[0]!=1U||command_done[0]||command_data[0])fprintf(stderr,"failure scenario=%u done=%u nack=%u data=%u\n",mode,command_done[0],command_nack[0],command_data[0]);
        assert(command_nack[0]==1U&&!command_done[0]&&!command_data[0]);
        assert(command_done[1]==1U&&!command_nack[1]&&command_data[1]==1U);
        uint16_t expected=(mode==NO_DATA||mode==ONLY_REVISION||mode==PARTIAL)?CAN_ERR_SCAN_TIMEOUT:
            ((mode==EMPTY_BARCODE||mode==START_DENIED||mode==STOP_DENIED||mode==FLUSH_DENIED)?CAN_ERR_SCAN_HARDWARE:CAN_ERR_SCAN_COMM);
        if(command_error[0]!=expected)fprintf(stderr,"scenario%u expected=%04x actual=%04x\n",mode,expected,command_error[0]);
        assert(command_error[0]==expected);
        if(mode==STALE_STOP_ACK||mode==STALE_FLUSH_ACK)assert(discarded_old_bytes==6U);
        printf("PASS failure scenario%u: NACK=%04X once, no DATA; next isolated read succeeds\n",mode,expected);
    }
    const unsigned cancellations[]={RESET_PREP_STOP,RESET_PREP_FLUSH,RESET_READING,RESET_NAK,RESET_ACK};
    for(unsigned i=0U;i<sizeof(cancellations)/sizeof(cancellations[0]);i++) {
        run_model(cancellations[i],1U);
        assert(command_nack[0]==1U&&command_error[0]==CAN_ERR_DEVICE_BUSY&&!command_data[0]);
        assert(scan_reset_prepared&&rx_cancelled&&reset_stops==1U);
        printf("PASS reset scenario%u: STOP-only shutdown, DEVICE_BUSY, no barcode result\n",cancellations[i]);
    }
    puts("34 operation scenarios / 63 CAN commands, protocol-conforming peer model; no BENCH or actual FLUSH support claim.");
}
