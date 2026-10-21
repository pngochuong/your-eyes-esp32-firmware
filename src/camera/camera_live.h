#ifndef CAMERA_LIVE_H
#define CAMERA_LIVE_H

// ============================================================================
// Live: chup lien tuc / chup mot anh theo lenh cua server
// ============================================================================
// Server dieu khien hoan toan (FIRMWARE_ENDPOINTS muc 6) — kinh chi lam DUNG
// lenh vua nhan:
//   `cam`     {"size":"vga","fps":5,"quality":12}  -> chup lien tuc, gui 0x04
//   `capture` {"size":"uxga"}                      -> chup MOT anh, gui 0x04
//   `end`                                          -> dung, tra camera
//
// Khoi nay chi lo CHUP va giu nhip fps. Byte di dau thi no khong biet: nguoi
// goi dua ham `send` vao (audio_service noi no voi device_session). Nho vay
// camera khong phai biet gi ve WebSocket.
//
// Chay trong task "live" rieng, ghim core 0. Anh cua luot hoi (photoCapture)
// van chup duoc trong luc live chay — hai ben tranh nhau qua cameraLock().
// ============================================================================
#include <Arduino.h>
#include "esp_camera.h"

// Gui mot anh JPEG. Chan cho toi khi gui xong hoac bo. Tra ve false = khung
// bi bo (duong len dang ban) — live chup khung moi o nhip sau, khong thu lai.
typedef bool (*LiveSendFn)(const uint8_t *jpg, size_t len);

enum LiveKind : uint8_t { LIVE_NONE, LIVE_OCR, LIVE_NAV };

// Dung task. Goi mot lan, sau cameraStart().
bool liveBegin(LiveSendFn send);

// Lenh `cam`. `kind` chi de log va de liveOnPress() biet co nen dung khong.
void liveCam(LiveKind kind, framesize_t fs, int fps, int quality);

// Lenh `capture`: chup mot anh o `fs` roi quay lai nhu cu.
void liveCapture(framesize_t fs);

// Lenh `end`, hoac socket rot: dung gui, tra camera ve HD.
void liveStop();

// Nguoi dung vua bam nut. OCR live bi luot moi huy (dung gui 0x04 ngay);
// nav live thi SONG qua luot moi — canh bao vat can van phai den.
void liveOnPress();

// Tam dung / chay lai ma KHONG quen lenh `cam` dang co. Dung trong cuoc goi
// (muc 16.2): server tam dung nav va tu bat lai khi cup, khong gui lai `cam`
// — dung han bang liveStop() la mat canh bao vat can sau cuoc goi.
void livePause(bool paused);

LiveKind liveKind();

// In che do live + so khung da gui. Chi phuc vu lenh chan doan.
void livePrintStatus();

#endif  // CAMERA_LIVE_H
