#include "device_session.h"

#include <WiFi.h>
#include <esp_random.h>

#include "device_token.h"
#include "ws_client.h"
#include "../audio/audio_cues.h"
#include "../../app_config.h"

// ============================================================================
// Hang so cua giao thuc — lay tu FIRMWARE_ENDPOINTS.md
// ============================================================================
static const unsigned long PING_MS       = 25000;   // muc 1, buoc 5
static const unsigned long HELLO_OK_MS   = 10000;   // server dong 4400 sau 10 s
// Khong nhan duoc gi (ke ca pong) bay lau thi coi socket la chet. Ping 25 s
// nen binh thuong luon co pong trong 25 s; 70 s la ba nhip ping truot.
static const unsigned long DEAD_MS       = 70000;
static const unsigned long BACKOFF_MS[]  = { 500, 1000, 2000, 4000, 8000 };
static const int           BACKOFF_N     = sizeof(BACKOFF_MS) / sizeof(BACKOFF_MS[0]);

// 🔴 Gom tieng toi 1 KB moi gui (~128 ms tieng). Bo nen sinh mot khoi 256 B
// moi 32 ms; gui tung khoi thi moi khoi la mot khung WS + mot ban ghi TLS +
// mot goi TCP — do tren duong POST cu: tieng chi day duoc 3.8 KB/s trong khi
// anh day 15.5 KB/s tren cung socket. Gom 1 KB thi phi giam 4 lan, ma server
// van "doan y" duoc tren phan da toi (muc 12, buoc 4).
static const size_t AUDIO_CHUNK = 1024;

// Mic cuoc goi: gom 2 khoi (~63 ms) moi gui. Khac AUDIO_CHUNK o tren: trong
// cuoc goi do tre la thu nguoi nghe cam thay, con phi khung thi 16 khung/s
// van nhe.
static const size_t MIC_CHUNK   = 512;
// 🔴 Mang nghen thi BO tieng cu, khong day bu. Tieng cuoc goi tre 1 s da vo
// dung; day het phan don lai la ca cuoc goi tre theo vinh vien.
static const size_t MIC_MAX_LAG = 32 * 256;   // ~1 s
static const size_t MIC_KEEP    = 4 * 256;

// ============================================================================
// Trang thai
// ============================================================================
enum SessState : uint8_t {
  SS_OFFLINE,      // chua co socket, dang cho backoff / Wi-Fi
  SS_WAIT_HELLO,   // socket mo, da gui hello, cho hello_ok
  SS_READY,        // dung duoc
  SS_REPLACED,     // dong 4000 — co phien moi cua cung kinh. Khong tu mo lai.
  SS_BAD_TOKEN,    // dong 4401 — token sai/thieu. Khong tu mo lai.
  SS_NO_TOKEN,     // NVS chua co token, khong mo socket
};

static SessionHandlers   H = {};
static volatile SessState g_st = SS_OFFLINE;
static volatile bool     g_kick = false;
static TaskHandle_t      g_task = nullptr;

static char          g_device[32];
static char          g_session[32]   = "";
static int           g_srvCue        = -1;
static uint16_t      g_lastClose     = 0;
static uint16_t      g_failStreak    = 0;
static uint32_t      g_opens         = 0;
static int           g_backoffIdx    = 0;
static unsigned long g_nextTry       = 0;
static unsigned long g_tHello        = 0;
static unsigned long g_lastTx        = 0;

// ---- hang gui
enum OutKind : uint8_t { OM_TEXT, OM_IMAGE, OM_AUDIO };
struct OutMsg {
  OutKind        kind;
  const uint8_t *ptr;
  size_t         len;
  char           text[200];
};
static QueueHandle_t g_q = nullptr;

// 🔴 Cung kieu "mot nguoi ghi, mot nguoi doc" nhu duong POST cu: task audio
// CHI tang g_audFill va dat g_audDone; task mang CHI doc. Khong khoa.
static const uint8_t *volatile g_aud      = nullptr;
static volatile size_t         g_audFill  = 0;
static volatile bool           g_audDone  = false;
static bool                    g_streaming = false;   // chi task mang dung
static size_t                  g_audSent   = 0;       // chi task mang dung
// Anh dang cho gui = da xep - da xong. Moi bien mot nguoi ghi (task audio tang
// g_imgQueued, task mang tang g_imgDone), nen khong can khoa.
static volatile uint32_t       g_imgQueued = 0;
static volatile uint32_t       g_imgDone   = 0;

// ---- khung live: mot o duy nhat. 0 = trong, 1 = da dat, 2 = dang gui
static const uint8_t *volatile g_livePtr = nullptr;
static volatile size_t         g_liveLen = 0;
static volatile uint8_t        g_liveSt  = 0;
static portMUX_TYPE            g_liveMux = portMUX_INITIALIZER_UNLOCKED;

// ---- mic cuoc goi: vong tron, vi tri la so byte TICH LUY nhu play_queue.
// Task audio CHI ghi g_micFill/g_micOn, task mang CHI ghi g_micSent — khong khoa.
static uint8_t *volatile       g_mic        = nullptr;
static volatile size_t         g_micCap     = 0;
static volatile uint32_t       g_micFill    = 0;
static volatile uint32_t       g_micSent    = 0;
static volatile bool           g_micOn      = false;
static volatile bool           g_micSending = false;

bool     sessionReady()      { return g_st == SS_READY; }
uint16_t sessionFailStreak() { return g_failStreak; }

static const char *stateName(SessState s) {
  switch (s) {
    case SS_OFFLINE:    return "OFFLINE";
    case SS_WAIT_HELLO: return "CHO hello_ok";
    case SS_READY:      return "SAN SANG";
    case SS_REPLACED:   return "BI THAY THE (4000)";
    case SS_BAD_TOKEN:  return "SAI TOKEN (4401)";
    case SS_NO_TOKEN:   return "CHUA CO TOKEN";
  }
  return "?";
}

// ============================================================================
// JSON — chi du cho cac khung phang cua server
// ============================================================================
// Khung server gui deu phang (mot tang, rieng `audio` long mot tang) va ten
// khoa khong trung nhau, nen tim theo ten khoa la du. Khong keo ArduinoJson vao
// chi de doc chuc truong co dinh.
static const char *jFind(const char *j, const char *key) {
  size_t kl = strlen(key);
  for (const char *p = j; (p = strchr(p, '"')) != nullptr; p++) {
    if (strncmp(p + 1, key, kl) == 0 && p[1 + kl] == '"') {
      const char *q = p + 2 + kl;
      while (*q == ' ' || *q == '\t') q++;
      if (*q != ':') continue;
      q++;
      while (*q == ' ' || *q == '\t') q++;
      return q;
    }
  }
  return nullptr;
}

// Doc mot chuoi bat dau tai `v` (dang tro vao dau "). Tra ve con tro sau dau "
// dong, hoac nullptr.
static const char *jReadStr(const char *v, char *out, size_t cap) {
  if (*v != '"') return nullptr;
  size_t n = 0;
  for (v++; *v && *v != '"'; v++) {
    char c = *v;
    bool esc = false;
    if (c == '\\' && v[1]) { c = *++v; esc = true; }
    // json.dumps() cua Python mac dinh thoat moi ky tu ngoai ASCII: ten nguoi
    // than "Me" co dau toi day thanh ẹ. Doi lai ra UTF-8.
    if (esc && c == 'u' && isxdigit(v[1]) && isxdigit(v[2]) &&
        isxdigit(v[3]) && isxdigit(v[4])) {
      char hex[5] = { v[1], v[2], v[3], v[4], 0 };
      uint32_t cp = strtoul(hex, nullptr, 16);
      v += 4;
      char u[3];
      size_t k;
      if (cp < 0x80)       { u[0] = (char)cp; k = 1; }
      else if (cp < 0x800) { u[0] = (char)(0xC0 | (cp >> 6));  u[1] = (char)(0x80 | (cp & 0x3F)); k = 2; }
      else                 { u[0] = (char)(0xE0 | (cp >> 12)); u[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                             u[2] = (char)(0x80 | (cp & 0x3F)); k = 3; }
      if (n + k < cap) { memcpy(out + n, u, k); n += k; }
      continue;
    }
    if (n + 1 < cap) out[n++] = c;
  }
  out[n] = 0;
  return *v == '"' ? v + 1 : nullptr;
}

static bool jStr(const char *j, const char *key, char *out, size_t cap) {
  out[0] = 0;
  const char *v = jFind(j, key);
  return v && jReadStr(v, out, cap);
}

static double jNum(const char *j, const char *key, double def) {
  const char *v = jFind(j, key);
  if (!v || (*v != '-' && (*v < '0' || *v > '9'))) return def;
  return strtod(v, nullptr);
}

static bool jIsNull(const char *j, const char *key) {
  const char *v = jFind(j, key);
  return !v || strncmp(v, "null", 4) == 0;
}

// Thoat chuoi de nhet vao JSON. Token/turn la chuoi do kinh hoac server dat,
// nhung mot dau " lot vao la ca khung thanh bad_frame.
static void jEsc(char *out, size_t cap, const char *s) {
  size_t n = 0;
  for (; s && *s && n + 2 < cap; s++) {
    if (*s == '"' || *s == '\\') out[n++] = '\\';
    out[n++] = *s;
  }
  out[n] = 0;
}

// ============================================================================
// Gui
// ============================================================================
static bool sendText(const char *s) {
  bool ok = wsSendText(s, strlen(s));
  if (ok) g_lastTx = millis();
  return ok;
}

static bool enqueue(const OutMsg &m) {
  if (!g_q) return false;
  if (xQueueSend(g_q, &m, 0) != pdTRUE) {
    Serial.println("Hang gui day — bo mot khung");
    return false;
  }
  return true;
}

static bool enqueueText(const char *s) {
  OutMsg m = {};
  m.kind = OM_TEXT;
  strncpy(m.text, s, sizeof(m.text) - 1);
  return enqueue(m);
}

bool sessionTurnStart(const char *turn, unsigned long tPressMs) {
  if (g_st != SS_READY) return false;
  char t[64], j[160];
  jEsc(t, sizeof(t), turn);
  snprintf(j, sizeof(j), "{\"t\":\"turn_start\",\"turn\":\"%s\",\"t_press_ms\":%lu}", t, tPressMs);
  return enqueueText(j);
}

void sessionImage(const uint8_t *jpg, size_t len) {
  if (g_st != SS_READY || !jpg || !len) return;
  OutMsg m = {};
  m.kind = OM_IMAGE; m.ptr = jpg; m.len = len;
  if (enqueue(m)) g_imgQueued = g_imgQueued + 1;
}

void sessionAudioOpen(const uint8_t *buf) {
  if (g_st != SS_READY) return;
  g_audFill = 0;
  g_audDone = false;
  // Con tro dat SAU: task mang lay con tro tu hang, roi moi doc fill/done.
  g_aud = buf;
  OutMsg m = {};
  m.kind = OM_AUDIO; m.ptr = buf;
  enqueue(m);
}

void sessionAudioPush(size_t totalBytes, bool done) {
  // 🔴 So byte dat TRUOC co `done`, task mang doc theo thu tu nguoc lai — nen
  // no khong bao gio thay "xong" di kem mot con so cu va bo sot khoi cuoi.
  if (totalBytes > g_audFill) g_audFill = totalBytes;
  if (done) g_audDone = true;
}

bool sessionTurnEnd(const char *turn, unsigned long tReleaseMs) {
  if (g_st != SS_READY) return false;
  char t[64], j[160];
  jEsc(t, sizeof(t), turn);
  snprintf(j, sizeof(j), "{\"t\":\"turn_end\",\"turn\":\"%s\",\"t_release_ms\":%lu}", t, tReleaseMs);
  return enqueueText(j);
}

void sessionPlayed(const char *turn, unsigned long firstByteMs, unsigned long firstAudioMs) {
  if (g_st != SS_READY) return;
  char t[64], j[200];
  jEsc(t, sizeof(t), turn);
  snprintf(j, sizeof(j),
           "{\"t\":\"played\",\"turn\":\"%s\",\"first_byte_ms\":%lu,\"first_audio_ms\":%lu}",
           t, firstByteMs, firstAudioMs);
  enqueueText(j);
}

void sessionCancel() {
  if (g_st == SS_READY) enqueueText("{\"t\":\"cancel\"}");
}

static void sendCallCmd(const char *t, const char *call) {
  if (g_st != SS_READY) return;
  char c[80], j[160];
  jEsc(c, sizeof(c), call);
  snprintf(j, sizeof(j), "{\"t\":\"%s\",\"call\":\"%s\"}", t, c);
  enqueueText(j);
}

void sessionCallAnswer(const char *call) { sendCallCmd("call_answer", call); }
void sessionCallHangup(const char *call) { sendCallCmd("call_hangup", call); }

void sessionCallMicOpen(uint8_t *ring, size_t cap) {
  g_mic    = ring;
  // Tron xuong boi so khoi: moi khoi nam lien mot cho, khong vat qua mep.
  g_micCap = cap / 256 * 256;
  g_micOn  = g_micCap > 0;
}

bool sessionCallMicPush(const uint8_t *p, size_t n) {
  if (!g_micOn || g_st != SS_READY) return false;
  uint32_t f = g_micFill, s = g_micSent;
  if (g_micCap - (size_t)(f - s) < n) return false;
  size_t off = f % g_micCap, first = g_micCap - off;
  if (first > n) first = n;
  memcpy(g_mic + off, p, first);
  if (n > first) memcpy(g_mic, p + first, n - first);
  g_micFill = f + (uint32_t)n;     // dat SAU khi chep xong
  return true;
}

void sessionCallMicClose() { g_micOn = false; }

bool sessionUplinkIdle() {
  if (!g_q) return true;
  return uxQueueMessagesWaiting(g_q) == 0 && !g_streaming && g_imgQueued == g_imgDone &&
         !g_micOn && !g_micSending;
}

bool sessionLiveFrame(const uint8_t *jpg, size_t len, unsigned long waitMs) {
  if (g_st != SS_READY) return false;
  portENTER_CRITICAL(&g_liveMux);
  if (g_liveSt != 0) { portEXIT_CRITICAL(&g_liveMux); return false; }
  g_livePtr = jpg; g_liveLen = len; g_liveSt = 1;
  portEXIT_CRITICAL(&g_liveMux);

  unsigned long t = millis();
  for (;;) {
    portENTER_CRITICAL(&g_liveMux);
    uint8_t st = g_liveSt;
    // Het gio ma task mang CHUA cam khung: rut lai. Da cam (st == 2) thi phai
    // doi no gui xong — con tro dang duoc doc, tra frame buffer luc nay la
    // gui rac len server.
    if (st == 1 && millis() - t > waitMs) { g_liveSt = 0; st = 3; }
    portEXIT_CRITICAL(&g_liveMux);
    if (st == 0) return true;
    if (st == 3) return false;
    vTaskDelay(2 / portTICK_PERIOD_MS);
  }
}

// ============================================================================
// Nhan
// ============================================================================
static char   g_txt[1536];
static size_t g_txtLen = 0;
static bool   g_txtOver = false;
static uint8_t g_binType = 0;

static void onText(const char *j) {
  char t[24];
  jStr(j, "t", t, sizeof(t));

  if (!strcmp(t, "hello_ok")) {
    jStr(j, "session", g_session, sizeof(g_session));
    g_srvCue = (int)jNum(j, "cue_version", -1);
    g_st = SS_READY;
    g_failStreak = 0;
    g_backoffIdx = 0;
    Serial.printf("Phien %s SAN SANG (cue server %d, kinh %d, ping %d s)\n",
                  g_session, g_srvCue, cueVersion(), (int)jNum(j, "ping_s", 25));
    if (g_srvCue > cueVersion())
      Serial.println("  Bo cue cua kinh CU hon server: server se doc cue thanh tieng. "
                     "Chep assets/cues moi vao cues/ roi chay tools/gen_cue_clips.py.");
    if (H.onReady) H.onReady();
    return;
  }

  if (!strcmp(t, "reply")) {
    static char turn[48], live[8], cues[8][24];
    SessionReply r = {};
    jStr(j, "turn", turn, sizeof(turn));
    r.turn = turn;
    jStr(j, "live", live, sizeof(live));        // null -> ""
    r.live = live;
    r.hasAudio = !jIsNull(j, "audio");
    double pre = jNum(j, "preroll_s", 0.2);
    r.prerollMs = (uint16_t)(pre * 1000.0 + 0.5);
    const char *v = jFind(j, "cues");
    if (v && *v == '[') {
      v++;
      while (r.nCues < 8) {
        while (*v == ' ' || *v == ',') v++;
        if (*v != '"') break;
        v = jReadStr(v, cues[r.nCues], sizeof(cues[0]));
        if (!v) break;
        r.cues[r.nCues] = cues[r.nCues];
        r.nCues++;
      }
    }
    if (H.onReply) H.onReply(r);
    return;
  }

  if (!strcmp(t, "reply_end")) {
    char turn[48];
    jStr(j, "turn", turn, sizeof(turn));
    if (H.onReplyEnd) H.onReplyEnd(turn);
    return;
  }

  if (!strcmp(t, "hazard")) {
    char dir[12], kind[24];
    jStr(j, "dir", dir, sizeof(dir));
    jStr(j, "kind", kind, sizeof(kind));
    if (H.onHazard) H.onHazard((int)jNum(j, "level", 1), dir, kind);
    return;
  }

  if (!strcmp(t, "cue")) {
    char id[24];
    jStr(j, "id", id, sizeof(id));
    if (H.onCue) H.onCue(id);
    return;
  }

  if (!strcmp(t, "cam")) {
    char size[12];
    jStr(j, "size", size, sizeof(size));
    if (H.onCam) H.onCam(size, (int)jNum(j, "fps", 5), (int)jNum(j, "quality", 12));
    return;
  }

  if (!strcmp(t, "capture")) {
    char size[12];
    jStr(j, "size", size, sizeof(size));
    if (H.onCapture) H.onCapture(size);
    return;
  }

  if (!strcmp(t, "end")) {
    if (H.onLiveEnd) H.onLiveEnd();
    return;
  }

  if (!strcmp(t, "call_state")) {
    char call[64], state[16], peer[64], reason[32];
    jStr(j, "call", call, sizeof(call));
    jStr(j, "state", state, sizeof(state));
    jStr(j, "peer", peer, sizeof(peer));
    jStr(j, "reason", reason, sizeof(reason));
    if (H.onCallState) H.onCallState(call, state, peer, reason);
    return;
  }

  if (!strcmp(t, "pong")) return;

  if (!strcmp(t, "error")) {
    char code[24];
    jStr(j, "code", code, sizeof(code));
    Serial.printf("Server bao loi: %s\n", code);
    if (H.onError) H.onError(code);
    return;
  }

  Serial.printf("Khung text la: %.80s\n", j);
}

static void onPiece(const WsPiece &p) {
  if (p.text) {
    if (p.first) { g_txtLen = 0; g_txtOver = false; }
    if (g_txtLen + p.len < sizeof(g_txt)) {
      memcpy(g_txt + g_txtLen, p.data, p.len);
      g_txtLen += p.len;
    } else {
      g_txtOver = true;
    }
    if (p.last) {
      g_txt[g_txtLen] = 0;
      if (g_txtOver) Serial.printf("Khung text qua dai (> %u B) — bo\n", (unsigned)sizeof(g_txt));
      else           onText(g_txt);
    }
    return;
  }

  // Binary: byte dau cua THONG DIEP la loai. Mieng sau cung loai voi mieng dau.
  const uint8_t *d = p.data;
  size_t         n = p.len;
  if (p.first) {
    if (n == 0) return;
    g_binType = d[0];
    d++; n--;
    if (g_binType != 0x02)
      Serial.printf("Khung binary loai la 0x%02X — bo\n", g_binType);
  }
  if (g_binType == 0x02 && n && H.onAudio) H.onAudio(d, n);
}

// ============================================================================
// Vong doi socket
// ============================================================================
static void dropUplink() {
  OutMsg m;
  while (g_q && xQueueReceive(g_q, &m, 0) == pdTRUE) {}
  g_streaming = false;
  g_imgDone = g_imgQueued;
  g_micSent = g_micFill;
  portENTER_CRITICAL(&g_liveMux);
  g_liveSt = 0;               // nguoi dang doi sessionLiveFrame() se ve ngay
  portEXIT_CRITICAL(&g_liveMux);
}

static void scheduleRetry() {
  unsigned long base = BACKOFF_MS[g_backoffIdx];
  if (g_backoffIdx < BACKOFF_N - 1) g_backoffIdx++;
  // Nhieu +-20%: nhieu kinh cung rot mot luc (server khoi dong lai) khong
  // dong loat go cua cung mot nhip.
  long jitter = (long)(base / 5);
  long d = (long)base + (long)(esp_random() % (uint32_t)(2 * jitter + 1)) - jitter;
  g_nextTry = millis() + (unsigned long)d;
}

static void onClosed(uint16_t code, bool wasReady) {
  g_lastClose = code;
  g_st = SS_OFFLINE;
  g_session[0] = 0;
  dropUplink();
  if (!wasReady) g_failStreak++;

  switch (code) {
    case 4000:
      Serial.println("WS dong 4000: phien moi cua kinh nay da thay the — KHONG tu mo lai "
                     "(bam nut de mo lai)");
      g_st = SS_REPLACED;
      break;
    case 4401:
      Serial.println("WS dong 4401: token sai/thieu hoac server chua cau hinh token — "
                     "KHONG thu lai. Nap token bang lenh '=' roi bam nut.");
      g_st = SS_BAD_TOKEN;
      break;
    case 4400:
      Serial.println("WS dong 4400: khung dau khong phai hello hop le — LOI FIRMWARE");
      break;
    case 1009:
      Serial.println("WS dong 1009: khung qua lon — LOI FIRMWARE");
      break;
    case 1001:
      Serial.println("WS dong 1001: server dong vi im lang qua 90 s");
      break;
    default:
      Serial.printf("WS dong %u\n", (unsigned)code);
      break;
  }
  if (g_st == SS_OFFLINE) scheduleRetry();
  if (wasReady && H.onDown) H.onDown();
}

// Mo socket + gui hello. Chay tren task goi (setup() lan dau, "wssess" ve sau).
static void connectOnce() {
  char token[DEVICE_TOKEN_MAX + 1];
  if (!tokenLoad(token, sizeof(token))) {
    if (g_st != SS_NO_TOKEN)
      Serial.println("CHUA CO TOKEN trong NVS — go '=<token>' qua Serial, "
                     "hoac chay tools/nap_token.ps1");
    g_st = SS_NO_TOKEN;
    return;
  }

  g_opens++;
  if (!wsOpen(API_HOST, API_PORT, API_PATH)) {
    g_failStreak++;
    g_st = SS_OFFLINE;
    scheduleRetry();
    return;
  }

  // 🔴 Token chi nam trong khung nay, KHONG bao gio duoc in. Log ben duoi chi
  // in do dai.
  char tok[2 * DEVICE_TOKEN_MAX + 1], j[400];
  jEsc(tok, sizeof(tok), token);
  snprintf(j, sizeof(j),
           "{\"t\":\"hello\",\"device\":\"%s\",\"token\":\"%s\",\"cue_version\":%d,"
           "\"fw\":\"" FW_VERSION "\",\"accept\":\"ima_adpcm\"}",
           g_device, tok, cueVersion());
  size_t tokLen = strlen(token);
  memset(token, 0, sizeof(token));
  bool ok = sendText(j);
  memset(j, 0, sizeof(j));
  memset(tok, 0, sizeof(tok));
  if (!ok) {
    wsAbort();
    onClosed(1006, false);
    return;
  }
  Serial.printf("Da gui hello (device %s, token %u ky tu, cue_version %d)\n",
                g_device, (unsigned)tokLen, cueVersion());
  g_st = SS_WAIT_HELLO;
  g_tHello = millis();
}

// Day tieng da co len. Tra ve true khi luong tieng da xong han.
static bool pumpAudio() {
  size_t have = g_audFill;
  bool   fin  = g_audDone;      // doc SAU `have` — xem sessionAudioPush()
  if (have > g_audSent && (have - g_audSent >= AUDIO_CHUNK || fin)) {
    size_t n = have - g_audSent;
    // Mot khung WS <= 2.1 MB la du rong, nhung giu moi khung vua phai de
    // doc xen duoc khung server gui ve giua hai lan ghi.
    if (n > 8 * AUDIO_CHUNK) n = 8 * AUDIO_CHUNK;
    if (!wsSendTyped(0x03, g_aud + g_audSent, n)) return false;
    g_audSent += n;
    g_lastTx = millis();
    return false;
  }
  if (fin && g_audSent >= g_audFill) {
    Serial.printf("Day xong tieng %u B\n", (unsigned)g_audSent);
    return true;
  }
  return false;
}

// Day mic cuoc goi da co len (0x05).
static void pumpMic() {
  // 🔴 Bat co "dang gui" TRUOC khi doc g_micOn. Nguoc lai thi task audio co the
  // dong mic, thay sessionUplinkIdle() = true va ghi de vong giua luc task nay
  // van dinh gui.
  g_micSending = true;
  if (!g_micOn) {
    g_micSent    = g_micFill;      // khong goi thi bam theo, lan mo sau tinh tu day
    g_micSending = false;
    return;
  }
  uint32_t f = g_micFill, s = g_micSent;
  if (f - s > MIC_MAX_LAG) {
    Serial.printf("Goi: mic don %u B — bo, chi giu %u B moi nhat\n",
                  (unsigned)(f - s), (unsigned)MIC_KEEP);
    s = f - MIC_KEEP;
    g_micSent = s;
  }
  size_t have = f - s;
  if (have >= MIC_CHUNK) {
    size_t off = s % g_micCap, n = g_micCap - off;
    if (n > have) n = have;
    if (n > 4 * MIC_CHUNK) n = 4 * MIC_CHUNK;
    if (wsSendTyped(0x05, g_mic + off, n)) g_lastTx = millis();
    g_micSent = s + (uint32_t)n;
  }
  g_micSending = false;
}

static void sessTask(void *) {
  for (;;) {
    // ---- khong co mang: dung yen, socket (neu con) coi nhu chet
    if (WiFi.status() != WL_CONNECTED) {
      if (wsIsOpen()) {
        bool wasReady = (g_st == SS_READY);
        wsAbort();
        onClosed(1006, wasReady);
      }
      vTaskDelay(200 / portTICK_PERIOD_MS);
      continue;
    }

    // ---- chua co socket: cho backoff, hoac duoc kick
    if (!wsIsOpen()) {
      bool kick = g_kick;
      g_kick = false;
      bool parked = (g_st == SS_REPLACED || g_st == SS_BAD_TOKEN || g_st == SS_NO_TOKEN);
      if ((parked && !kick) || (!kick && (long)(millis() - g_nextTry) < 0)) {
        vTaskDelay(50 / portTICK_PERIOD_MS);
        continue;
      }
      if (kick) g_backoffIdx = 0;
      connectOnce();
      continue;
    }

    // ---- doc truoc: vai mieng moi vong, du nhanh hon 8 KB/s tieng nhieu lan
    for (int i = 0; i < 16; i++) {
      WsPiece p = wsPoll();
      if (p.kind == WS_NOTHING) break;
      if (p.kind == WS_CLOSED) { onClosed(p.closeCode, g_st == SS_READY); break; }
      onPiece(p);
    }
    if (!wsIsOpen()) continue;

    if (g_st == SS_WAIT_HELLO && millis() - g_tHello > HELLO_OK_MS) {
      Serial.println("Qua 10 s khong co hello_ok — dong, mo lai");
      wsClose(1000);
      onClosed(1006, false);
      continue;
    }

    if (g_st == SS_READY) {
      // ---- ghi: dung thu tu trong hang. Dang day tieng thi chua lay khung sau
      // (turn_end phai di SAU khoi tieng cuoi).
      if (g_streaming) {
        if (pumpAudio()) g_streaming = false;
      } else {
        OutMsg m;
        if (xQueueReceive(g_q, &m, 0) == pdTRUE) {
          if (m.kind == OM_TEXT) {
            sendText(m.text);
          } else if (m.kind == OM_IMAGE) {
            unsigned long t = millis();
            if (wsSendTyped(0x01, m.ptr, m.len)) {
              g_lastTx = millis();
              Serial.printf("Day xong anh %u KB trong %lu ms\n",
                            (unsigned)(m.len / 1024), millis() - t);
            }
            g_imgDone = g_imgDone + 1;
          } else if (m.kind == OM_AUDIO) {
            g_audSent   = 0;
            g_streaming = true;
          }
        }
      }

      // ---- khung live: xen vao giua cac khung cua luot, khong doi luot xong
      portENTER_CRITICAL(&g_liveMux);
      bool haveLive = (g_liveSt == 1);
      if (haveLive) g_liveSt = 2;
      portEXIT_CRITICAL(&g_liveMux);
      if (haveLive) {
        if (wsSendTyped(0x04, g_livePtr, g_liveLen)) g_lastTx = millis();
        portENTER_CRITICAL(&g_liveMux);
        g_liveSt = 0;
        portEXIT_CRITICAL(&g_liveMux);
      }

      // ---- mic cuoc goi: cung xen vao, khong doi hang
      pumpMic();

      // ---- ping ke ca khi ranh: Cloudflare cat ket noi ranh ~100 s
      if (millis() - g_lastTx >= PING_MS) sendText("{\"t\":\"ping\"}");
    }

    // ---- socket "song gia": connected() van true ma khong co gi ve
    if (wsIsOpen() && millis() - wsLastRxMs() > DEAD_MS) {
      Serial.printf("Qua %lu s khong nhan duoc gi — coi socket la chet\n", DEAD_MS / 1000);
      bool wasReady = (g_st == SS_READY);
      wsAbort();
      onClosed(1006, wasReady);
      continue;
    }

    // Ghi that bai (ws_client tu dong socket) -> bao rot o vong sau.
    if (!wsIsOpen()) {
      onClosed(1006, g_st == SS_READY);
      continue;
    }

    vTaskDelay((g_streaming ? 1 : 2) / portTICK_PERIOD_MS);
  }
}

// ============================================================================
void sessionBegin(const SessionHandlers &h) {
  H = h;
  uint64_t mac = ESP.getEfuseMac();
  snprintf(g_device, sizeof(g_device), "glasses-%02x%02x%02x%02x%02x%02x",
           (uint8_t)(mac), (uint8_t)(mac >> 8), (uint8_t)(mac >> 16),
           (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  if (!g_q) g_q = xQueueCreate(16, sizeof(OutMsg));

  // Lan mo DAU chay ngay tren task goi — xem ghi chu o header: bat tay TLS
  // phai xong truoc khi httpd + audio lay mat khoi RAM noi lien.
  if (WiFi.status() == WL_CONNECTED) connectOnce();

  // Stack 12 KB: bat tay TLS cua mbedtls (khi mo lai) mot minh da an ~8 KB.
  // Ghim core 0 — noi Wi-Fi/TCP dang chay san, de core 1 danh tron cho I2S.
  if (!g_task &&
      xTaskCreatePinnedToCore(sessTask, "wssess", 12288, nullptr, 1, &g_task, 0) != pdPASS) {
    Serial.println("Khong tao duoc task wssess");
    g_task = nullptr;
  }
}

void sessionKick() {
  if (g_st == SS_REPLACED || g_st == SS_BAD_TOKEN || g_st == SS_NO_TOKEN)
    g_st = SS_OFFLINE;
  g_kick = true;
}

void sessionPrintStatus() {
  Serial.printf("Server  : %s://%s:%u%s\n", API_USE_TLS ? "wss" : "ws",
                API_HOST, (unsigned)API_PORT, API_PATH);
  Serial.printf("Phien   : %s%s%s\n", stateName(g_st),
                g_session[0] ? ", id " : "", g_session);
  Serial.printf("Device  : %s, fw " FW_VERSION ", cue kinh %d / server %d\n",
                g_device, cueVersion(), g_srvCue);
  Serial.printf("Mo socket %u lan, hong lien tiep %u, ma dong cuoi %u\n",
                (unsigned)g_opens, (unsigned)g_failStreak, (unsigned)g_lastClose);
  char tok[DEVICE_TOKEN_MAX + 1];
  bool have = tokenLoad(tok, sizeof(tok));
  Serial.printf("Token   : %s\n", have ? "CO (khong in ra)" : "CHUA CO");
  memset(tok, 0, sizeof(tok));
}
