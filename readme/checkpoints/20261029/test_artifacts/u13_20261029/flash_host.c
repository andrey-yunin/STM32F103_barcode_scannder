#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "app_flash.h"
#include "can_protocol.h"
typedef int HAL_StatusTypeDef;
enum { HAL_OK=0,HAL_ERROR=1,osOK=0,osError=-1,osWaitForever=-1,FLASH_TYPEERASE_PAGES=2,FLASH_TYPEPROGRAM_WORD=3 };
typedef struct {uint32_t TypeErase,PageAddress,NbPages;} FLASH_EraseInitTypeDef;
static AppConfig_t g_app_config;
static void *configMutex;
static uint32_t flash_words[256];
static int acquire_fail,unlock_fail,erase_fail,program_fail,lock_fail,release_fail;
static int acquired,unlocks,erases,programs,locks,releases,fatals,held,done,nack,resets,delays;
static uint16_t last_error;
static int osMutexAcquire(void *m,int t){assert(m==configMutex&&t==osWaitForever);acquired++;if(acquire_fail)return osError;held=1;return osOK;}
static int osMutexRelease(void *m){assert(m==configMutex&&held);held=0;releases++;return release_fail?osError:osOK;}
static void Error_Handler(void){fatals++;}
static int HAL_FLASH_Unlock(void){assert(held);unlocks++;return unlock_fail?HAL_ERROR:HAL_OK;}
static int HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *e,uint32_t *pe){assert(held);assert(e->TypeErase==FLASH_TYPEERASE_PAGES&&e->PageAddress==APP_CONFIG_FLASH_ADDR&&e->NbPages==1);erases++;*pe=0;if(erase_fail)return HAL_ERROR;memset(flash_words,255,sizeof(flash_words));return HAL_OK;}
static int HAL_FLASH_Program(uint32_t t,uint32_t a,uint64_t v){assert(held&&t==FLASH_TYPEPROGRAM_WORD);assert(a==APP_CONFIG_FLASH_ADDR+4U*(unsigned)programs);programs++;if(program_fail==programs)return HAL_ERROR;flash_words[(a-APP_CONFIG_FLASH_ADDR)/4]=(uint32_t)v;return HAL_OK;}
static int HAL_FLASH_Lock(void){assert(held);locks++;return lock_fail?HAL_ERROR:HAL_OK;}
void CAN_SendDone(uint16_t c,uint8_t v){assert(c==CAN_CMD_SRV_FACTORY_RESET&&v==0);done++;}
void CAN_SendNackPublic(uint16_t c,uint16_t e){assert(c==CAN_CMD_SRV_FACTORY_RESET);nack++;last_error=e;}
static int osDelay(uint32_t t){assert(t==100);assert(done+nack==1);delays++;return osOK;}
static void NVIC_SystemReset(void){assert(delays==1);resets++;}
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
static void factory_command(uint16_t key_value){struct {uint16_t cmd_code;uint8_t data[5];} parsed={.cmd_code=CAN_CMD_SRV_FACTORY_RESET,.data={key_value&255,key_value>>8}};switch(parsed.cmd_code){case CAN_CMD_SRV_FACTORY_RESET: {
			uint16_t key = (uint16_t) (parsed.data[0]
					| ((uint16_t) parsed.data[1] << 8));
			if (key == SRV_MAGIC_FACTORY_RESET) {
				// --- FACTORY_RESET: стирание Flash + перезагрузка ---
				// --- Результат стирания определяет сервисный ответ ---
				if (AppConfig_FactoryReset()) {
					CAN_SendDone(parsed.cmd_code, 0U);
				} else {
					CAN_SendNackPublic(parsed.cmd_code, CAN_ERR_FLASH_WRITE);
				}
				osDelay(100);
				NVIC_SystemReset();
			} else {
				CAN_SendNackPublic(parsed.cmd_code, CAN_ERR_INVALID_KEY);
			}
			break;
		}

		}}

static void init(void){
 configMutex=(void*)1;acquire_fail=unlock_fail=erase_fail=program_fail=lock_fail=release_fail=0;
 acquired=unlocks=erases=programs=locks=releases=fatals=held=done=nack=resets=delays=0;last_error=0;
 memset(flash_words,0xA5,sizeof(flash_words));memset(&g_app_config,0x39,sizeof(g_app_config));g_app_config.magic=APP_CONFIG_MAGIC;g_app_config.performer_id=0x6F;
}
static void check_stages(int mode,int stage){
 init();if(stage==0)configMutex=NULL;if(stage==1)acquire_fail=1;if(stage==2)unlock_fail=1;if(stage==3)erase_fail=1;if(stage==4)lock_fail=1;
 assert(!(mode?AppConfig_FactoryReset():AppConfig_Commit()));assert(!held);
 if(stage<=1){assert(!unlocks&&!erases&&!programs&&!locks&&!releases);return;}
 assert(unlocks==1&&locks==1&&releases==1);
 if(stage==2)assert(!erases&&!programs);
 if(stage==3)assert(erases==1&&!programs);
}
int main(void){
 _Static_assert(sizeof(AppConfig_t)==16,"Scanner layout");
 for(int m=0;m<2;m++)for(int s=0;s<5;s++)check_stages(m,s);
 for(int i=1;i<=4;i++){init();program_fail=i;assert(!AppConfig_Commit());assert(programs==i&&locks==1&&releases==1&&!held);}
 init();assert(AppConfig_Commit());assert(programs==4&&erases==1&&locks==1&&releases==1);assert(g_app_config.checksum==CalculateChecksum(&g_app_config));assert(!memcmp(flash_words,&g_app_config,16));
 init();AppConfig_t before=g_app_config;assert(AppConfig_FactoryReset());assert(!memcmp(&before,&g_app_config,16));assert(!programs&&locks==1&&releases==1);for(int i=0;i<256;i++)assert(flash_words[i]==UINT32_MAX);
 init();release_fail=1;assert(!AppConfig_FactoryReset());assert(fatals==1);
 init();release_fail=1;assert(AppConfig_Commit());assert(!fatals); /* Exact HC behavior: Commit ignores release result. */
 init();factory_command(0);assert(nack==1&&last_error==CAN_ERR_INVALID_KEY&&!resets&&!unlocks);
 init();factory_command(SRV_MAGIC_FACTORY_RESET);assert(done==1&&!nack&&resets==1&&erases==1);assert(g_app_config.performer_id==0x6F);
 init();erase_fail=1;factory_command(SRV_MAGIC_FACTORY_RESET);assert(!done&&nack==1&&last_error==CAN_ERR_FLASH_WRITE&&resets==1);
 puts("PASS: exact HC Commit/FactoryReset; mutex, Unlock/Erase/each Program/Lock failure paths; 16-byte layout+CRC; reset preserves RAM; F006 key/DONE/NACK/reset. HOST stubs only; no physical Flash readback.");
}
