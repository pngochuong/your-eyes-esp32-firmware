#ifndef CAMERA_CAPTURE_H
#define CAMERA_CAPTURE_H

// ============================================================================
// Chup anh de gui di
// ============================================================================
// Khac hoan toan voi duong /capture cua web server: o day chup mot LOAT khung
// roi giu khung net nhat, va giu ban sao trong PSRAM de con thu am va gui di
// ma khong chiem frame buffer cua camera.
// ============================================================================
#include <Arduino.h>

// Chuan bi chan den chieu sang. Goi mot lan luc khoi dong.
void flashLedBegin();

// Bat/tat den chieu sang. Khong lam gi neu FLASH_LED_PIN < 0.
void flashLed(bool on);

// Chup mot loat, giu khung net nhat vao bo dem trong. Tra ve false neu khong
// lay duoc khung nao. Anh cu (neu con) bi giai phong ngay dau ham.
bool photoCapture();

// Ban sao JPEG cua lan chup gan nhat. photoData() tra NULL khi chua co anh.
const uint8_t *photoData();
size_t         photoSize();

// Tra lai RAM cua anh dang giu.
void photoRelease();

// Do do net cua mot anh JPEG. Tra ve -1 neu khong do duoc.
// Ton ~200 ms (phai giai nen), nen chi dung khi chan doan.
float photoMeasureSharpness(const uint8_t *buf, size_t len, int w, int h);

#endif  // CAMERA_CAPTURE_H
