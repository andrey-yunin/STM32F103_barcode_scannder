void CAN_SendDataFragmented(uint16_t cmd_code, const uint8_t *data,
		uint8_t total_len) {
	(void) cmd_code;

	uint8_t offset = 0;
	uint8_t seq = 0;

	while (offset < total_len) {
		CanTxFrame_t tx;
		memset(&tx, 0, sizeof(tx));

		tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL,
				CAN_MSG_TYPE_DATA_DONE_LOG, CAN_ADDR_CONDUCTOR,
				(uint32_t )AppConfig_GetPerformerID());
		tx.header.IDE = CAN_ID_EXT;
		tx.header.RTR = CAN_RTR_DATA;
		tx.header.DLC = 8;

		uint8_t chunk = total_len - offset;
		if (chunk > 6)
			chunk = 6;

		tx.data[0] = CAN_SUB_TYPE_DATA;
		tx.data[1] = seq;
		if (offset + chunk >= total_len) {
			tx.data[1] |= 0x80; // EOT на последнем фрагменте
		}

		for (uint8_t i = 0; i < chunk; i++) {
			tx.data[2 + i] = data[offset + i];
		}

		CAN_QueueTxFrame(&tx);

		offset += chunk;
		seq++;
	}
}
