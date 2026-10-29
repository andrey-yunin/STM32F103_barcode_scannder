/*
 * app_flash.c
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */

#include "app_flash.h"
#include "can_protocol.h"
#include "main.h"
#include "cmsis_os.h"
#include <string.h>
#include <stddef.h>


// --- RAM-копия конфигурации и мьютекс ---
static AppConfig_t g_app_config;
static osMutexId_t configMutex = NULL;

const osMutexAttr_t configMutex_attr = {
    "configMutex",
    osMutexRecursive | osMutexPrioInherit,
    NULL,
    0U
};

// --- CRC16-CCITT Modbus ---
// Считается от начала структуры до поля checksum (не включая его).
static uint16_t CalculateChecksum(const AppConfig_t *cfg)
{
    uint16_t crc = 0xFFFF;
    const uint8_t *p = (const uint8_t *)cfg;
    const uint32_t len = (uint32_t)offsetof(AppConfig_t, checksum);

    for (uint32_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if ((crc & 0x0001U) != 0U) {
                crc = (crc >> 1) ^ 0xA001U;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// --- UID: 96-битный идентификатор STM32F103 ---
void AppConfig_GetMCU_UID(uint8_t *out_uid)
{
    if (out_uid == NULL) return;
    uint8_t *uid_base = (uint8_t *)0x1FFFF7E8;
    memcpy(out_uid, uid_base, 12);
}

// --- Инициализация: чтение из Flash или заводские настройки ---
void AppConfig_Init(void)
{
    const AppConfig_t *flash_cfg;

    // --- Конфигурация не допускается к работе без mutex ---
    // Механизм Thermo: отказ создания ведёт в существующий fatal-путь.
    if (configMutex == NULL) {
        configMutex = osMutexNew(&configMutex_attr);
    }
    if (configMutex == NULL) {
        Error_Handler();
        return;
    }

    flash_cfg = (const AppConfig_t *)APP_CONFIG_FLASH_ADDR;

    // --- Принимаем только целую запись своего адресного семейства ---
    if ((flash_cfg->magic == APP_CONFIG_MAGIC) &&
        (flash_cfg->checksum == CalculateChecksum(flash_cfg)) &&
        (flash_cfg->performer_id >= CAN_ADDR_SCANNER_BOARD_BASE) &&
        (flash_cfg->performer_id <= CAN_ADDR_SCANNER_BOARD_LAST)) {
        memcpy(&g_app_config, flash_cfg, sizeof(g_app_config));
    } else {
        // --- Defaults только в RAM; исходную Flash-страницу не меняем ---
        // Scanner сохраняет собственный layout и алгоритм checksum.
        memset(&g_app_config, 0, sizeof(g_app_config));
        g_app_config.magic = APP_CONFIG_MAGIC;
        g_app_config.performer_id = CAN_ADDR_SCANNER_BOARD_BASE;
        g_app_config.checksum = CalculateChecksum(&g_app_config);
    }
}

// --- Чтение NodeID из RAM-копии ---
uint32_t AppConfig_GetPerformerID(void)
{
    uint32_t id = 0x60;
    if (osMutexAcquire(configMutex, osWaitForever) == osOK) {
        id = g_app_config.performer_id;
        osMutexRelease(configMutex);
    }
    return id;
}

// --- Запись NodeID в RAM-копию (без Flash) ---
void AppConfig_SetPerformerID(uint32_t id)
{
    if (osMutexAcquire(configMutex, osWaitForever) == osOK) {
        g_app_config.performer_id = (uint8_t)id;
        osMutexRelease(configMutex);
    }
}

// --- Commit: RAM -> Flash под mutex с проверкой HAL-стадий ---
// Механизм HC: failed Unlock запрещает erase/program; Lock проверяется всегда.
// Scanner сохраняет layout и checksum. Отказ не гарантирует сохранность
// прежней записи после начавшегося стирания; независимого readback здесь нет.
bool AppConfig_Commit(void) {
	FLASH_EraseInitTypeDef erase_config;
	HAL_StatusTypeDef status = HAL_ERROR;
	uint32_t page_error = 0U;
	uint32_t flash_address = APP_CONFIG_FLASH_ADDR;
	const uint32_t *config_words;

	if (configMutex == NULL) {
		return false;
	}

	if (osMutexAcquire(configMutex, osWaitForever) != osOK) {
		return false;
	}

	g_app_config.checksum = CalculateChecksum(&g_app_config);

	erase_config.TypeErase = FLASH_TYPEERASE_PAGES;
	erase_config.PageAddress = APP_CONFIG_FLASH_ADDR;
	erase_config.NbPages = 1U;

	status = HAL_FLASH_Unlock();

	if (status == HAL_OK) {
		status = HAL_FLASHEx_Erase(&erase_config, &page_error);
	}

	if (status == HAL_OK) {
		config_words = (const uint32_t*) &g_app_config;

		for (uint32_t offset = 0U; offset < sizeof(g_app_config); offset +=
				sizeof(uint32_t)) {
			status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, flash_address,
					*config_words);

			if (status != HAL_OK) {
				break;
			}

			flash_address += sizeof(uint32_t);
			config_words++;
		}
	}

	if (HAL_FLASH_Lock() != HAL_OK) {
		status = HAL_ERROR;
	}

	(void) osMutexRelease(configMutex);

	return (status == HAL_OK);
}

// --- Factory reset: стирание конфигурационной страницы под тем же mutex ---
// RAM NodeID не меняется: сервисный ответ до reset идёт текущим адресом.
bool AppConfig_FactoryReset(void) {
	FLASH_EraseInitTypeDef erase_config = { 0 };
	uint32_t page_error = 0U;
	HAL_StatusTypeDef status;

	if (configMutex == NULL) {
		return false;
	}

	if (osMutexAcquire(configMutex, osWaitForever) != osOK) {
		return false;
	}

	erase_config.TypeErase = FLASH_TYPEERASE_PAGES;
	erase_config.PageAddress = APP_CONFIG_FLASH_ADDR;
	erase_config.NbPages = 1U;

	/* Стирание допустимо только после успешной разблокировки. */
	status = HAL_FLASH_Unlock();

	if (status == HAL_OK) {
		status = HAL_FLASHEx_Erase(&erase_config, &page_error);
	}

	/* Закрываем доступ к записи независимо от результата стирания. */
	if (HAL_FLASH_Lock() != HAL_OK) {
		status = HAL_ERROR;
	}

	/* После получения mutex освобождаем его при любом результате. */
	if (osMutexRelease(configMutex) != osOK) {
		Error_Handler();
		return false;
	}

	return status == HAL_OK;
}
