#include "scan_controller.h"
#include <assert.h>
#include <stdio.h>

// --- Repeated requests after timeout, malformed reply and successful decode ---
int main(void)
{
    ScanCtrl_Init();
    assert(ScanCtrl_StartScan(10));
    assert(!ScanCtrl_StartScan(10));
    assert(ScanCtrl_HandleTimeout());
    assert(ScanCtrl_GetState() == SCAN_STATE_ERROR);
    assert(ScanCtrl_StartScan(10));
    const uint8_t invalid[] = {3, 0xE4, 4, 0, 0x15, 0xFF};
    assert(!ScanCtrl_ProcessReceived(invalid, sizeof invalid));
    assert(ScanCtrl_StartScan(10));
    const uint8_t type_only[] = {5, 0xF3, 0, 0, 'A', 0xFE, 0xC7};
    assert(!ScanCtrl_ProcessReceived(type_only, sizeof type_only));
    assert(ScanCtrl_GetState() == SCAN_STATE_ERROR);
    assert(ScanCtrl_StartScan(10));
    const uint8_t barcode[] = {6, 0xF3, 0, 0, 0, 'A', 0xFE, 0xC6};
    assert(ScanCtrl_ProcessReceived(barcode, sizeof barcode));
    assert(ScanCtrl_GetState() == SCAN_STATE_DATA_READY);
    assert(ScanCtrl_StartScan(10));
    uint8_t saved[SHADOW_BUF_SIZE], length;
    assert(ScanCtrl_GetLastBarcode(saved, &length));
    assert(length == 1 && saved[0] == 'A');
    assert(!ScanCtrl_StartScan(10));
    ScanCtrl_HandleTransportError();
    assert(ScanCtrl_GetState() == SCAN_STATE_ERROR);
    assert(ScanCtrl_StartScan(10));
    ScanStats_t stats;
    ScanCtrl_GetStats(&stats);
    assert(stats.scan_timeouts == 1 && stats.ssi_decode_errors == 2);
    assert(stats.scan_success == 1);
    puts("Scan controller retry: PASS");
    return 0;
}
