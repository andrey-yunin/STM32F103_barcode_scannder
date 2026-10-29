#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#define FLAG_SCAN_UART_RX 4U
#define osFlagsWaitAny 0U
#define osFlagsErrorTimeout 0xFFFFFFFEU
#define osFlagsError 0x80000000U
#define APP_WDG_CLIENT_SCAN 2U
static uint32_t now,mode,calls,beats,expected;
static uint32_t osKernelGetTickFreq(void){return 1000;}
static uint32_t osKernelGetTickCount(void){return now;}
static void AppWatchdog_Heartbeat(unsigned c){assert(c==2);++beats;}
static uint32_t osThreadFlagsWait(unsigned f,unsigned opts,uint32_t wait){
 assert(f==4 && opts==0 && wait>0 && wait<=500);
 ++calls;
 if(mode==1){now+=1;return 4;}
 if(mode==2){return 0xFFFFFFFCU;}
 if(mode==3 && calls==4)assert(wait==200);
 now+=wait+(mode==3 && calls<4?100:0);
 return osFlagsErrorTimeout;
}
static void run(void){struct {uint16_t timeout_sec;}cmd={2};
#include "actual_uart_wait.inc"
 if(expected==0)assert(!rx_received && !wait_error);
 if(expected==1)assert(rx_received && !wait_error);
 if(expected==2)assert(!rx_received && wait_error);
}
int main(void){
 mode=0;expected=0;run();assert(now==2000 && calls==4 && beats==4);
 now=calls=beats=0;mode=1;expected=1;run();assert(calls==1 && beats==1);
 now=calls=beats=0;mode=2;expected=2;run();assert(calls==1 && beats==0 && now==0);
 now=calls=beats=0;mode=3;expected=0;run();assert(now==2000 && calls==4);
 now=0xFFFFFF00U;calls=beats=0;mode=0;expected=0;run();assert(now==(uint32_t)(0xFFFFFF00U+2000U) && calls==4);
 puts("PASS: total deadline, RX, RTOS error without heartbeat, remaining wait 200 ticks, tick wrap");
}
