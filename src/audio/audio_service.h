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

// Chay tron mot lan hoi dap nhu vua bam nut, nhung khong can cham vao nut.
//
// Ly do ton tai: moi phep do end-to-end truoc day deu phai co nguoi dung ngoi
// canh board de bam. Chan doan qua Serial tu xa vi the khong cham toi duoc
// chang nang nhat (gui anh + am len server) — dung chang hay hong nhat. Ham
// nay di ĐUNG trinh tu cua audioTask, khong phai duong tat, nen so lieu no in
// ra so sanh truc tiep duoc voi mot lan bam that.
//
// Goi tu consolePoll(), tuc dang o trong task "audio" — khong duoc goi tu
// task khac, vi I2S va bo dem thu deu thuoc ve task nay.
void audioSimulatePress(unsigned long holdMs);

#endif  // AUDIO_SERVICE_H
