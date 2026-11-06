static void test_enqueue(uint16_t code) {
    ParsedCanCommand_t parsed={.cmd_code=code};
    switch(code) {
		case CAN_CMD_SCAN_REQUEST:
		case CAN_CMD_SCAN_GET_REVISION:
		case CAN_CMD_SCAN_BEEP:
		case CAN_CMD_SCAN_GET_DATA: {
			if ((parsed.cmd_code == CAN_CMD_SCAN_GET_REVISION
					|| parsed.cmd_code == CAN_CMD_SCAN_BEEP)
					&& (parsed.device_id || parsed.data[0] || parsed.data[1]
							|| parsed.data[2] || parsed.data[3]
							|| parsed.data[4])) {
				CAN_SendNackPublic(parsed.cmd_code, 0x0006);
				break;
			}
			ScanResult_t scan_cmd;
			memset(&scan_cmd, 0, sizeof(scan_cmd));
			scan_cmd.cmd_code = parsed.cmd_code;
			// timeout_sec из CAN-фрейма: byte2=lo, byte3=hi (через device_id и data[0])
			if (parsed.cmd_code == CAN_CMD_SCAN_REQUEST) {
				scan_cmd.timeout_sec = (uint16_t) (parsed.device_id)
						| ((uint16_t) parsed.data[0] << 8);
			}

			// --- Учёт начинается до enqueue и сохраняется после dequeue ---
			ScanTask_ReserveCommand();
			if (osMessageQueuePut(scan_queueHandle, &scan_cmd, 0, 0) != osOK) {
				// Отказ очереди оставляет владение у Dispatcher.
				ScanTask_ReleaseCommand();
				CAN_Diagnostics_RecordAppQueueOverflow();
				CAN_SendNackPublic(parsed.cmd_code, CAN_ERR_DEVICE_BUSY);
			}
			break;
		}

    default: assert(0);
    }
}
static void test_commit(void) {
    ParsedCanCommand_t parsed={.cmd_code=CAN_CMD_SRV_FLASH_COMMIT};
    switch(parsed.cmd_code) {
		case CAN_CMD_SRV_FLASH_COMMIT: {
			// --- Занятый домен не прерываем и не ждём ради Commit ---
			// Dispatcher — единственный producer scan_queue; после нулевого
			// снимка новая операция не появится до завершения этого handler.
			if (ScanTask_IsFlashCommitBusy()) {
				CAN_SendNackPublic(parsed.cmd_code, CAN_ERR_DEVICE_BUSY);
				break;
			}

			// --- COMMIT: сохранение RAM-конфигурации в Flash ---
			if (AppConfig_Commit()) {
				CAN_SendDone(parsed.cmd_code, 0);
			} else {
				CAN_SendNackPublic(parsed.cmd_code, CAN_ERR_FLASH_WRITE);
			}
			break;
		}

    default: assert(0);
    }
}
