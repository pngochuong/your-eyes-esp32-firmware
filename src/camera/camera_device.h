#ifndef CAMERA_DEVICE_H
#define CAMERA_DEVICE_H

// ============================================================================
// Vong doi phan cung camera
// ============================================================================
// Chi lo mot viec: dung camera_config_t, bat camera len, tat di, bat lai.
// Profile cam bien nam o camera_tuning.*, chup anh nam o camera_capture.*.
// ============================================================================
#include <Arduino.h>
#include "esp_camera.h"

// Bat camera va dua no vao trang thai san sang: dung config, esp_camera_init(),
// nap profile cam bien, bo vai khung dau cho AEC/AWB hoi tu, in thong tin.
// Tra ve false neu khong khoi tao duoc — luc do khong nen dung web server len.
bool cameraStart();

// Khoi tao lai sau esp_camera_deinit(). Phep thu chan doan nhieu can tat camera
// roi bat lai; nho co ham nay ma cau hinh khong phai chep ra lam hai ban.
bool cameraReinit();

// esp_camera_init() thanh cong hay chua. loop() dua vao co nay de khong dung
// web server len khi khong co camera.
bool cameraIsReady();

// Do phan giai va muc nen DANG dung. Day la NGUON DUY NHAT cua hai con so do:
// camera_config_t, profile cam bien va cac dong log deu doc tu day, nen khong
// con canh config mot dang, sensor mot neo.
framesize_t cameraFrameSize();
int         cameraJpegQuality();

// Ten hien thi cua do phan giai, chi dung cho log.
const char *cameraFrameSizeName(framesize_t size);

// ============================================================================
// Doi che do chup luc dang chay — cho live (OCR / chi duong) cua server
// ============================================================================
// Server ra lenh `cam` (vga 5 fps, qvga 8 fps...) va `capture` (uxga), trong
// khi anh cua mot luot hoi van can HD. Mot camera, hai nguoi dung, nen:
//   - ai chup cung phai cameraLock() truoc, cameraUnlock() sau;
//   - ai can do phan giai nao thi tu cameraSetMode() ngay sau khi khoa.
// Doi giua chung khong lam hong ai: nguoi kia khoa lai se tu dat lai che do.

// Khoa de quy: cung mot task khoa long nhau duoc.
void cameraLock();
void cameraUnlock();

// Dat do phan giai + muc nen. Lon hon co da cap phat luc init thi bi kep lai
// (frame buffer khong chua noi) va in mot dong. Tra ve do phan giai THAT da dat.
framesize_t cameraSetMode(framesize_t fs, int quality);

// Quay ve che do cua anh luot hoi (cameraFrameSize / cameraJpegQuality).
void cameraUseDefault();

// Doc rong x cao THAT tu marker SOF cua mot file JPEG. false neu khong doc
// duoc. Khac fb->width: cai do la kich thuoc DA CAU HINH, khong phai du lieu.
bool jpegSize(const uint8_t *p, size_t len, uint16_t *w, uint16_t *h);

// "qvga", "vga", "svga", "hd", "sxga", "uxga"... -> framesize_t.
// Khong nhan ra thi tra `fallback`.
framesize_t cameraParseSize(const char *name, framesize_t fallback);

#endif  // CAMERA_DEVICE_H
