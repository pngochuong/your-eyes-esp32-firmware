#ifndef AUDIO_SERVICE_H
#define AUDIO_SERVICE_H

// ============================================================================
// Dieu phoi mot lan hoi dap: nut -> anh -> tieng -> server -> loa
// ============================================================================
// Day la file duy nhat biet TRINH TU. Cac khoi ben duoi (camera_capture,
// audio_recorder, audio_dsp, api_client, audio_player) khong biet gi ve nhau.
//
// Toan bo chay trong task "audio" ghim o core 1. Core 0 lo WiFi/TCP + httpd,
// nen stream camera khong khung trong luc thu/gui/phat.
// ============================================================================
#include <Arduino.h>

// Xin bo dem, mo I2S, dung task "audio" len.
// Goi SAU startCameraServer() de camera va httpd chiem RAM truoc; phan PSRAM
// con lai moi la phan audio that su duoc dung.
// Tra ve false neu khong khoi dong duoc — camera van chay binh thuong.
bool startAudio();

#endif  // AUDIO_SERVICE_H
