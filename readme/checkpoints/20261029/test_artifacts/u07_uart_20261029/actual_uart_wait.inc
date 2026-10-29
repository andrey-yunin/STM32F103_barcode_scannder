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

		