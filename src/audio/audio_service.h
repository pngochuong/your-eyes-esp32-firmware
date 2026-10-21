#ifndef AUDIO_SERVICE_H
#define AUDIO_SERVICE_H

// ============================================================================
// Dieu phoi: nut -> anh -> tieng -> phien WS -> loa, va live -> camera
// ============================================================================
// Day la file duy nhat biet TRINH TU. Cac khoi ben duoi (camera_capture,
// camera_live, audio_recorder, audio_dsp, device_session, play_queue,
// audio_player) khong biet gi ve nhau — moi day noi giua chung nam o day.
//
// Mot luot hoi (FIRMWARE_ENDPOINTS muc 1, buoc 7-13):
//   bam   -> dung phat, turn_start, chup, 0x01 anh, 0x03 tieng theo luong
//   nha   -> turn_end
//   server tra reply/0x02/reply_end bat ky luc nao -> xep vao play_queue,
//   task audio phat khi ranh, bao `played`.
//
// Cuoc goi (muc 16): `call_state` doi nghia cua nut — ringing_in: bam = nghe,
// ringing_out/active: bam = cup. Luc `active` task audio vao vong song cong
// (mic 0x05 + loa 0x02 cung luc), tam dung live, khong gui turn_start.
//
// Task "audio" ghim core 1 lo nut + mic + loa. Task "wssess" (core 0) lo
// socket; cac handler cua no chi xep hang, khong cham I2S.
// ============================================================================
#include <Arduino.h>

// Dang ky handler va mo phien WS lan dau. Goi NGAY sau wifiConnect(), TRUOC
// startCameraServer() va startAudio() — xem ghi chu o sessionBegin().
// Mat Wi-Fi luc khoi dong cung goi: task phien tu cho mang roi tu noi.
void startSession();

// Xin bo dem, mo I2S, dung task "audio" + task "live".
// Goi SAU startCameraServer() de camera va httpd chiem RAM truoc.
// Tra ve false neu khong khoi dong duoc — camera van chay binh thuong.
bool startAudio();

// Chay tron mot luot nhu vua giu nut `holdMs`, khong can cham nut. Di DUNG
// trinh tu cua luot that (chi thay dieu kien dung thu am), nen so lieu in ra
// so sanh truc tiep duoc.
//
// Goi tu consolePoll(), tuc dang o trong task "audio".
void audioSimulatePress(unsigned long holdMs);

#endif  // AUDIO_SERVICE_H
