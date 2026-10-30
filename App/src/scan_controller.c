/*
 * scan_controller.c
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */

#include "scan_controller.h"
#include "scan_ssi.h"
#include <string.h>

// --- Локальные данные ---
static ScanState_t g_state = SCAN_STATE_IDLE;
static uint8_t g_shadow_buf[SHADOW_BUF_SIZE];
static uint8_t g_shadow_len = 0;
static ScanStats_t g_stats;

static uint8_t g_barcode_buf[SHADOW_BUF_SIZE];
static uint8_t g_barcode_len = 0;

void ScanCtrl_Init(void) {
	g_state = SCAN_STATE_IDLE;
	memset(&g_stats, 0, sizeof(g_stats));
	memset(g_shadow_buf, 0, SHADOW_BUF_SIZE);
	g_shadow_len = 0;
	g_barcode_len = 0;
}

bool ScanCtrl_StartScan(uint16_t timeout_sec) {
	(void) timeout_sec;
	// Only an active transaction is busy. ERROR and DATA_READY retain the
	// previous result until the next request; neither blocks a new scan.
	if (g_state == SCAN_STATE_SCANNING)
		return false;

	g_state = SCAN_STATE_SCANNING;
	g_barcode_len = 0;
	return true;
}

bool ScanCtrl_ProcessReceived(const uint8_t *data, uint16_t len) {
	if (g_state != SCAN_STATE_SCANNING)
		return false;

	SsiMsg_t msg;
	if (!Ssi_Decode(data, len, &msg)) {
		g_stats.ssi_decode_errors++;
		g_state = SCAN_STATE_ERROR;
		return false;
	}

	if (msg.opcode == SSI_OP_DECODE_DATA) {
		// --- Обязательный байт типа штрихкода ---
		if (msg.data_len < 1U) {
			g_stats.ssi_decode_errors++;
			g_state = SCAN_STATE_ERROR;
			return false;
		}

		// --- Строка начинается после Bar Code Type ---
		uint16_t barcode_len = msg.data_len - 1U;
		uint16_t copy_len =
				barcode_len < SHADOW_BUF_SIZE ? barcode_len : SHADOW_BUF_SIZE;

		memcpy(g_barcode_buf, &msg.data[1], copy_len);
		g_barcode_len = (uint8_t) copy_len;

		// --- Сохранение последнего успешно считанного штрихкода ---
		memcpy(g_shadow_buf, g_barcode_buf, g_barcode_len);
		g_shadow_len = g_barcode_len;

		g_stats.scan_success++;
		g_state = SCAN_STATE_DATA_READY;
		return true;
	}

	if (msg.opcode == SSI_OP_CMD_NAK) {
		g_stats.scan_timeouts++;
		g_state = SCAN_STATE_ERROR;
		return false;
	}

	// ACK (0xD0) — игнорируем, ждём DATA
	return false;
}

bool ScanCtrl_GetLastBarcode(uint8_t *out_buf, uint8_t *out_len) {
	if (out_buf == NULL || out_len == NULL)
		return false;
	if (g_shadow_len == 0)
		return false;

	memcpy(out_buf, g_shadow_buf, g_shadow_len);
	*out_len = g_shadow_len;
	return true;
}

bool ScanCtrl_HandleTimeout(void) {
	if (g_state != SCAN_STATE_SCANNING)
		return false;
	g_stats.scan_timeouts++;
	g_state = SCAN_STATE_ERROR;
	return true;
}

void ScanCtrl_HandleTransportError(void) {
	if (g_state == SCAN_STATE_SCANNING)
		g_state = SCAN_STATE_ERROR;
}

ScanState_t ScanCtrl_GetState(void) {
	return g_state;
}

void ScanCtrl_GetStats(ScanStats_t *out) {
	if (out != NULL)
		memcpy(out, &g_stats, sizeof(g_stats));
}

