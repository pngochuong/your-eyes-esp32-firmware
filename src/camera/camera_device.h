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

#endif  // CAMERA_DEVICE_H
