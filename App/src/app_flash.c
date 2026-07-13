/*
 * app_flash.c
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */

#include "app_flash.h"
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
    // Создание мьютекса
    if (configMutex == NULL) {
        configMutex = osMutexNew(&configMutex_attr);
    }

    const AppConfig_t *flash_cfg = (const AppConfig_t *)APP_CONFIG_FLASH_ADDR;

    // Валидация: magic + CRC16
    if (flash_cfg->magic == APP_CONFIG_MAGIC &&
        flash_cfg->checksum == CalculateChecksum(flash_cfg))
    {
        memcpy(&g_app_config, flash_cfg, sizeof(AppConfig_t));
    } else {
        // Заводские настройки по умолчанию
        memset(&g_app_config, 0, sizeof(AppConfig_t));
        g_app_config.magic = APP_CONFIG_MAGIC;
        g_app_config.node_id = 0x60;
        g_app_config.checksum = CalculateChecksum(&g_app_config);
    }
}

// --- Чтение NodeID из RAM-копии ---
uint8_t AppConfig_GetNodeID(void)
{
    uint8_t id = 0x60;
    if (osMutexAcquire(configMutex, osWaitForever) == osOK) {
        id = g_app_config.node_id;
        osMutexRelease(configMutex);
    }
    return id;
}

// --- Запись NodeID в RAM-копию (без Flash) ---
void AppConfig_SetNodeID(uint8_t id)
{
    if (osMutexAcquire(configMutex, osWaitForever) == osOK) {
        g_app_config.node_id = id;
        osMutexRelease(configMutex);
    }
}

// --- Commit: RAM → Flash ---
bool AppConfig_Commit(void)
{
    bool success = false;
    HAL_StatusTypeDef status = HAL_ERROR;
    uint32_t PageError = 0;
    FLASH_EraseInitTypeDef EraseInitStruct;

    if (osMutexAcquire(configMutex, osWaitForever) == osOK) {
        // Обновление контрольной суммы
        g_app_config.checksum = CalculateChecksum(&g_app_config);

        HAL_FLASH_Unlock();

        // Стирание страницы
        EraseInitStruct.TypeErase   = FLASH_TYPEERASE_PAGES;
        EraseInitStruct.PageAddress = APP_CONFIG_FLASH_ADDR;
        EraseInitStruct.NbPages     = 1;
        status = HAL_FLASHEx_Erase(&EraseInitStruct, &PageError);

        if (status == HAL_OK) {
            // Запись 32-битными словами
            uint32_t *pData = (uint32_t *)&g_app_config;
            uint32_t addr = APP_CONFIG_FLASH_ADDR;
            for (uint32_t i = 0; i < sizeof(AppConfig_t); i += 4) {
                status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, *pData);
                if (status != HAL_OK) break;
                addr += 4;
                pData++;
            }
        }

        HAL_FLASH_Lock();
        success = (status == HAL_OK);
        osMutexRelease(configMutex);
    }
    return success;
}

// --- Factory reset: стирание Flash-страницы ---
void AppConfig_FactoryReset(void)
{
    HAL_FLASH_Unlock();
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t PageError = 0;
    EraseInitStruct.TypeErase   = FLASH_TYPEERASE_PAGES;
    EraseInitStruct.PageAddress = APP_CONFIG_FLASH_ADDR;
    EraseInitStruct.NbPages     = 1;
    HAL_FLASHEx_Erase(&EraseInitStruct, &PageError);
    HAL_FLASH_Lock();
}
