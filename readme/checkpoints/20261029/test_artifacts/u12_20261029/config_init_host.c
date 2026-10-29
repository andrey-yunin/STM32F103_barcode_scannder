#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "app_flash.h"
#include "can_protocol.h"
static AppConfig_t flash,g_app_config;
static void *configMutex;
static const unsigned configMutex_attr=1;
static unsigned creates,fatals;
static int fail_mutex;
static void *osMutexNew(const void *attr){assert(attr==&configMutex_attr);++creates;return fail_mutex?NULL:(void*)1;}
static void Error_Handler(void){++fatals;}
#undef APP_CONFIG_FLASH_ADDR
#define APP_CONFIG_FLASH_ADDR ((uintptr_t)&flash)
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
static void defaults(void){assert(g_app_config.magic==APP_CONFIG_MAGIC && g_app_config.performer_id==0x60 && g_app_config.checksum==CalculateChecksum(&g_app_config));for(unsigned j=0;j<9;++j)assert(!g_app_config.reserved[j]);}
int main(void){
 _Static_assert(sizeof(AppConfig_t)==16,"preserve Scanner layout");
 const uint8_t ids[]={0x60,0x6F,0x00,0x10,0x5F,0x70,0xFF};
 for(unsigned i=0;i<sizeof ids;++i){memset(&flash,0xA5,sizeof flash);flash.magic=APP_CONFIG_MAGIC;flash.performer_id=ids[i];flash.checksum=CalculateChecksum(&flash);AppConfig_t saved=flash;
 configMutex=NULL;creates=fatals=0;AppConfig_Init();assert(creates==1 && !fatals && !memcmp(&flash,&saved,sizeof flash));
 if(i<2)assert(!memcmp(&g_app_config,&flash,sizeof flash));else defaults();
 AppConfig_Init();assert(creates==1);}
 memset(&flash,0,sizeof flash);flash.magic=1;flash.performer_id=0x60;flash.checksum=CalculateChecksum(&flash);AppConfig_Init();defaults();
 flash.magic=APP_CONFIG_MAGIC;flash.checksum=CalculateChecksum(&flash)^1;AppConfig_Init();defaults();
 AppConfig_t saved=g_app_config;fail_mutex=1;configMutex=NULL;creates=fatals=0;AppConfig_Init();assert(creates==1 && fatals==1 && !memcmp(&g_app_config,&saved,sizeof saved));
 puts("PASS: valid60/6F load, invalid family/magic/CRC defaults, mutex failure/reuse, reserved/layout/checksum preserved, Flash unchanged");}
