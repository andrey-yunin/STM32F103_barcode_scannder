        bool rx_received = false;
        bool wait_error = false;
        bool hardware_error = false;
        bool checksum_seen = false;
        bool start_acked = false;
        uint32_t start_resends = 0U;

        // --- Packet storage and snapshot cursor belong to the Scanner task ---
        static uint8_t rx_packet[SSI_PACKET_MAX_LEN];
        uint16_t rx_len = 0U;
        const uint8_t *rx_data = NULL;

        // --- Recover SSI within the original reading deadline ---
        for (;;) {
            if (AppSafety_IsResetPending())
                break;
            uint32_t elapsed = osKernelGetTickCount() - started;
            if (elapsed >= timeout_ticks)
                break;

            uint16_t received_len;
            bool receive_error;
            const uint8_t *received = ScanUart_GetReceived(&received_len, &receive_error);
            if (receive_error) {
                wait_error = true;
                break;
            }

            if (received != NULL) {
                uint16_t cursor = 0U;
                while (cursor < received_len) {
                    if (AppSafety_IsResetPending()
                            || (uint32_t) (osKernelGetTickCount() - started) >= timeout_ticks)
                        break;

                    ScanRxStep_t step = Scan_CollectPacket(received, received_len,
                            &cursor, rx_packet, &rx_len);
                    if (step == SCAN_RX_BAD_LENGTH) {
                        wait_error = true;
                        break;
                    }
                    if (step == SCAN_RX_MORE)
                        break;

                    SsiMsg_t message;
                    ScanPacketKind_t kind = Scan_ClassifyPacket(rx_packet, rx_len, &message);
                    if (kind == SCAN_PACKET_BARCODE) {
                        rx_data = rx_packet;
                        rx_received = true;
                        break;
                    }

                    // --- Every handled non-barcode packet releases the assembler ---
                    rx_len = 0U;
                    if (kind == SCAN_PACKET_ACK) {
                        start_acked = true;
                    } else if (kind == SCAN_PACKET_REVISION) {
                        // A natural reply to another command is not this reading's result.
                    } else if (kind == SCAN_PACKET_RESEND) {
                        checksum_seen = true;
                        const uint8_t cause = SSI_NAK_RESEND;
                        ssi_len = Ssi_Encode(SSI_OP_CMD_NAK, &cause, 1U, ssi_packet);
                        if (!ScanUart_Wake() || !ScanUart_SendForReply(ssi_packet, ssi_len)) {
                            wait_error = true;
                            break;
                        }
                    } else if (kind == SCAN_PACKET_NAK) {
                        if (message.data[0] == SSI_NAK_RESEND) {
                            // Retransmit only a START explicitly rejected before its ACK.
                            // A later NAK must never restart an already accepted decode.
                            if (start_acked || start_resends >= SCAN_SSI_HOST_RESENDS) {
                                wait_error = true;
                                break;
                            }
                            ++start_resends;
                            ssi_len = Ssi_EncodeWithStatus(SSI_OP_START_DECODE,
                                    SSI_STATUS_RETRANSMIT, NULL, 0U, ssi_packet);
                            if (!ScanUart_Wake() || !ScanUart_SendStart(ssi_packet, ssi_len)) {
                                wait_error = true;
                                break;
                            }
                        } else {
                            hardware_error = true;
                            break;
                        }
                    } else {
                        hardware_error = true;
                        break;
                    }
                    AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
                }
                if (rx_received || wait_error || hardware_error)
                    break;
                AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
                continue;
            }

            // --- Recompute remaining time after task work, never renew the deadline ---
            elapsed = osKernelGetTickCount() - started;
            if (elapsed >= timeout_ticks)
                break;
            uint32_t remaining = timeout_ticks - elapsed;
            uint32_t wait_ticks = remaining < slice_ticks ? remaining : slice_ticks;
            uint32_t flags = osThreadFlagsWait(FLAG_SCAN_UART_RX, osFlagsWaitAny, wait_ticks);
            if (flags != osFlagsErrorTimeout && (flags & osFlagsError) != 0U) {
                wait_error = true;
                break;
            }
            AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
        }

        // --- Confirm one valid nonempty barcode before accepting its CAN result ---
        bool ack_error = false;
        if (rx_received && !wait_error && !hardware_error) {
            ssi_len = Ssi_Encode(SSI_OP_CMD_ACK, NULL, 0U, ssi_packet);
            ack_error = !ScanUart_Wake() || !ScanUart_Send(ssi_packet, ssi_len);
        }
