#ifndef DEVICE_TOKEN_H
#define DEVICE_TOKEN_H

// ============================================================================
// Token cua kinh — nam trong NVS, KHONG nam trong ma nguon
// ============================================================================
// Server doc VISIONCARE_DEVICE_CLIENT_TOKEN va doi kinh gui dung token do
// trong `hello` (FIRMWARE_ENDPOINTS muc 2). Spec cam ba thu: bien dich cung
// vao firmware, in ra log/Serial, dat trong URL.
//
// Nap mot lan qua Serial (lenh '=' trong serial_console, hoac
// tools/nap_token.ps1 doc thang tu .env), roi no song qua moi lan nap lai
// firmware — NVS nam o phan vung rieng, khong bi ghi de khi upload.
//
// Khoi nay chi doc/ghi NVS. Khong biet ai dung token.
// ============================================================================
#include <Arduino.h>

// Tran do dai token. Token sinh bang secrets.token_urlsafe(32) dai 43 ky tu;
// de rong gap ba cho token kieu khac.
#define DEVICE_TOKEN_MAX 128

// Doc token vao `out` (cap >= DEVICE_TOKEN_MAX + 1). Tra ve false neu chua co.
bool tokenLoad(char *out, size_t cap);

// Ghi token moi. Rong = xoa. Tra ve false neu NVS hong hoac qua dai.
bool tokenSave(const char *token);

#endif  // DEVICE_TOKEN_H
