typedef struct {
    uint16_t cmd_code;
    uint8_t  device_id;
    uint8_t  data[5];
    uint8_t  data_len;
} ParsedCanCommand_t;

typedef struct {
    uint16_t cmd_code;      // 0x0601 (SCAN_REQUEST) or 0x0602 (SCAN_GET_DATA)
    uint16_t timeout_sec;   // timeout in seconds (only for SCAN_REQUEST)
    uint8_t  barcode[128];  // result barcode
    uint8_t  barcode_len;
} ScanResult_t;

#define SCAN_QUEUE_LEN              4

#define FLAG_CAN_RX                 0x01

#define FLAG_CAN_TX                 0x02

#define FLAG_SCAN_UART_RX   0x04

#define STBY_Pin GPIO_PIN_1

#define STBY_GPIO_Port GPIOA

#define LED_CAN_Pin GPIO_PIN_2

#define LED_CAN_GPIO_Port GPIOA

#define LED_WORK_Pin GPIO_PIN_3

#define LED_WORK_GPIO_Port GPIOA

#define TRIG_Pin GPIO_PIN_12

#define TRIG_GPIO_Port GPIOB

#define AIMWAKE_Pin GPIO_PIN_13

#define AIMWAKE_GPIO_Port GPIOB

#define DLED_Pin GPIO_PIN_14

#define DLED_GPIO_Port GPIOB

#define BPR_Pin GPIO_PIN_15

#define BPR_GPIO_Port GPIOB

#define PWRDWN_Pin GPIO_PIN_8

#define PWRDWN_GPIO_Port GPIOA

#define RTS_Pin GPIO_PIN_9

#define RTS_GPIO_Port GPIOA

#define CTS_Pin GPIO_PIN_10

#define CTS_GPIO_Port GPIOA

/*
 * app_safety.c
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */



// --- Постоянный запрет активации до перезапуска MCU ---
static volatile bool app_safety_reset_pending = false;

/*
 * Отпускает управляющие входы сканера и выключает индикацию платы.
 * Допускает вызов до MX_GPIO_Init(), из startup и аварийных обработчиков.
 * Не использует RTOS, UART, CAN или Flash.
 *
 * Не отменяет UART/DMA и не устанавливает запрет повторной активации.
 * HIGH на TRIG не гарантирует остановку сканирования, запущенного SSI.
 */
void AppSafety_AllOff(void) {
	GPIO_InitTypeDef gpio = { 0 };

	// --- Подготовка портов для раннего и аварийного вызова ---
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	// --- Неактивные уровни до переключения пинов в выходной режим ---
	HAL_GPIO_WritePin(CTS_GPIO_Port, CTS_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(GPIOB, TRIG_Pin | AIMWAKE_Pin, GPIO_PIN_SET);

	/*
	 * Сохраняем текущую политику платы:
	 * CAN-трансивер активен, обе LED выключены.
	 */
	HAL_GPIO_WritePin(GPIOA,
	STBY_Pin | LED_CAN_Pin | LED_WORK_Pin, GPIO_PIN_RESET);

	// --- Настройка только выходов; входные сигналы модуля не меняем ---
	gpio.Mode = GPIO_MODE_OUTPUT_PP;
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_LOW;

	gpio.Pin = STBY_Pin | LED_CAN_Pin | LED_WORK_Pin | CTS_Pin;
	HAL_GPIO_Init(GPIOA, &gpio);

	gpio.Pin = TRIG_Pin | AIMWAKE_Pin;
	HAL_GPIO_Init(GPIOB, &gpio);
}

// --- Чтение состояния подготовки к reset ---
bool AppSafety_IsResetPending(void) {
	return app_safety_reset_pending;
}

// --- Атомарная фиксация запрета и аппаратный safe-state ---
// Без Flash, CAN и ожиданий RTOS. Состояние IRQ вызывающего сохраняется.
// Отмена UART/DMA подключается отдельным блоком.
void AppSafety_PrepareReset(void) {
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	app_safety_reset_pending = true;
	AppSafety_AllOff();
	__DSB();

	__set_PRIMASK(saved_primask);
}


/*
 * scan_uart_driver.c
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */









extern UART_HandleTypeDef huart3;

// --- DMA_NORMAL receive buffer + domain holding buffer ---
static uint8_t dma_buffer[SCAN_UART_BUF_SIZE];
static uint8_t holding_buf[SCAN_UART_BUF_SIZE];
static volatile uint16_t holding_len = 0;
static ScanUartCapture_t capture;
static volatile bool capture_active;
static uint16_t rx_prefix;

// --- Capture ownership changes only in scan-task context with IRQs masked ---
void ScanUart_BeginCapture(void) {
	uint32_t irq_state = __get_PRIMASK();
	__disable_irq();
	memset(&capture, 0, sizeof(capture));
	capture_active = true;
	__set_PRIMASK(irq_state);
}

void ScanUart_EndCapture(ScanUartCapture_t *out) {
	uint32_t irq_state = __get_PRIMASK();
	__disable_irq();
	capture_active = false;
	if (out != NULL)
		*out = capture;
	__set_PRIMASK(irq_state);
}

// --- HT and subsequent IDLE/TC share a prefix; retain each byte only once ---
static void ScanUart_CaptureRx(uint16_t size, bool half_transfer) {
	uint16_t offset = size >= rx_prefix ? rx_prefix : 0U;
	if (capture_active) {
		uint16_t count = size - offset;
		uint16_t room = SCAN_UART_CAPTURE_SIZE - capture.length;
		if (count > room) {
			count = room;
			capture.overflow = true;
		}
		memcpy(capture.bytes + capture.length, dma_buffer + offset, count);
		capture.length += count;
		capture.errors |= huart3.ErrorCode;
	}
	rx_prefix = half_transfer ? size : 0U;
}

// --- Diagnostic storage is separate from the SSI/domain holding buffer ---
static ScanUartDiagnostics_t rx_diag;
static uint8_t rx_arm_status = 0xFFU;
static uint8_t rx_event_type = 0xFFU;

// --- One retained START attempt and ownership of the in-flight TX completion ---
static ScanUartTxDiagnostics_t start_tx = { .state = 0xFFU };
static uint32_t active_start_sequence;
static volatile bool tx_byte_done;
static bool tx_last_byte;

// --- Read-only snapshot; callbacks cannot change the copy while IRQs are masked ---
void ScanUart_GetTxDiagnostics(ScanUartTxDiagnostics_t *out) {
	if (out == NULL)
		return;
	uint32_t irq_state = __get_PRIMASK();
	__disable_irq();
	*out = start_tx;
	__set_PRIMASK(irq_state);
}

// --- HAL completion belongs only to a successfully accepted tracked transfer ---
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
	if (huart->Instance != USART3)
		return;
	if (active_start_sequence != 0U
			&& active_start_sequence == start_tx.sequence) {
		start_tx.complete_tick = HAL_GetTick();
		start_tx.state |= 0x100U;
		start_tx.error_mask |= huart->ErrorCode;
	}
	active_start_sequence = 0U;
	tx_byte_done = true;
	if (tx_last_byte)
		HAL_GPIO_WritePin(CTS_GPIO_Port, CTS_Pin, GPIO_PIN_SET);
}

// --- Read-only task snapshot: DMA never writes rx_diag ---
void ScanUart_GetDiagnostics(ScanUartDiagnostics_t *out) {
	if (out == NULL)
		return;
	uint32_t irq_state = __get_PRIMASK();
	__disable_irq();
	*out = rx_diag;
	out->baud = huart3.Init.BaudRate;
	out->state = (uint32_t) rx_arm_status | ((uint32_t) huart3.RxState << 8)
			| ((uint32_t) rx_event_type << 16);
	__set_PRIMASK(irq_state);
}

// --- Capture the stable prefix reported by HAL, including half-transfer ---
// DMA_NORMAL: HT prefix remains stable while DMA writes its second half.
// Do not interpret this prefix as a validated SSI packet.
static void ScanUart_RecordRx(uint16_t size) {
	++rx_diag.sequence;
	if (rx_diag.sequence == 0U)
		++rx_diag.sequence;
	rx_diag.tick_ms = HAL_GetTick();
	rx_diag.length = size;
	rx_event_type = (uint8_t) HAL_UARTEx_GetRxEventType(&huart3);
	memset(rx_diag.bytes, 0, sizeof(rx_diag.bytes));
	memcpy(rx_diag.bytes, dma_buffer, size);
	rx_diag.error_mask |= huart3.ErrorCode;
}

// --- Preserve UART errors; this observer does not change recovery policy ---
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
	if (huart->Instance != USART3)
		return;
	rx_diag.error_mask |= huart->ErrorCode;
	if (capture_active)
		capture.errors |= huart->ErrorCode;
	if (active_start_sequence != 0U
			&& active_start_sequence == start_tx.sequence)
		start_tx.error_mask |= huart->ErrorCode;
	if (rx_diag.error_count != UINT32_MAX)
		++rx_diag.error_count;
}

/*
 * Отменяет приём после установки reset-latch.
 * Latch и RX-gate исключают повторный запуск из callback.
 * Вызывается задачей-владельцем; CAN-ответов не формирует.
 */
bool ScanUart_CancelRx(void) {
	// --- Закрытие накопления результата ---
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	capture_active = false;
	__DMB();

	__set_PRIMASK(saved_primask);

	// --- Остановка UART RX DMA ---
	HAL_StatusTypeDef status = HAL_UART_AbortReceive(&huart3);

	// --- Аннулирование данных отменённой операции ---
	// После latch RX callback эти поля больше не обновляет.
	holding_len = 0U;
	rx_prefix = 0U;

	// --- Результат аппаратной остановки ---
	if (status != HAL_OK)
		return false;

	return true;
}

bool ScanUart_Init(void) {
	holding_len = 0;
	return ScanUart_StartRx();
}

/*
 * Запускает RX DMA только до подготовки к reset.
 * Проверка latch и запуск неделимы, включая вызов из RX callback.
 * При запрете диагностика последнего HAL-вызова сохраняется.
 */
bool ScanUart_StartRx(void) {
	// --- Атомарная проверка запрета RX ---
	uint32_t irq_state = __get_PRIMASK();
	__disable_irq();

	if (AppSafety_IsResetPending()) {
		__set_PRIMASK(irq_state);
		return false;
	}

	// --- Существующий запуск и запись фактического HAL-результата ---
	HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle_DMA(&huart3, dma_buffer,
	SCAN_UART_BUF_SIZE);

	rx_arm_status = (uint8_t) status;

	// --- Завершение критической секции ---
	__DSB();
	__set_PRIMASK(irq_state);

	return status == HAL_OK;
}

/*
 * Ожидает уровень RTS в пределах существующего таймаута.
 * Подготовка к reset прекращает ожидание с результатом false.
 * Атомарный запрет последующей активации обеспечивают вызывающие gates.
 */
static bool ScanUart_WaitRts(GPIO_PinState level, uint32_t timeout_ms) {
	// --- Начало существующего срока ожидания ---
	uint32_t started = HAL_GetTick();

	// --- Ожидание уровня с проверкой отмены ---
	while (HAL_GPIO_ReadPin(RTS_GPIO_Port, RTS_Pin) != level) {
		if (AppSafety_IsResetPending())
			return false;

		if ((uint32_t) (HAL_GetTick() - started) >= timeout_ms)
			return false;

		AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);

		if (osDelay(1U) != osOK)
			return false;
	}

	// --- Уровень RTS не означает успех отменённой операции ---
	return !AppSafety_IsResetPending();
}

/*
 * Выполняет аппаратный wake только до подготовки к reset.
 * Проверка latch и активация AIMWAKE неделимы.
 * Отпускание линии разрешено всегда; задержки выполняются с
 * восстановленным состоянием IRQ.
 */
bool ScanUart_Wake(void) {
	// --- Атомарная проверка запрета и активация ---
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	if (AppSafety_IsResetPending()) {
		__set_PRIMASK(saved_primask);
		return false;
	}

	HAL_GPIO_WritePin(AIMWAKE_GPIO_Port, AIMWAKE_Pin, GPIO_PIN_RESET);

	__DSB();
	__set_PRIMASK(saved_primask);

	// --- Существующий импульс wake и безусловное отпускание линии ---
	uint32_t ticks = (osKernelGetTickFreq() + 999U) / 1000U + 1U;
	bool ok = osDelay(ticks) == osOK;

	HAL_GPIO_WritePin(AIMWAKE_GPIO_Port, AIMWAKE_Pin, GPIO_PIN_SET);

	// --- Существующее ожидание готовности после wake ---
	ticks = (osKernelGetTickFreq() + 99U) / 100U + 1U;
	bool settled = osDelay(ticks) == osOK;

	// Reset во время ожидания исключает успешный результат wake.
	return settled && ok && !AppSafety_IsResetPending();
}

/*
 * Вызывается только задачей-владельцем TX.
 * Закрывает принадлежность START completion до остановки DMA.
 * При ошибке остановки не возвращает управление владельцу буфера:
 * фактический Error_Handler терминален.
 */
static void ScanUart_AbortTx(void) {
	// --- Закрытие принадлежности позднего START completion ---
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	active_start_sequence = 0U;
	__DMB();

	__set_PRIMASK(saved_primask);

	// --- Проверяемая остановка UART TX DMA ---
	if (HAL_UART_AbortTransmit(&huart3) != HAL_OK) {
		Error_Handler();
	}

	// --- Сброс локальных признаков после остановки ---
	tx_byte_done = false;
	tx_last_byte = false;

	// --- Отпускание запроса передачи ---
	HAL_GPIO_WritePin(CTS_GPIO_Port, CTS_Pin, GPIO_PIN_SET);
}

// --- One SSI transaction: request, grant per byte, completed TX, release ---
// One-byte DMA transfers retain the existing HAL completion diagnostics while
// checking decoder permission before every character. Only the last callback
// marks the whole START complete and releases CTS immediately after UART TC.
static bool ScanUart_SendInternal(const uint8_t *data, uint16_t len,
bool track_start) {
	if (data == NULL || len == 0U || len > 134U)
		return false;
	// The preceding transaction must have released its grant before a new one.
	if (!ScanUart_WaitRts(GPIO_PIN_SET, 2000U))
		return false;

	// --- Атомарная проверка запрета и запрос передачи ---
	// PrepareReset не может вклиниться между проверкой и CTS LOW.
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	if (AppSafety_IsResetPending()) {
		__set_PRIMASK(saved_primask);
		return false;
	}

	HAL_GPIO_WritePin(CTS_GPIO_Port, CTS_Pin, GPIO_PIN_RESET);

	__DSB();
	__set_PRIMASK(saved_primask);

	// --- Существующая передача SSI ---
	bool success = false;
	for (uint16_t i = 0; i < len; ++i) {
		// Bound character gaps below the default 200 ms intercharacter timeout.
		if (!ScanUart_WaitRts(GPIO_PIN_RESET, i == 0U ? 2000U : 100U))
			break;
		// --- Атомарный запрет запуска следующего DMA-байта ---
		uint32_t irq_state = __get_PRIMASK();
		__disable_irq();

		if (AppSafety_IsResetPending()) {
			__set_PRIMASK(irq_state);
			break;
		}

		tx_byte_done = false;
		tx_last_byte = i + 1U == len;
		if (track_start && i == 0U) {
			uint32_t next = start_tx.sequence + 1U;
			if (next == 0U)
				next = 1U;
			memset(&start_tx, 0, sizeof(start_tx));
			start_tx.sequence = next;
			start_tx.start_tick = HAL_GetTick();
			start_tx.requested_len = len;
			start_tx.captured_len =
					len < SCAN_UART_TX_CAPTURE_SIZE ?
							len : SCAN_UART_TX_CAPTURE_SIZE;
			start_tx.baud = huart3.Init.BaudRate;
			memcpy(start_tx.bytes, data, start_tx.captured_len);
		}
		HAL_StatusTypeDef status = HAL_UART_Transmit_DMA(&huart3,
				(uint8_t*) &data[i], 1U);
		active_start_sequence =
				status == HAL_OK && track_start && tx_last_byte ?
						start_tx.sequence : 0U;
		if (track_start) {
			start_tx.state = (uint32_t) status
					| ((uint32_t) huart3.gState << 16);
			start_tx.error_mask |= huart3.ErrorCode;
		}
		// --- Завершение критической секции запуска DMA ---
		__DSB();
		__set_PRIMASK(irq_state);

		if (status != HAL_OK)
			break;
		// --- Ожидание завершения байта с проверкой отмены ---
		uint32_t started = HAL_GetTick();

		while (!tx_byte_done && (uint32_t) (HAL_GetTick() - started) < 100U) {
			if (AppSafety_IsResetPending())
				break;

			AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);

			if (osDelay(1U) != osOK)
				break;
		}

		// --- Отмена имеет приоритет над полученным completion ---
		// Буфер возвращается вызывающему только после остановки TX.
		if (!tx_byte_done || AppSafety_IsResetPending()) {
			ScanUart_AbortTx();
			break;
		}

		success = tx_last_byte;
	}
	HAL_GPIO_WritePin(CTS_GPIO_Port, CTS_Pin, GPIO_PIN_SET);
	if (!success)
		return false;
	return ScanUart_WaitRts(GPIO_PIN_SET, 2000U);
}

// --- Normal commands never replace the retained START snapshot ---
bool ScanUart_Send(const uint8_t *data, uint16_t len) {
	return ScanUart_SendInternal(data, len, false);
}

// --- Explicit domain hook avoids guessing START from arbitrary UART bytes ---
bool ScanUart_SendStart(const uint8_t *data, uint16_t len) {
	return ScanUart_SendInternal(data, len, true);
}

const uint8_t* ScanUart_GetReceived(uint16_t *out_len) {
	if (out_len == NULL)
		return NULL;
	*out_len = holding_len;
	return (holding_len > 0) ? holding_buf : NULL;
}

/*
 * Сохраняет сырое RX-событие для диагностики.
 * После подготовки к reset данные не передаются домену,
 * RX DMA не перезапускается и задача не получает новое уведомление.
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
	// --- Существующая проверка источника и размера ---
	if (huart->Instance != USART3)
		return;

	if (Size == 0 || Size > SCAN_UART_BUF_SIZE)
		return;

	// --- Диагностический снимок фактического события ---
	ScanUart_RecordRx(Size);

	// --- Позднее событие не продолжает отменённую операцию ---
	if (AppSafety_IsResetPending())
		return;

	// --- Существующая передача данных домену ---
	ScanUart_CaptureRx(Size, rx_event_type == 1U);
	memcpy((void*) holding_buf, dma_buffer, Size);
	holding_len = Size;

	(void) ScanUart_StartRx();

	osThreadFlagsSet(task_scan_contrHandle, FLAG_SCAN_UART_RX);
}


/*
 * scan_controller.c
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */




// --- Локальные данные ---
static ScanState_t g_state = SCAN_STATE_IDLE;
static uint8_t g_shadow_buf[SHADOW_BUF_SIZE];
static uint8_t g_shadow_len = 0;
static ScanStats_t g_stats;

static uint8_t g_barcode_buf[SHADOW_BUF_SIZE];
static uint8_t g_barcode_len = 0;

void ScanCtrl_Init(void)
{
    g_state = SCAN_STATE_IDLE;
    memset(&g_stats, 0, sizeof(g_stats));
    memset(g_shadow_buf, 0, SHADOW_BUF_SIZE);
    g_shadow_len = 0;
    g_barcode_len = 0;
}

bool ScanCtrl_StartScan(uint16_t timeout_sec)
{
    (void)timeout_sec;
    // Only an active transaction is busy. ERROR and DATA_READY retain the
    // previous result until the next request; neither blocks a new scan.
    if (g_state == SCAN_STATE_SCANNING) return false;

    g_state = SCAN_STATE_SCANNING;
    g_barcode_len = 0;
    return true;
}

bool ScanCtrl_ProcessReceived(const uint8_t *data, uint16_t len)
{
    if (g_state != SCAN_STATE_SCANNING) return false;

    SsiMsg_t msg;
    if (!Ssi_Decode(data, len, &msg)) {
        g_stats.ssi_decode_errors++;
        g_state = SCAN_STATE_ERROR;
        return false;
    }

    if (msg.opcode == SSI_OP_DECODE_DATA) {
        // Штрихкод получен
        uint16_t copy_len = (msg.data_len < SHADOW_BUF_SIZE) ? msg.data_len : SHADOW_BUF_SIZE;
        memcpy(g_barcode_buf, msg.data, copy_len);
        g_barcode_len = (uint8_t)copy_len;

        // Копия в shadow
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

bool ScanCtrl_GetLastBarcode(uint8_t *out_buf, uint8_t *out_len)
{
    if (out_buf == NULL || out_len == NULL) return false;
    if (g_shadow_len == 0) return false;

    memcpy(out_buf, g_shadow_buf, g_shadow_len);
    *out_len = g_shadow_len;
    return true;
}

bool ScanCtrl_HandleTimeout(void)
{
    if (g_state != SCAN_STATE_SCANNING) return false;
    g_stats.scan_timeouts++;
    g_state = SCAN_STATE_ERROR;
    return true;
}

void ScanCtrl_HandleTransportError(void)
{
    if (g_state == SCAN_STATE_SCANNING) g_state = SCAN_STATE_ERROR;
}

ScanState_t ScanCtrl_GetState(void)
{
    return g_state;
}

void ScanCtrl_GetStats(ScanStats_t *out)
{
    if (out != NULL) memcpy(out, &g_stats, sizeof(g_stats));
}



/*
 * scan_ssi.c
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */



// --- Checksum SSI: negative byte sum, transmitted as a big-endian word ---
static uint16_t Ssi_Checksum(const uint8_t *data, uint16_t len)
{
    uint32_t sum = 0;
    for (uint16_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return (uint16_t)(0U - sum);
}

// --- Encode one packet; Length includes the four-byte header ---
uint8_t Ssi_Encode(uint8_t opcode, const uint8_t *data,
                   uint16_t data_len, uint8_t *out_packet)
{
    if (out_packet == NULL || data_len > SSI_MAX_PAYLOAD ||
        (data_len != 0 && data == NULL)) return 0;

    uint16_t idx = 0;
    out_packet[idx++] = (uint8_t)(4 + data_len);
    out_packet[idx++] = opcode;
    out_packet[idx++] = SSI_MSG_SRC_HOST;
    out_packet[idx++] = 0x00;
    for (uint16_t i = 0; i < data_len; i++) {
        out_packet[idx++] = data[i];
    }

    uint16_t checksum = Ssi_Checksum(out_packet, idx);
    out_packet[idx++] = (uint8_t)(checksum >> 8);
    out_packet[idx++] = (uint8_t)checksum;
    return (uint8_t)idx;
}

// --- Validate the first complete packet before publishing its contents ---
bool Ssi_Decode(const uint8_t *buf, uint16_t buf_len, SsiMsg_t *out_msg)
{
    if (buf == NULL || out_msg == NULL || buf_len < 6) return false;

    uint16_t length = buf[0];
    if (length < 4 || length > 4 + SSI_MAX_PAYLOAD) return false;
    if (buf_len < length + 2) return false;

    uint16_t received_checksum = (uint16_t)(((uint16_t)buf[length] << 8) |
                                          buf[length + 1]);
    if (received_checksum != Ssi_Checksum(buf, length)) return false;

    out_msg->opcode = buf[1];
    out_msg->msg_src = buf[2];
    out_msg->status = buf[3];
    out_msg->data_len = length - 4;
    memcpy(out_msg->data, buf + 4, out_msg->data_len);
    return true;
}


/*
 * task_scan_controller.c
 *
 *  Created on: Jul 10, 2026
 *      Author: andrey
 *
 *  Задача Scan Controller.
 *  Управляет сканером SE-965HP через USART3 (SSI-протокол).
 *  Получает команды из scan_queue (START/STOP сканирования),
 *  отправляет результат сканирования через dispatcher в CAN.
 */












// --- Подтверждение подготовки; единственный писатель — задача Scanner ---
static volatile bool scan_reset_prepared = false;

// --- Чтение подтверждения задачей Dispatcher ---
bool ScanTask_IsResetPrepared(void) {
	return scan_reset_prepared;
}

/*
 * Вызывается только задачей Scanner, вне выполняющегося UART TX.
 * Отменяет доменное состояние и публикует подтверждение один раз.
 * Latch не снимается; новые операции до reset запрещены.
 */
static bool Scan_SyncResetState(void) {
	// --- Проверка запроса и ранее завершённой подготовки ---
	if (!AppSafety_IsResetPending())
		return false;

	if (scan_reset_prepared)
		return true;

	// --- Остановка RX и отмена текущего состояния ---
	if (!ScanUart_CancelRx()) {
		Error_Handler();
	}

	ScanCtrl_HandleTransportError();

	// --- Удаление уведомления отменённой операции ---
	uint32_t flags = osThreadFlagsClear(FLAG_SCAN_UART_RX);
	if ((flags & osFlagsError) != 0U) {
		Error_Handler();
	}

	// --- Публикация подтверждения после завершения подготовки ---
	__DMB();
	scan_reset_prepared = true;

	return true;
}

/*
 * Принимает решение о terminal-результате до возможного reset.
 * Решение сохраняется вызывающим кодом; передача CAN выполняется
 * после восстановления IRQ. Доставку ответа этот gate не подтверждает.
 */
static bool Scan_TryAcceptTerminalResult(void) {
	// --- Короткая критическая секция принятия результата ---
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	bool accepted = !AppSafety_IsResetPending();

	__set_PRIMASK(saved_primask);

	return accepted;
}

// --- Read-only revision probe or single BEEP: DONE means RX capture complete ---
static void ScanQueryRevision(uint16_t command) {

	// --- Отмена до начала запроса ---
	if (Scan_SyncResetState()) {
		CAN_SendNackPublic(command, CAN_ERR_DEVICE_BUSY);
		return;
	}

	static uint8_t packet[SSI_PACKET_MAX_LEN];
	const uint8_t beep_code = 0;
	uint8_t length =
			command == CAN_CMD_SCAN_BEEP ?
					Ssi_Encode(SSI_OP_BEEP, &beep_code, 1, packet) :
					Ssi_Encode(SSI_OP_REQUEST_REVISION, NULL, 0, packet);
	ScanUartCapture_t rx;
	ScanUart_BeginCapture();

	if (!ScanUart_Wake() || !ScanUart_Send(packet, length)) {
		ScanUart_EndCapture(NULL);

		// --- Отмена reset отличается от неисправности UART ---
		if (Scan_SyncResetState()) {
			CAN_SendNackPublic(command, CAN_ERR_DEVICE_BUSY);
		} else {
			CAN_SendNackPublic(command, CAN_ERR_SCAN_HARDWARE);
		}
		return;
	}

	// Keep the DMA TX buffer alive and capture even malformed/fragmented RX.
	// Fixed 3 s covers the default 2 s SSI response timeout with margin.
	uint32_t started = osKernelGetTickCount();
	uint32_t duration = 3U * osKernelGetTickFreq();
	// --- Ожидание capture с проверкой отмены ---
	while ((uint32_t) (osKernelGetTickCount() - started) < duration) {
		if (Scan_SyncResetState()) {
			CAN_SendNackPublic(command, CAN_ERR_DEVICE_BUSY);
			return;
		}

		AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
		osDelay((osKernelGetTickFreq() + 19U) / 20U);
	}

	// --- Проверка отмены после последнего ожидания ---
	if (Scan_SyncResetState()) {
		CAN_SendNackPublic(command, CAN_ERR_DEVICE_BUSY);
		return;
	}

	ScanUart_EndCapture(&rx);

	// v1 envelope: schema8, flags8 (bit0 overflow), length16, errors32, raw.
	// Little-endian metadata; padding to six bytes is zero and checked by host.
	uint8_t response[204] = { 1U, 0U };
	response[1] = rx.overflow ? 1U : 0U;
	response[2] = (uint8_t) rx.length;
	response[3] = (uint8_t) (rx.length >> 8);
	for (unsigned i = 0; i < 4; ++i)
		response[4 + i] = (uint8_t) (rx.errors >> (8U * i));
	memcpy(response + 8, rx.bytes, rx.length);
	uint16_t total = (uint16_t) (((8U + rx.length + 5U) / 6U) * 6U);

	// --- Принятие результата capture либо завершение отмены ---
	if (!Scan_TryAcceptTerminalResult()) {
		(void) Scan_SyncResetState();
		CAN_SendNackPublic(command, CAN_ERR_DEVICE_BUSY);
		return;
	}
	if (!CAN_SendDiagnosticResponse(command, response, total))
		CAN_SendNackPublic(command, CAN_ERR_DEVICE_BUSY);
}

void app_start_task_scan_controller(void *argument) {
	(void) argument;

	uint8_t ssi_packet[SSI_PACKET_MAX_LEN];
	uint16_t ssi_len;

	ScanCtrl_Init();
	ScanUart_Init();

	// --- SCAN_ENABLE: однократно при старте ---
	ssi_len = Ssi_Encode(SSI_OP_SCAN_ENABLE, NULL, 0, ssi_packet);
	if (ScanUart_Wake())
		ScanUart_Send(ssi_packet, ssi_len);

	ScanResult_t cmd;

	// --- Интервал ожидания команды в тиках RTOS ---
	const uint32_t idle_wait_ticks = (APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS
			* osKernelGetTickFreq()) / 1000U;

	for (;;) {

		// --- Подготовка выполняется и при отсутствии команд ---
		(void) Scan_SyncResetState();

		osStatus_t queue_status;

		// --- Ожидание команды от Dispatcher ---
		queue_status = osMessageQueueGet(scan_queueHandle, &cmd,
		NULL, idle_wait_ticks);

		// Пустая очередь — штатный режим, задача исполняется.
		if (queue_status == osErrorTimeout) {
			AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
			continue;
		}

		// Ошибка очереди не подтверждает прогресс.
		// Содержимое cmd при ошибке не обрабатываем.
		if (queue_status != osOK) {
			continue;
		}

		// --- Команда получена и передаётся на обработку ---
		AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);

		// --- Команда из очереди не запускается после reset-latch ---
		if (Scan_SyncResetState()) {
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
			continue;
		}

		if (cmd.cmd_code == CAN_CMD_SCAN_GET_REVISION
				|| cmd.cmd_code == CAN_CMD_SCAN_BEEP) {
			ScanQueryRevision(cmd.cmd_code);
			continue;
		}

		if (cmd.cmd_code == CAN_CMD_SCAN_GET_DATA) {
			// --- Подготовка результата из сохранённого штрихкода ---
			uint8_t barcode[SHADOW_BUF_SIZE];
			uint8_t barcode_len = 0;
			bool has_barcode = ScanCtrl_GetLastBarcode(barcode, &barcode_len);

			// --- Принятие результата либо завершение отмены ---
			if (!Scan_TryAcceptTerminalResult()) {
				(void) Scan_SyncResetState();
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
				continue;
			}

			// --- Передача принятого результата вне критической секции ---
			if (has_barcode) {
				CAN_SendDataFragmented(cmd.cmd_code, barcode, barcode_len);
				CAN_SendDone(cmd.cmd_code, 0);
			} else {
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_NO_DATA);
			}
			continue;
		}

		if (cmd.cmd_code != CAN_CMD_SCAN_REQUEST) {
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_UNKNOWN_CMD);
			continue;
		}

		// --- SCAN_REQUEST ---
		if (!ScanCtrl_StartScan(cmd.timeout_sec)) {
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
			continue;
		}

		// Отправка START_DECODE
		ssi_len = Ssi_Encode(SSI_OP_START_DECODE, NULL, 0, ssi_packet);
		// Retain this attempt separately from the subsequent STOP_DECODE.
		if (!ScanUart_Wake() || !ScanUart_SendStart(ssi_packet, ssi_len)) {
			// --- Reset отличается от ошибки передачи START ---
			if (Scan_SyncResetState()) {
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
			} else {
				ScanCtrl_HandleTransportError();
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_HARDWARE);
			}
			continue;
		}

		// Ожидание UART RX с таймаутом
		// --- Общий срок ожидания ответа Scanner ---
		// Считаем фактически прошедшие тики, включая время выполнения задачи.
		uint32_t timeout_ms =
				cmd.timeout_sec > 0U ?
						(uint32_t) cmd.timeout_sec * 1000U : 4000U;
		uint32_t tick_freq = osKernelGetTickFreq();
		uint32_t timeout_ticks = (uint32_t) (((uint64_t) timeout_ms * tick_freq)
				/ 1000U);
		uint32_t slice_ticks = (500U * tick_freq) / 1000U;
		uint32_t started = osKernelGetTickCount();

		bool rx_received = false;
		bool wait_error = false;

		// --- Ограниченные ожидания внутри общего срока ---
		for (;;) {
			// --- Отмена ожидания текущего сканирования ---
			if (AppSafety_IsResetPending())
				break;
			uint32_t elapsed = osKernelGetTickCount() - started;
			if (elapsed >= timeout_ticks) {
				break;
			}

			uint32_t remaining = timeout_ticks - elapsed;
			uint32_t wait_ticks =
					remaining < slice_ticks ? remaining : slice_ticks;

			uint32_t flags = osThreadFlagsWait(
			FLAG_SCAN_UART_RX, osFlagsWaitAny, wait_ticks);

			// Штатное ожидание завершилось без данных.
			if (flags == osFlagsErrorTimeout) {
				AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
				continue;
			}

			// Ошибка RTOS: прекращаем запрос без ложного heartbeat.
			if ((flags & osFlagsError) != 0U) {
				wait_error = true;
				break;
			}

			AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
			rx_received = true;
			break;
		}

		// --- Reset имеет приоритет над RX, timeout и ошибкой ожидания ---
		if (Scan_SyncResetState()) {
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
			continue;
		}

		// --- Принятие terminal-результата текущего сканирования ---
		// Gate предшествует изменению статистики и сохранению штрихкода.
		if (!Scan_TryAcceptTerminalResult()) {
			(void) Scan_SyncResetState();
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
			continue;
		}

		// --- Раздельные исходы ошибки ожидания и отсутствия ответа ---
		if (wait_error) {
			ScanCtrl_HandleTransportError();
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_COMM);
		} else if (!rx_received) {
			ScanCtrl_HandleTimeout();
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_TIMEOUT);
		} else {
			// --- Существующая обработка полученных UART-данных ---
			uint16_t rx_len = 0;
			const uint8_t *rx_data = ScanUart_GetReceived(&rx_len);

			if (rx_data != NULL && ScanCtrl_ProcessReceived(rx_data, rx_len)) {
				uint8_t buf[SHADOW_BUF_SIZE];
				uint8_t buf_len = 0;

				ScanCtrl_GetLastBarcode(buf, &buf_len);
				CAN_SendDataFragmented(cmd.cmd_code, buf, buf_len);
				CAN_SendDone(cmd.cmd_code, 0);
			} else {
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_HARDWARE);
			}
		}

		// STOP_DECODE
		ssi_len = Ssi_Encode(SSI_OP_STOP_DECODE, NULL, 0, ssi_packet);
		if (ScanUart_Wake())
			ScanUart_Send(ssi_packet, ssi_len);
	}
}


static uint16_t Dispatcher_ReadLe16(const uint8_t *data) {
	return (uint16_t) data[0] | ((uint16_t) data[1] << 8U);
}

static void Dispatcher_PrepareReset(void) {
	// --- Начало общего срока подготовки ---
	const uint32_t timeout_ticks = osKernelGetTickFreq();
	const uint32_t started = osKernelGetTickCount();

	AppSafety_PrepareReset();

	// --- Ограниченное ожидание подтверждения домена ---
	for (;;) {
		if ((uint32_t) (osKernelGetTickCount() - started) >= timeout_ticks) {
			Error_Handler();
		}

		if (ScanTask_IsResetPrepared())
			return;

		if (osDelay(1U) != osOK) {
			Error_Handler();
		}

		// Подтверждает успешный проход ожидания Dispatcher.
		AppWatchdog_Heartbeat(APP_WDG_CLIENT_DISPATCHER);
	}
}

static void host_dispatch(ParsedCanCommand_t parsed) { switch(parsed.cmd_code) {
case CAN_CMD_SRV_REBOOT: {
			// --- Проверка ключа до любых действий подготовки ---
			if ((parsed.data_len < 2U)
					|| (Dispatcher_ReadLe16(parsed.data) != SRV_MAGIC_REBOOT)) {
				CAN_SendNackPublic(parsed.cmd_code, CAN_ERR_INVALID_KEY);
				break;
			}

			// --- Подтверждённая подготовка предшествует DONE ---
			Dispatcher_PrepareReset();

			CAN_SendDone(parsed.cmd_code, 0U);

			// --- Существующее окно отправки HC: 100 тиков ---
			// Постановка DONE в очередь не гарантирует доставку по CAN.
			if (osDelay(100U) != osOK) {
				Error_Handler();
			}

			NVIC_SystemReset();
			break;
		}
case CAN_CMD_SRV_FACTORY_RESET: {
			// --- Проверка ключа до подготовки и стирания Flash ---
			if ((parsed.data_len < 2U)
					|| (Dispatcher_ReadLe16(parsed.data)
							!= SRV_MAGIC_FACTORY_RESET)) {
				CAN_SendNackPublic(parsed.cmd_code, CAN_ERR_INVALID_KEY);
				break;
			}

			// --- Подтверждённая подготовка предшествует Flash ---
			Dispatcher_PrepareReset();

			// --- Результат стирания определяет сервисный ответ ---
			if (AppConfig_FactoryReset()) {
				CAN_SendDone(parsed.cmd_code, 0U);
			} else {
				CAN_SendNackPublic(parsed.cmd_code, CAN_ERR_FLASH_WRITE);
			}

			// --- После ошибки управление также не возобновляется ---
			// Существующее окно HC: 100 тиков, без гарантии доставки CAN.
			if (osDelay(100U) != osOK) {
				Error_Handler();
			}

			NVIC_SystemReset();
			break;
		}
 default: assert(0); }}

void Error_Handler(void) {
	/* USER CODE BEGIN Error_Handler_Debug */
	/* User can add his own implementation to report the HAL error return state */

	// --- Терминальный аварийный останов ---
	/*
	 * Запрещаем переключение задач до отпускания управляющих линий.
	 * Штатное управление не возобновляется.
	 * RTOS, CAN и обслуживание IWDG здесь не используются.
	 */
	__disable_irq();
	AppSafety_AllOff();
	__DSB();
	__ISB();

	// --- Ожидание аппаратного сброса ---
	/*
	 * Запущенный IWDG выполнит reset по своему таймауту.
	 * До запуска IWDG ожидаем внешнего сброса.
	 */
	host_terminal(1);
	/* USER CODE END Error_Handler_Debug */
}

static void EnterSafeFaultState(void) {
	__disable_irq();
	AppSafety_AllOff();
	__DSB();
	__ISB();
}

void NMI_Handler(void) {
	/* USER CODE BEGIN NonMaskableInt_IRQn 0 */
	EnterSafeFaultState();
	/* USER CODE END NonMaskableInt_IRQn 0 */
	/* USER CODE BEGIN NonMaskableInt_IRQn 1 */
	host_terminal(1);
	/* USER CODE END NonMaskableInt_IRQn 1 */
}

void HardFault_Handler(void) {
	/* USER CODE BEGIN HardFault_IRQn 0 */
	EnterSafeFaultState();
	/* USER CODE END HardFault_IRQn 0 */
	host_terminal(1);
}

void MemManage_Handler(void) {
	/* USER CODE BEGIN MemoryManagement_IRQn 0 */
	EnterSafeFaultState();
	/* USER CODE END MemoryManagement_IRQn 0 */
	host_terminal(1);
}

void BusFault_Handler(void) {
	/* USER CODE BEGIN BusFault_IRQn 0 */
	EnterSafeFaultState();
	/* USER CODE END BusFault_IRQn 0 */
	host_terminal(1);
}

void UsageFault_Handler(void) {
	/* USER CODE BEGIN UsageFault_IRQn 0 */
	EnterSafeFaultState();
	/* USER CODE END UsageFault_IRQn 0 */
	host_terminal(1);
}
