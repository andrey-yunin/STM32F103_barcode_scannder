/*
 * app_flash.h
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 *
 *
 *  Модуль хранения конфигурации Scanner Executor в последней странице Flash.
 *  Адрес: 0x0800FC00 (Page 63 для STM32F103C8, 64KB Flash, 1KB страница).
 *
 *  Структура AppConfig_t (16 байт):
 *  magic(4B) + performer_id(1B) + reserved[9](9B) + checksum(2B) = 16B
 *
 *  Защита: recursive mutex с наследованием приоритета.
 *  Валидация: magic key + CRC16-CCITT (Modbus).
 */

#ifndef INC_APP_FLASH_H_
#define INC_APP_FLASH_H_

#include <stdint.h>
#include <stdbool.h>

// --- Адрес конфигурационной страницы Flash ---
// Последняя страница STM32F103C8 (64KB Flash, страница 63 по 1KB).
#define APP_CONFIG_FLASH_ADDR    0x0800FC00
#define APP_CONFIG_MAGIC         0x55AAEEFF

// --- Структура конфигурации ---
// Размер 16 байт, кратен 4 — запись словами uint32_t.
typedef struct {
    uint32_t magic;             // Ключ валидности (APP_CONFIG_MAGIC)
    uint8_t  performer_id;      // CAN NodeID (default 0x60)
    uint8_t  reserved[9];       // Резерв: SSI-параметры, baudrate и т.д.
    uint16_t checksum;          // CRC16-CCITT Modbus
} AppConfig_t;

// --- Публичный API ---

void AppConfig_Init(void);

bool AppConfig_Commit(void);

void AppConfig_FactoryReset(void);

void AppConfig_GetMCU_UID(uint8_t *out_uid);

uint32_t AppConfig_GetPerformerID(void);

void AppConfig_SetPerformerID(uint32_t id);


#endif /* INC_APP_FLASH_H_ */
