// ============================================================================
// 🔴 Cac file nay la .cpp, KHONG phai .ino — co ly do. Arduino tu sinh
// prototype cho moi ham trong tab .ino roi chen len DAU file gop, tuc la
// truoc dong #include <WiFiClientSecure.h>. Luc do kieu WiFiClientSecure chua
// ton tai -> "redeclared as different kind of entity". File .cpp khong bi
// Arduino dong vao, nen khong dinh. Doi lai phai tu #include <Arduino.h> vi
// .cpp khong duoc chen san.
// ============================================================================
#include "audio_service.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_random.h>

#include "adpcm_codec.h"
#include "audio_buffers.h"
#include "audio_cues.h"
#include "audio_dsp.h"
#include "audio_i2s.h"
#include "audio_player.h"
#include "audio_recorder.h"
#include "play_queue.h"
#include "../camera/camera_capture.h"
#include "../camera/camera_device.h"
#include "../camera/camera_live.h"
#include "../diag/serial_console.h"
#include "../net/device_session.h"
#include "../util/perf_probe.h"
#include "../../app_config.h"

// Bao lau sau turn_end ma chua co `reply` thi keu loi cho nguoi dung bam lai.
// E2E do duoc: nha nut -> tieng dau 502-560 ms (FIRMWARE_ENDPOINTS muc 14).
// 15 s du rong cho mang cham va lan server nghi lau, ma van du som de nguoi
// dung khiem thi khong dung do khong biet may con song khong.
static const unsigned long REPLY_TIMEOUT_MS = 15000;

// Truoc khi dung lai bo dem anh/tieng cho luot moi, duong len cua luot cu phai
// xong. Tren duong cham (hotspot 9 KB/s) anh 60 KB mat ~7 s; qua tran nay thi
// bo luot moi va keu loi, khong ghi de bo dem dang duoc gui.
static const unsigned long UPLINK_WAIT_MS = 8000;

// 🔴 Khoi dong lai khi TLS khong bat tay noi vi het RAM noi LIEN.
// Do that tren board (2026-08-27): mot phien mbedtls doi ~45 KB lien, va sau
// khi httpd + audio da cap phat thi khoi lien lon nhat chi con 31-35 KB — phien
// dau tien (mo luc khoi dong) chay duoc, nhung rot roi thi KHONG mo lai duoc nua
// cho toi khi cam lai nguon. Voi thiet bi "bat nguon la chay", cam lai nguon la
// viec nguoi dung khiem thi khong tu biet de lam. Nen tu lam, nhung chi khi:
//   - mo hong lien tiep RESTART_FAILS lan (khong phai mot lan truot mang),
//   - va khoi lien lon nhat that su duoi nguong (khong phai mang hong),
//   - va dang ranh (khong giua luot, khong dang phat).
#define RESTART_ON_TLS_STARVE  API_USE_TLS
static const uint16_t RESTART_FAILS     = 6;
static const size_t   TLS_MIN_BLOCK     = 48 * 1024;

// ============================================================================
// Nen ban thu thanh ADPCM va day len NGAY trong luc dang thu
// ============================================================================
// Ban thu di len thanh khung 0x03 ADPCM THO, khong header RIFF (muc 10): cu du
// 505 mau la nen ra mot khoi 256 byte va giao ngay cho task mang. Luc nha nut
// chi con khoi cuoi.
static AdpcmEnc g_enc;
static size_t   g_encBytes = 0;   // so byte ADPCM da nen vao sendBuf
static size_t   g_pendSamp = 0;   // mau dau tien cua recBuf CHUA duoc nen

static void encodeBegin() {
  adpcmEncReset(g_enc);
  dspStreamBegin();
  g_encBytes = 0;
  g_pendSamp = 0;
}

// Nen het cac khoi DAY co the nen tu so mau dang co, roi bao cho task mang.
// `flush` = thu xong roi, nen not phan le thanh mot khoi cuoi.
static void encodeUpTo(size_t haveSamp, bool flush) {
  if (!sendBuf) return;

  while (g_pendSamp < haveSamp) {
    size_t n = haveSamp - g_pendSamp;
    if (n > (size_t)ADPCM_BLOCK_SAMPLES) n = ADPCM_BLOCK_SAMPLES;
    // Chua du mot khoi va cung chua thu xong -> de danh cho lan sau.
    if (n < (size_t)ADPCM_BLOCK_SAMPLES && !flush) break;

    if (g_encBytes + ADPCM_BLOCK_BYTES > sendCap) {
      Serial.println("Bo dem gui day — dung nen");
      break;
    }
    g_encBytes += adpcmEncodeBlock(g_enc, recBuf + g_pendSamp, n,
                                   sendBuf + g_encBytes);
    g_pendSamp += n;
  }
  sessionAudioPush(g_encBytes, flush);
}

// Ham goi lai cua recordWhileHeld()/recordFixed(): loc roi nen ngay mieng vua
// doc duoc, giua hai lan doc mic.
static void onRecChunk(size_t from, size_t count) {
  dspStreamPrep(from, count);
  encodeUpTo(from + count, false);
}

static size_t encodeFinish(size_t pcmBytes) {
  encodeUpTo(pcmBytes / sizeof(int16_t), true);
  Serial.printf("Nen ADPCM: %u -> %u byte\n", (unsigned)pcmBytes, (unsigned)g_encBytes);
  return g_encBytes;
}

// ============================================================================
// Handler cua phien — chay trong task "wssess", chi xep hang
// ============================================================================
// Luot dang cho tra loi. Task audio ghi s_waitTurn TRUOC khi gui turn_start,
// nen luc reply toi thi chuoi da on dinh.
static char          s_waitTurn[24] = "";
static volatile bool s_waiting      = false;
static volatile bool s_gotReply     = false;
static volatile bool s_rejected     = false;   // error rate_limited cho luot nay
static volatile bool s_lost         = false;   // socket rot giua luc cho
static unsigned long s_waitSince    = 0;

// 🔴 Socket rot thi phai bo moi tieng dang cho — nhung pqFlush() chi duoc chay
// tren task DOC (task audio): neu task mang xoa giua luc task audio dang
// pqRead() thi vi tri doc bi ghi lui va doan cu phat lai. Nen task mang chi
// dat co, task audio tu xoa.
static volatile bool s_dropAll = false;

// Moc first_byte_ms cua reply dang nhan (task mang ghi va doc).
static unsigned long s_firstByte = 0;

// 🔴 Khung cua luot CU con dang bay khi nguoi dung bam lan moi. Server huy luot
// cu khi nhan turn_start, nhung nhung gi no da gui TRUOC do van toi kinh SAU
// pqFlush() — khong chan thi cau tra loi cu phat chen vao sau cau hoi moi.
// Chi chan reply cua luot do KINH dat id (bat dau bang 't') ma khong phai luot
// hien tai; `result-*` va cau noi cua live van phai di qua.
static char          s_curTurn[24] = "";
static volatile bool s_skipping    = false;

// `reply.live` cua reply gan nhat — quyet dinh `cam` ke tiep la OCR hay nav.
static volatile LiveKind s_pendingLive = LIVE_NONE;

// ---- cuoc goi (muc 16). Task mang ghi theo `call_state`, task audio doc de
// biet nut bam nghia la gi va khi nao vao vong song cong.
enum CallSt : uint8_t { CALL_NONE, CALL_RING_IN, CALL_RING_OUT, CALL_ACTIVE };
static volatile CallSt s_call = CALL_NONE;
static char            s_callId[64] = "";
static portMUX_TYPE    s_callMux = portMUX_INITIALIZER_UNLOCKED;
// Kinh vua gui call_hangup. Server chua kip xu ly thi van gui tieng cuoc goi
// them mot nhip — bo di, va khong quay lai vong goi cho toi khi server bao
// trang thai moi. Co han: server khong tra gi thi sau HANGUP_MUTE_MS nghe lai.
static volatile bool   s_hangupSent = false;
static unsigned long   s_hangupAt   = 0;
static const unsigned long HANGUP_MUTE_MS = 3000;

static bool hangupMuting() {
  return s_hangupSent && millis() - s_hangupAt < HANGUP_MUTE_MS;
}

static void hReady() {
  Serial.println("San sang — giu nut de hoi");
}

static void hDown() {
  // Muc 1, buoc 16: dung phat, tat camera live, bo luot do dang.
  s_dropAll  = true;
  s_skipping = false;
  liveStop();
  if (s_waiting) s_lost = true;
  // Muc 16.2: rot phien thi server cup cuoc goi thuong. Chuoi khan cap van
  // chay — server gui lai `call_state` ngay sau hello_ok, luc do moi vao lai.
  s_call       = CALL_NONE;
  s_hangupSent = false;
}

static void hReply(const SessionReply &r) {
  bool ours  = r.turn[0] == 't';            // id kinh tu sinh: "t%04x-%lu"
  s_skipping = ours && strcmp(r.turn, s_curTurn) != 0;
  if (s_skipping) {
    Serial.printf("<< reply %s cua luot cu — bo\n", r.turn);
    return;
  }
  if (s_waiting && strcmp(r.turn, s_waitTurn) == 0) s_gotReply = true;
  Serial.printf("<< reply %s: cue [", r.turn);
  for (int i = 0; i < r.nCues; i++) Serial.printf("%s%s", i ? " " : "", r.cues[i]);
  Serial.printf("], live=%s, audio=%s\n",
                r.live[0] ? r.live : "null", r.hasAudio ? "co" : "khong");

  if (!strcmp(r.live, "ocr"))      s_pendingLive = LIVE_OCR;
  else if (!strcmp(r.live, "nav")) s_pendingLive = LIVE_NAV;

  s_firstByte = 0;
  PqEvent e = {};
  e.type        = PQ_REPLY;
  e.hasAudio    = r.hasAudio;
  e.prerollMs   = r.prerollMs;
  e.firstByteMs = millis();
  strncpy(e.turn, r.turn, sizeof(e.turn) - 1);
  pqPushEvent(e);

  // Cue phat tu flash NGAY tai day trong luong — truoc moi khoi 0x02 cua reply
  // nay, vi reply luon toi truoc khoi cua no (muc 1, buoc 11).
  for (int i = 0; i < r.nCues; i++) {
    PqEvent c = {};
    c.type = PQ_CUE;
    strncpy(c.cue, r.cues[i], sizeof(c.cue) - 1);
    pqPushEvent(c);
  }
}

static void hAudio(const uint8_t *p, size_t n) {
  if (s_skipping || hangupMuting()) return;
  if (!s_firstByte) s_firstByte = millis();
  pqPushAudio(p, n);
}

static void hReplyEnd(const char *turn) {
  if (s_skipping) { s_skipping = false; return; }
  PqEvent e = {};
  e.type        = PQ_REPLY_END;
  e.firstByteMs = s_firstByte;
  strncpy(e.turn, turn, sizeof(e.turn) - 1);
  pqPushEvent(e);
  s_firstByte = 0;
}

static void hHazard(int level, const char *dir, const char *kind) {
  // Muc 6: phat hazard_<level>; muc >= 2 them dir_*, muc 3 them stop_now.
  // Server da chong lam phien — kinh phat MOI khung nhan duoc.
  if (level < 1) level = 1;
  if (level > 3) level = 3;
  Serial.printf("<< HAZARD muc %d, %s, %s\n", level, dir, kind);
  char id[24];
  snprintf(id, sizeof(id), "hazard_%d", level);
  pqUrgent(id);
  if (level >= 2 && dir[0]) {
    snprintf(id, sizeof(id), "dir_%s", dir);
    pqUrgent(id);
  }
  if (level >= 3) pqUrgent("stop_now");
}

static void hCue(const char *id) {
  // Cue cua live (huong dan can chu) — keu ngay, khong xep sau cau dang doc.
  pqUrgent(id);
}

static void hCam(const char *size, int fps, int quality) {
  // `cam` dau tien sau mot reply co live -> dung kieu reply do noi. Nhung
  // `cam` sau do (OCR xong, server tu dung lai nav) khong co reply nao di
  // kem — doan theo kich thuoc: nav chay qvga, OCR chay vga (muc 6).
  LiveKind k = s_pendingLive;
  s_pendingLive = LIVE_NONE;
  if (k == LIVE_NONE) k = (!strcasecmp(size, "qvga") || !strcasecmp(size, "qqvga")) ? LIVE_NAV : LIVE_OCR;
  liveCam(k, cameraParseSize(size, FRAMESIZE_VGA), fps, quality);
}

static void hCapture(const char *size) {
  liveCapture(cameraParseSize(size, FRAMESIZE_UXGA));
}

static void hLiveEnd() {
  liveStop();
}

static void hError(const char *code) {
  if (!strcmp(code, "rate_limited")) {
    // Muc 11: bao bang cue cuc bo, khong thu lai don dap. Luot vua gui bi bo.
    pqUrgent("system_busy");
    if (s_waiting) s_rejected = true;
  }
}

static void hCallState(const char *call, const char *state, const char *peer,
                       const char *reason) {
  CallSt st = !strcmp(state, "ringing_in")  ? CALL_RING_IN
            : !strcmp(state, "ringing_out") ? CALL_RING_OUT
            : !strcmp(state, "active")      ? CALL_ACTIVE
            :                                 CALL_NONE;      // ended / la
  if (call[0]) {
    // Id co the doi tu id tam sang id that — luon giu cai moi nhat.
    portENTER_CRITICAL(&s_callMux);
    strncpy(s_callId, call, sizeof(s_callId) - 1);
    s_callId[sizeof(s_callId) - 1] = 0;
    portEXIT_CRITICAL(&s_callMux);
  }
  s_hangupSent = false;
  s_call       = st;
  Serial.printf("<< CUOC GOI %s: %s%s%s%s\n", peer[0] ? peer : "?", state,
                reason[0] ? " (" : "", reason, reason[0] ? ")" : "");
}

static bool liveSend(const uint8_t *jpg, size_t len) {
  return sessionLiveFrame(jpg, len, 2000);
}

static void onPlayed(const char *turn, unsigned long firstByteMs, unsigned long firstAudioMs) {
  sessionPlayed(turn, firstByteMs ? firstByteMs : firstAudioMs, firstAudioMs);
}

void startSession() {
  SessionHandlers h = {};
  h.onReady    = hReady;
  h.onDown     = hDown;
  h.onReply    = hReply;
  h.onAudio    = hAudio;
  h.onReplyEnd = hReplyEnd;
  h.onHazard   = hHazard;
  h.onCue      = hCue;
  h.onCam      = hCam;
  h.onCapture  = hCapture;
  h.onLiveEnd  = hLiveEnd;
  h.onError    = hError;
  h.onCallState = hCallState;
  sessionBegin(h);
}

// ============================================================================
// Mot luot hoi
// ============================================================================
static bool btnDown() { return digitalRead(BTN) == BTN_ACTIVE; }

// Luc nut nha han lan truoc. Bam lai som hon PRESS_GAP_MS thi chua tinh — xem
// waitRelease().
static unsigned long s_releasedAt = 0;
static const unsigned long PRESS_GAP_MS = 250;

// Ham dung cua playerPump(): nguoi dung bam nut, socket vua rot, hoac cuoc goi
// vua noi may (server da tu dung moi cau dang doc — muc 16.2).
static bool stopPlayback() {
  return btnDown() || s_dropAll || (s_call == CALL_ACTIVE && !s_hangupSent);
}

static void waitRelease() {
  // 🔴 Vong nay TUNG khong co gioi han. Khi chan nut chap xuong GND that thi no
  // cho vinh vien: thiet bi cam tit, khong log gi, nhin giong hong hoan toan.
  const unsigned long STUCK_MS = 15000;
  unsigned long t = millis();
  // 🔴 "Da nha" = nut o muc cao LIEN TUC RELEASE_STABLE_MS, khong phai mot lan
  // doc. Do that tren board (2026-10-20): 5 luot trong 1.5 s, moi luot thu
  // 0-500 ms — nut doi luc nha, moi lan doi la mot turn_start moi, an vao tran
  // 30 luot/phut cua server.
  const unsigned long RELEASE_STABLE_MS = 60;
  unsigned long tHigh = 0;
  for (;;) {
    if (btnDown()) {
      tHigh = 0;
      if (millis() - t > STUCK_MS) {
        Serial.printf("LOI PHAN CUNG: GPIO%d o muc thap lien tuc %lu s.\n",
                      BTN, STUCK_MS / 1000);
        Serial.println("Nut bi ket, chap, hoac day tin hieu cham GND.");
        break;
      }
    } else {
      if (!tHigh) tHigh = millis();
      if (millis() - tHigh >= RELEASE_STABLE_MS) break;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  s_releasedAt = millis();
}

static void newTurnId(char *out, size_t cap) {
  // Kinh tu sinh id (muc 3). Ghep mot tem ngau nhien luc khoi dong de id cua
  // lan bat may truoc khong trung id cua lan nay trong metrics cua server.
  static uint16_t boot = (uint16_t)esp_random();
  static uint32_t n = 0;
  snprintf(out, cap, "t%04x-%lu", boot, (unsigned long)++n);
}

static void runTurn(bool simulated, unsigned long holdMs) {
  perfBegin(simulated ? "GIA LAP MOT LUOT" : "MOT LUOT BAM NUT");
  Serial.println(simulated ? "\n===== GIA LAP BAM NUT =====" : "\n===== BAM NUT =====");

  // Muc 1, buoc 7: dung moi thu dang phat. Server cung bo cau dang doc khi
  // nhan turn_start (muc 4.2), nen phan con trong hang la phan bo di.
  pqFlush();
  liveOnPress();

  if (!sessionReady()) {
    Serial.println("Chua co phien voi server — keu loi, thu mo lai ngay");
    // Bam nut la nguoi dung chu dong — ke ca sau 4000/4401 cung thu lai.
    sessionKick();
    cueError();
    if (!simulated) waitRelease();
    return;
  }

  // Bo dem anh + tieng cua luot truoc phai xong viec tren duong len.
  if (!sessionUplinkIdle()) {
    unsigned long t = millis();
    while (!sessionUplinkIdle() && millis() - t < UPLINK_WAIT_MS)
      vTaskDelay(10 / portTICK_PERIOD_MS);
    if (!sessionUplinkIdle()) {
      Serial.println("Duong len van con ban voi luot truoc — bo luot nay");
      cueError();
      if (!simulated) waitRelease();
      return;
    }
    Serial.printf("Doi luot truoc day xong %lu ms\n", millis() - t);
  }

  char turn[24];
  newTurnId(turn, sizeof(turn));
  unsigned long tPress = millis();

  strncpy(s_curTurn, turn, sizeof(s_curTurn) - 1);
  strncpy(s_waitTurn, turn, sizeof(s_waitTurn) - 1);
  s_gotReply = false;
  s_rejected = false;
  s_lost     = false;
  // 🔴 Bat co cho NGAY TU DAY, khong doi toi sau turn_end. Do that tren board
  // (2026-10-20): server tra `reply` ~200 ms sau turn_end — nhanh hon luc task
  // audio in xong bang thoi gian + bo nho roi moi bat co. Reply toi luc co con
  // tat thi hReply() khong ghi nhan, va 15 s sau kinh keu loi oan du da phat
  // xong cau tra loi. Dong ho 15 s van chi tinh tu turn_end (o duoi).
  s_waitSince = millis();
  s_waiting   = true;
  sessionTurnStart(turn, tPress);
  perfMark("turn_start");

  // 1. Chup TRUOC khi thu: anh phai la khoanh khac bam, khong phai luc nha.
  bool havePhoto = photoCapture();
  perfMark("chup anh");

  // 2. Giao anh NGAY (muc 12, buoc 3) — no di len trong luc nguoi dung dang noi.
  // Da nha nut truoc khi chup xong thi day la cu bam KHONG noi (= "dung" khi
  // dang phat nhac / chi duong, muc 5.5): khong can anh, tiet kiem duong len.
  bool held = simulated || btnDown();
  if (havePhoto && held) sessionImage(photoData(), photoSize());
  else if (!havePhoto)   Serial.println("Khong co anh — luot nay chi co tieng");

  // 3. Thu va day tieng THEO LUONG (muc 1, buoc 9).
  encodeBegin();
  sessionAudioOpen(sendBuf);
  size_t pcmBytes = 0;
  if (simulated)  pcmBytes = recordFixed(holdMs, onRecChunk);
  else if (held)  pcmBytes = recordWhileHeld(onRecChunk);
  perfMark("thu am + nen + day");

  if (!simulated) waitRelease();
  perfMark("cho nha nut");

  // 🔴 Cu bam ngan KHONG bi bo nhu truoc (MIN_MS). Voi server nay "bam khong
  // noi" la mot lenh that — dung nhac, dung chi duong (muc 5.5) — nen van phai
  // turn_end du khong co tieng nao.
  encodeFinish(pcmBytes);
  bool ended = sessionTurnEnd(turn, millis());
  perfMark("turn_end");

  dspStreamReport(pcmBytes / sizeof(int16_t));

  if (!ended) {
    s_waiting = false;
    Serial.println("Mat phien giua luot — keu loi");
    cueError();
  } else {
    s_waitSince = millis();
  }
  perfReport();
  memReport("sau mot luot");
  // Anh KHONG tra o day: task mang co the van dang day no. photoCapture() cua
  // luot sau tu tra, va luot sau chi chup khi duong len da ranh (o tren).
}

void audioSimulatePress(unsigned long holdMs) {
  runTurn(true, holdMs);
}

// ============================================================================
// Cuoc goi (FIRMWARE_ENDPOINTS muc 16)
// ============================================================================
// Bam nut khi dang co cuoc goi: KHONG phai mot luot hoi. Server bo qua
// turn_start khi ringing_out/active, nen gui turn_start luc nay chi lam nguoi
// dung tuong may lien.
static void onCallPress() {
  pqFlush();                  // dung cau "Me dang goi. Bam nut de nghe." dang doc
  char id[64];
  portENTER_CRITICAL(&s_callMux);
  memcpy(id, s_callId, sizeof(id));
  portEXIT_CRITICAL(&s_callMux);

  if (!sessionReady()) {
    sessionKick();
    cueError();
  } else if (s_call == CALL_RING_IN) {
    Serial.println("\n===== BAM NUT: NGHE MAY =====");
    sessionCallAnswer(id);
  } else {
    // ringing_out (ke ca chuoi khan cap) hoac active: cup / huy.
    Serial.println("\n===== BAM NUT: CUP MAY =====");
    s_hangupAt   = millis();
    s_hangupSent = true;
    sessionCallHangup(id);
  }
  waitRelease();
}

// Vong song cong: moi vong doc DUNG mot khoi mic (~31.6 ms, nhip cua ca vong),
// nen + gui 0x05, roi phat DUNG mot khoi tieng dau kia. Chay toi khi cuoc goi
// het, nguoi dung bam cup, hoac socket rot.
//
// Lenh Serial KHONG chay trong luc goi: lenh chan doan dung chung recBuf[] va
// I2S voi vong nay.
//
// 🔴 Chua co khu vong (AEC): mic nghe ca tieng loa, nen dau kia nghe lai giong
// minh tre ~0.3-0.5 s. Spec muc 16.4 cung ghi AEC "chua kiem".
static void runCall() {
  Serial.println("\n===== DANG GOI =====");
  pqFlush();                  // server da dung moi cau dang doc (muc 16.2)
  livePause(true);            // muc 16.2: ngung 0x04 khi active

  // sendBuf lam vong mic. Luot hoi truoc (neu con) phai day xong truoc.
  unsigned long t = millis();
  while (!sessionUplinkIdle() && millis() - t < 1000)
    vTaskDelay(10 / portTICK_PERIOD_MS);

  adpcmEncReset(g_enc);
  dspStreamBegin();
  sessionCallMicOpen(sendBuf, sendCap);
  playerCallBegin();

  uint8_t       out[ADPCM_BLOCK_BYTES];
  uint32_t      sent = 0, dropped = 0;
  bool          pressed = false;
  unsigned long t0 = millis();

  while (s_call == CALL_ACTIVE && !s_hangupSent && sessionReady() && !s_dropAll) {
    if (btnDown() && millis() - s_releasedAt >= PRESS_GAP_MS) {
      vTaskDelay(30 / portTICK_PERIOD_MS);      // loc nhieu nut; DMA loa con ~240 ms
      if (btnDown()) { pressed = true; break; }
    }
    size_t n = recordBlock(recBuf, ADPCM_BLOCK_SAMPLES);
    if (n) {
      dspStreamPrep(0, n);
      size_t b = adpcmEncodeBlock(g_enc, recBuf, n, out);
      if (sessionCallMicPush(out, b)) sent++;
      else                            dropped++;
    }
    playerCallStep();
  }

  sessionCallMicClose();
  playerCallEnd();
  livePause(false);
  Serial.printf("Het vong goi sau %lu s: mic %u khoi, bo %u khoi\n",
                (millis() - t0) / 1000, (unsigned)sent, (unsigned)dropped);

  // Cup SAU khi da tra loa: giu nut ma loa con mo thi DMA can va re.
  if (pressed) onCallPress();
}

// ============================================================================
static void checkReplyWait() {
  if (!s_waiting) return;
  if (s_gotReply || s_rejected) { s_waiting = false; return; }
  if (s_lost) {
    s_waiting = false;
    Serial.println("Socket rot khi dang cho tra loi — bam lai sau khi noi lai");
    cueError();
    return;
  }
  if (millis() - s_waitSince > REPLY_TIMEOUT_MS) {
    s_waiting = false;
    Serial.printf("Qua %lu s khong co reply cho %s — keu loi\n",
                  REPLY_TIMEOUT_MS / 1000, s_waitTurn);
    cueError();
  }
}

static void maybeRestart() {
#if RESTART_ON_TLS_STARVE
  if (sessionReady() || sessionFailStreak() < RESTART_FAILS) return;
  if (s_waiting || playerActive() || !pqEmpty() || s_call != CALL_NONE) return;
  if (WiFi.status() != WL_CONNECTED) return;      // mang hong thi restart vo ich
  size_t big = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (big >= TLS_MIN_BLOCK) return;
  Serial.printf("TLS hong %u lan lien, khoi lien RAM noi %u B < %u B — khoi dong lai\n",
                (unsigned)sessionFailStreak(), (unsigned)big, (unsigned)TLS_MIN_BLOCK);
  delay(200);
  ESP.restart();
#endif
}

static void audioTask(void *arg) {
  for (;;) {
    // Lenh chan doan qua Serial. Xem serial_console.h.
    consolePoll();

    if (s_dropAll) { s_dropAll = false; pqFlush(); }

    if (btnDown() && millis() - s_releasedAt >= PRESS_GAP_MS) {
      vTaskDelay(30 / portTICK_PERIOD_MS);      // loc nhieu nut
      if (btnDown()) {
        if (s_call != CALL_NONE) onCallPress();
        else                     runTurn(false, 0);
        continue;
      }
    }

    if (s_call == CALL_ACTIVE && !s_hangupSent && sessionReady()) {
      runCall();
      continue;
    }

    // Phat moi thu dang co. Tra ve ngay khi hang rong; bam nut thi tra ve
    // ngay — vong sau bat duoc nut va mo luot moi.
    playerPump(stopPlayback);

    checkReplyWait();
    maybeRestart();
    vTaskDelay(10 / portTICK_PERIOD_MS);        // vTaskDelay, KHONG delay()
  }
}

// ============================================================================
// Khoi tao — goi sau startCameraServer()
// ============================================================================
bool startAudio() {
  pinMode(BTN, INPUT_PULLUP);

  // Nut khong duoc phep dang bam luc khoi dong. Neu doc ra dang bam thi gan
  // nhu chac chan la chap day.
  delay(5);                     // cho dien tro keo len on dinh
  if (digitalRead(BTN) == BTN_ACTIVE) {
    Serial.printf("CANH BAO: GPIO%d dang o muc thap NGAY LUC KHOI DONG.\n", BTN);
    Serial.println("Nut dang bi ket hoac day tin hieu cham GND. May se tu");
    Serial.println("kich hoat lien tuc cho toi khi sua day.");
  }

  flashLedBegin();

  memReport("truoc khi cap phat audio");

  if (!audioBuffersAlloc()) return false;
  pqBegin(stageBuf, stageCap);

  if (!i2sBegin()) {
    audioBuffersFree();         // tra lai vai tram KB cho camera
    return false;
  }

  playerSetPlayedHandler(onPlayed);
  liveBegin(liveSend);

  // Ghim core 1: core 0 dang lo Wi-Fi/TCP + task wssess. Priority 1 = duoi httpd.
  if (xTaskCreatePinnedToCore(audioTask, "audio", 12288, NULL, 1, NULL, 1) != pdPASS) {
    Serial.println("Khong tao duoc task audio");
    audioBuffersFree();
    return false;
  }

  memReport("sau khi cap phat audio — day la muc nen thuc te");

  Serial.printf("Audio san sang (toi da %u giay/luot).\n",
                (unsigned)(recMaxSamp / SR));
  Serial.printf("Giu nut GPIO%d de chup + thu. Go 'v' xem phien, 'r' xem bo nho.\n", BTN);
  return true;
}
