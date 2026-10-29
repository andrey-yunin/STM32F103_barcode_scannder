#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "can_protocol.h"
static volatile CanDiagnostics_t g_can_diag;
static uint32_t irq;
static unsigned writes,copies;
static uint32_t __get_PRIMASK(void){return irq;}
static void __disable_irq(void){irq=1;++writes;}
static void __set_PRIMASK(uint32_t p){irq=p;++writes;}
static void *checked_copy(void *d,const void *s,size_t n){assert(irq==1);++copies;return memcpy(d,s,n);}
#define memcpy checked_copy
void CAN_Diagnostics_GetSnapshot(CanDiagnostics_t *out) {
	uint32_t primask;

	if (out == NULL) {
		return;
	}

	primask = __get_PRIMASK();

	__disable_irq();
	memcpy(out, (const void*) &g_can_diag, sizeof(CanDiagnostics_t));
	__set_PRIMASK(primask);
}
#undef memcpy
int main(void){
 memset((void*)&g_can_diag,0xA5,sizeof g_can_diag);
 for(unsigned initial=0;initial<2;++initial){
  CanDiagnostics_t out;memset(&out,0,sizeof out);irq=initial;writes=0;copies=0;
  CAN_Diagnostics_GetSnapshot(&out);
  assert(irq==initial && writes==2 && copies==1);
  assert(!memcmp(&out,(const void*)&g_can_diag,sizeof out));
  writes=0;copies=0;CAN_Diagnostics_GetSnapshot(NULL);
  assert(irq==initial && writes==0 && copies==0);
 }
 puts("PASS: HC code equality; PRIMASK 0/1 restored, copy under IRQ mask, complete snapshot, NULL has no effects");
}
