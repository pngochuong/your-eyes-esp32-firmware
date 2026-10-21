#ifndef DEVICE_SESSION_H
#define DEVICE_SESSION_H

// ============================================================================
// Phien WebSocket voi server: WS /v2/session — MOT socket cho ca thoi gian song
// ============================================================================
// Hop dong day du: FIRMWARE_ENDPOINTS.md. Khoi nay lo dung phan "giao thuc":
//   - mo socket, gui `hello` (kem token tu NVS), cho `hello_ok`;
//   - `ping` moi 25 s ke ca khi ranh;
//   - rot thi mo lai theo backoff 0.5 -> 1 -> 2 -> 4 -> 8 s (+-20%);
//     dong 4000 (phien moi thay the) / 4401 (sai token) thi KHONG tu mo lai;
//   - cat khung server gui thanh su kien roi goi cac ham trong SessionHandlers;
//   - xep hang moi thu kinh gui len, gui DUNG thu tu:
//       turn_start -> 0x01 anh -> 0x03 tieng (theo luong) -> turn_end
//   - cuoc goi (muc 16): bao `call_state`, gui `call_answer`/`call_hangup`,
//     day mic 0x05 tu mot vong tron — xen vao giua moi thu khac.
//
// Khoi nay KHONG phat tieng, KHONG chup anh, KHONG biet nut bam. audio_service
// dang ky handler va goi cac ham gui — moi phoi hop nam o do.
//
// Moi I/O socket chay trong MOT task "wssess" ghim core 0 (ws_client khong
// thread-safe). Ham gui chi xep hang roi tra ve; handler chay TRONG task
// "wssess" nen phai ngan va khong duoc chan.
// ============================================================================
#include <Arduino.h>

struct SessionReply {
  const char *turn;
  const char *cues[8];
  int         nCues;
  const char *live;        // "ocr" / "nav" / "" (null)
  bool        hasAudio;    // reply.audio khac null
  uint16_t    prerollMs;   // reply.audio.preroll_s
};

struct SessionHandlers {
  void (*onReady)();                                    // vua nhan hello_ok
  void (*onDown)();                                     // socket vua rot
  void (*onReply)(const SessionReply &r);
  void (*onAudio)(const uint8_t *p, size_t n);          // payload 0x02, tung mieng
  void (*onReplyEnd)(const char *turn);
  void (*onHazard)(int level, const char *dir, const char *kind);
  void (*onCue)(const char *id);                        // khung `cue` cua live
  void (*onCam)(const char *size, int fps, int quality);
  void (*onCapture)(const char *size);
  void (*onLiveEnd)();
  void (*onError)(const char *code);
  // `state`: ringing_out / ringing_in / active / ended. `reason` chi co khi
  // ended (rong neu khong co). `call` co the doi tu id tam sang id that.
  void (*onCallState)(const char *call, const char *state, const char *peer,
                      const char *reason);
};

// Dang ky handler, mo socket LAN DAU ngay tren task goi (chan toi khi xong
// hoac hong), roi dung task "wssess" lo phan con lai.
//
// 🔴 Goi TRUOC startCameraServer() va startAudio(). mbedtls can ~45 KB RAM noi
// LIEN cho mot phien, va do that: sau khi httpd + audio cap phat thi khoi lien
// lon nhat chi con 31-35 KB — bat tay chi thanh cong luc RAM con thoang. Phien
// WS duoc GIU suot, nen lan bat tay dat gia nay chi tra mot lan.
void sessionBegin(const SessionHandlers &h);

// Da nhan hello_ok, gui duoc turn_start.
bool sessionReady();

// Bo backoff, mo lai NGAY. Dung khi nguoi dung bam nut luc mat ket noi (ke ca
// sau 4000/4401 — tu mo lai thi cam, nguoi dung bam thi khong phai "tu"), va
// sau khi vua nap token moi.
void sessionKick();

// So lan mo lien tiep that bai (reset khi co hello_ok).
uint16_t sessionFailStreak();

// In trang thai phien. Chi phuc vu lenh chan doan.
void sessionPrintStatus();

// ---- gui len — goi tu task audio, deu tra ve tuc thi
bool sessionTurnStart(const char *turn, unsigned long tPressMs);

// Anh JPEG cua luot. `jpg` phai con song toi khi sessionUplinkIdle() = true —
// task mang doc thang, khong sao chep.
void sessionImage(const uint8_t *jpg, size_t len);

// Mo luong tieng 0x03. `buf` la bo dem ADPCM ma task audio nen dan vao; task
// mang day [da gui, totalBytes) moi khi co them. Giong y het duong POST cu.
void sessionAudioOpen(const uint8_t *buf);
// `totalBytes` tinh tu dau `buf`, chi tang. `done` o lan cuoi.
void sessionAudioPush(size_t totalBytes, bool done);

bool sessionTurnEnd(const char *turn, unsigned long tReleaseMs);
void sessionPlayed(const char *turn, unsigned long firstByteMs,
                   unsigned long firstAudioMs);
void sessionCancel();

// ---- cuoc goi (muc 16)
// Bam nut khi ringing_in = nghe; khi ringing_out / active = cup (ke ca dung
// chuoi khan cap). Khong bao gio gui turn_start trong luc goi.
void sessionCallAnswer(const char *call);
void sessionCallHangup(const char *call);

// Mic cuoc goi 0x05. `ring` la vung nho cua task audio, task mang doc thang
// tu do — dung lai duoc sau khi sessionUplinkIdle() = true.
void sessionCallMicOpen(uint8_t *ring, size_t cap);
// Chep mot khoi ADPCM vao vong. false = mang khong kip, khoi bi bo.
bool sessionCallMicPush(const uint8_t *p, size_t n);
void sessionCallMicClose();

// Hang gui rong, khong con anh, tieng hay mic cuoc goi nao dang day — bo dem
// dung lai an toan.
bool sessionUplinkIdle();

// Gui mot khung live 0x04. CHAN toi khi gui xong, bi bo, hoac het `waitMs`
// ma task mang chua nhan. Tra ve true khi da gui.
bool sessionLiveFrame(const uint8_t *jpg, size_t len, unsigned long waitMs);

#endif  // DEVICE_SESSION_H
