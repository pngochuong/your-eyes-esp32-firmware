#ifndef CAMERA_TUNING_H
#define CAMERA_TUNING_H

// ============================================================================
// Profile cam bien cho OCR + mo ta canh
// ============================================================================
// Moi quyet dinh "vi sao dat con so nay" nam trong camera_tuning.cpp, ngay
// canh dong ma dat no. Khong khoi tao phan cung o day — do la viec cua
// camera_device.*.
// ============================================================================
#include <Arduino.h>
#include "esp_camera.h"

// Nap toan bo profile vao cam bien: do phan giai, mau, do net, phoi sang, gain,
// sua loi diem anh va huong hinh.
void cameraApplySettings(sensor_t *sensor);

// Bo `frameCount` khung dau de AEC va AWB on dinh. Nhan tien bat luon cac dau
// hieu duong truyen camera co van de.
void cameraWarmUp(uint8_t frameCount);

// In lai profile dang ap dung, de doc log la biet may dang chay o cau hinh nao.
void cameraPrintSettings();

#endif  // CAMERA_TUNING_H
