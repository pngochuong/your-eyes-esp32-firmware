#include "ws_client.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>

#include "../../app_config.h"

// Kieu client doi theo cong tac API_USE_TLS. Ca hai deu ke thua NetworkClient
// nen moi cho khac trong file giu nguyen.
#if API_USE_TLS
static WiFiClientSecure g_c;
#else
static NetworkClient    g_c;
#endif

// Tran cho MOT lan ghi/doc o muc socket, va tran cho duong len dung han. Mot
// khung anh 60 KB tren hotspot 9 KB/s mat ~7 s — 20 s la du rong ma van biet
// duoc khi duong chet that.
static const unsigned long WS_IO_MS    = 20000;
// Tu luc gui GET toi luc co dong 101.
static const unsigned long WS_UPGRADE_MS = 10000;

static bool          g_open    = false;
static unsigned long g_lastRx  = 0;

unsigned long wsLastRxMs() { return g_lastRx; }
bool wsIsOpen() { return g_open; }

// ============================================================================
// Ghi
// ============================================================================
// Ghi het. client.write() co the ghi thieu khi bo dem day, nen phai lap.
static bool writeAll(const uint8_t *p, size_t len) {
  size_t sent = 0;
  unsigned long t0 = millis();
  while (sent < len) {
    size_t n = g_c.write(p + sent, len - sent);
    if (n == 0) {
      if (!g_c.connected() || millis() - t0 > WS_IO_MS) {
        // Bao nhieu byte da qua duoc la con so tach hai ca: dung o 0 nghia la
        // phien chet tu truoc, dung o giua nghia la duong truyen doi y.
        Serial.printf("WS: ghi dung o %u/%u byte sau %lu ms (RSSI %d)\n",
                      (unsigned)sent, (unsigned)len, millis() - t0, (int)WiFi.RSSI());
        return false;
      }
      vTaskDelay(5 / portTICK_PERIOD_MS);
      continue;
    }
    sent += n;
    t0 = millis();
  }
  return true;
}

// Dau khung tu kinh. `extra` = so byte dung truoc payload trong cung khung (byte
// loai cua binary). Tra ve so byte dau khung.
//
// 🔴 Khoa mat na (mask key) = 0x00000000. RFC bat client PHAI dat bit mask va
// gui 4 byte khoa; khoa 0 van hop le ve cu phap, va XOR voi 0 la khong doi —
// nen KHONG phai chep + xor 60 KB anh qua mot bo dem trung gian. Yeu cau "khoa
// khong doan duoc" cua RFC la de chong dau doc cache cua proxy tu JavaScript
// trong trinh duyet; kinh khong chay ma la nao, nen khong co gi de chong.
static size_t frameHead(uint8_t *h, uint8_t op, size_t payload) {
  size_t n = 0;
  h[n++] = 0x80 | op;                       // FIN + opcode
  if (payload < 126) {
    h[n++] = 0x80 | (uint8_t)payload;
  } else if (payload <= 0xFFFF) {
    h[n++] = 0x80 | 126;
    h[n++] = (uint8_t)(payload >> 8);
    h[n++] = (uint8_t)payload;
  } else {
    h[n++] = 0x80 | 127;
    for (int i = 7; i >= 0; i--) h[n++] = (uint8_t)((uint64_t)payload >> (8 * i));
  }
  h[n++] = 0; h[n++] = 0; h[n++] = 0; h[n++] = 0;   // khoa mat na = 0
  return n;
}

static bool sendFrame(uint8_t op, const uint8_t *pre, size_t preLen,
                      const uint8_t *p, size_t n) {
  if (!g_open) return false;
  uint8_t head[16 + 512];
  size_t  h = frameHead(head, op, preLen + n);
  if (preLen) { memcpy(head + h, pre, preLen); h += preLen; }

  // Khung nho (JSON, ping, khoi tieng 1 KB) gom vao MOT lan ghi = mot goi tin.
  // TCP_NODELAY dang bat, ghi ba lan la ba goi.
  bool ok;
  if (n <= sizeof(head) - h) {
    if (n) memcpy(head + h, p, n);
    ok = writeAll(head, h + n);
  } else {
    ok = writeAll(head, h) && writeAll(p, n);
  }
  if (!ok) { g_c.stop(); g_open = false; }
  return ok;
}

bool wsSendText(const char *s, size_t n) {
  return sendFrame(0x1, nullptr, 0, (const uint8_t *)s, n);
}

bool wsSendTyped(uint8_t type, const uint8_t *p, size_t n) {
  return sendFrame(0x2, &type, 1, p, n);
}

void wsClose(uint16_t code) {
  if (g_open) {
    uint8_t c[2] = { (uint8_t)(code >> 8), (uint8_t)code };
    sendFrame(0x8, nullptr, 0, c, 2);
  }
  g_c.stop();
  g_open = false;
}

void wsAbort() {
  g_c.stop();
  g_open = false;
}

// ============================================================================
// Bat tay
// ============================================================================
// Doc mot dong HTTP, NHUONG THAT trong luc cho.
//
// 🔴 Khong dung readStringUntil(): no quay vong trong timedRead() va nhuong
// bang yield(), ma yield() khong nhuong cho IDLE (uu tien thap nhat) — task
// watchdog ban. Da do that tren board 2026-08-27 voi duong POST cu.
static bool readLine(String &out, unsigned long deadline) {
  out = "";
  for (;;) {
    while (g_c.available()) {
      int ch = g_c.read();
      if (ch < 0) break;
      if (ch == '\n') { out.trim(); return true; }
      if (out.length() < 256) out += (char)ch;
    }
    if (!g_c.connected()) return false;
    if ((long)(millis() - deadline) > 0) return false;
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

static String acceptFor(const char *key) {
  static const char *GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  String s = String(key) + GUID;
  uint8_t sha[20];
  mbedtls_sha1((const unsigned char *)s.c_str(), s.length(), sha);
  unsigned char b64[32];
  size_t olen = 0;
  mbedtls_base64_encode(b64, sizeof(b64), &olen, sha, sizeof(sha));
  b64[olen] = 0;
  return String((const char *)b64);
}

bool wsOpen(const char *host, uint16_t port, const char *path) {
  wsClose(1000);

#if API_USE_TLS
  // ⚠️ Bo qua kiem tra chung chi — giong duong POST cu, xem huong dan GD 6.
  g_c.setInsecure();
  // 🔴 Bat tay TLS co dong ho RIENG, mac dinh 120 giay. Thieu dong nay da do
  // duoc 124304 ms cho mot lan "timeout 15 giay". Don vi la GIAY.
  g_c.setHandshakeTimeout(NET_HANDSHAKE_S);
#endif
  // 🔴 Dat TRUOC connect. start_ssl_client() nap gia tri nay vao
  // SO_SNDTIMEO/SO_RCVTIMEO ngay luc connect; dat sau la vo tac dung.
  g_c.setConnectionTimeout(WS_IO_MS);
  g_c.setTimeout(WS_IO_MS);

  unsigned long t = millis();
  if (!g_c.connect(host, port)) {
    Serial.printf("WS: %s %s:%u HONG sau %lu ms (RSSI %d, khoi lien RAM noi %u B)\n",
                  API_USE_TLS ? "TLS" : "TCP", host, (unsigned)port, millis() - t,
                  (int)WiFi.RSSI(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    g_c.stop();
    return false;
  }
  unsigned long tTls = millis() - t;
  // Ping, turn_end, played deu la khung vai chuc byte: Nagle giu chung lai
  // cho toi khi ACK ve la cong them ca trip vao dung luc nguoi dung dang cho.
  g_c.setNoDelay(true);

  uint8_t raw[16];
  for (int i = 0; i < 16; i += 4) {
    uint32_t r = esp_random();
    memcpy(raw + i, &r, 4);
  }
  unsigned char key[32];
  size_t klen = 0;
  mbedtls_base64_encode(key, sizeof(key), &klen, raw, sizeof(raw));
  key[klen] = 0;

  String hostHdr = String(host);
  if (port != 443 && port != 80) hostHdr += ":" + String(port);

  String req = String("GET ") + path + " HTTP/1.1\r\n"
               "Host: " + hostHdr + "\r\n"
               "User-Agent: ESP32S3-VisionCare/" FW_VERSION "\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n"
               "Sec-WebSocket-Key: " + (const char *)key + "\r\n"
               "Sec-WebSocket-Version: 13\r\n\r\n";
  if (!writeAll((const uint8_t *)req.c_str(), req.length())) {
    g_c.stop();
    return false;
  }

  unsigned long deadline = millis() + WS_UPGRADE_MS;
  String line;
  if (!readLine(line, deadline)) {
    Serial.println("WS: server khong tra dong trang thai");
    g_c.stop();
    return false;
  }
  bool is101 = line.indexOf(" 101") > 0;
  if (!is101) Serial.printf("WS: server tra '%s' thay vi 101\n", line.c_str());

  String want = acceptFor((const char *)key), got;
  for (;;) {
    if (!readLine(line, deadline)) { g_c.stop(); return false; }
    if (line.length() == 0) break;
    String low = line; low.toLowerCase();
    if (low.startsWith("sec-websocket-accept:")) { got = line.substring(21); got.trim(); }
  }
  if (!is101) { g_c.stop(); return false; }
  if (got != want) {
    // Proxy/captive portal tra 101 gia thi Accept khong khop — dung o day con
    // hon doc rac thanh khung WebSocket.
    Serial.println("WS: Sec-WebSocket-Accept khong khop — khong phai server that");
    g_c.stop();
    return false;
  }

  g_open   = true;
  g_lastRx = millis();
  Serial.printf("WS: da mo %s://%s%s (%s %lu ms, tong %lu ms)\n",
                API_USE_TLS ? "wss" : "ws", hostHdr.c_str(), path,
                API_USE_TLS ? "TLS" : "TCP", tTls, millis() - t);
  return true;
}

// ============================================================================
// Nhan
// ============================================================================
// May trang thai cat khung. Ton tai qua nhieu lan goi wsPoll() vi mot khung co
// the den vun thanh nhieu goi TCP.
enum RxState : uint8_t { RX_HEAD, RX_CTRL, RX_DATA };

static RxState  s_st = RX_HEAD;
static uint8_t  s_head[14];
static uint8_t  s_headLen = 0, s_headNeed = 2;
static uint8_t  s_op = 0;           // opcode cua khung dang doc
static bool     s_fin = false;
static uint64_t s_left = 0;         // payload con lai cua khung dang doc
static uint8_t  s_msgOp = 0;        // opcode cua THONG DIEP (khung dau, khong phai 0)
static bool     s_msgFirst = true;

static uint8_t  s_ctl[125];
static uint8_t  s_ctlLen = 0;

// Mieng giao cho nguoi goi. 2 KB: mot khung JSON thuong nam tron, khoi 0x02
// thi chia vai mieng.
static uint8_t  s_buf[2048];

static void rxReset() {
  s_st = RX_HEAD; s_headLen = 0; s_headNeed = 2;
  s_msgFirst = true;
}

static WsPiece closed(uint16_t code) {
  g_c.stop();
  g_open = false;
  rxReset();
  WsPiece p = {};
  p.kind = WS_CLOSED;
  p.closeCode = code;
  return p;
}

WsPiece wsPoll() {
  WsPiece none = {};
  none.kind = WS_NOTHING;
  if (!g_open) return none;

  for (;;) {
    int av = g_c.available();
    if (av <= 0) {
      if (!g_c.connected()) return closed(1006);
      return none;
    }
    g_lastRx = millis();

    if (s_st == RX_HEAD) {
      int ch = g_c.read();
      if (ch < 0) return none;
      s_head[s_headLen++] = (uint8_t)ch;
      if (s_headLen == 2) {
        uint8_t l7 = s_head[1] & 0x7F;
        s_headNeed = 2 + (l7 == 126 ? 2 : l7 == 127 ? 8 : 0) + ((s_head[1] & 0x80) ? 4 : 0);
      }
      if (s_headLen < s_headNeed) continue;

      s_fin = s_head[0] & 0x80;
      s_op  = s_head[0] & 0x0F;
      uint8_t l7 = s_head[1] & 0x7F;
      if (l7 == 126)      s_left = ((uint64_t)s_head[2] << 8) | s_head[3];
      else if (l7 == 127) { s_left = 0; for (int i = 0; i < 8; i++) s_left = (s_left << 8) | s_head[2 + i]; }
      else                s_left = l7;
      // Server KHONG duoc mask (RFC). Gap mask la khong phai server ta biet.
      if (s_head[1] & 0x80) {
        Serial.println("WS: server gui khung co mask — dong");
        wsClose(1002);
        return closed(1002);
      }
      s_headLen = 0; s_headNeed = 2;

      if (s_op & 0x08) {
        if (s_left > sizeof(s_ctl)) { wsClose(1002); return closed(1002); }
        s_ctlLen = 0;
        s_st = RX_CTRL;
      } else {
        if (s_op != 0) { s_msgOp = s_op; s_msgFirst = true; }
        s_st = RX_DATA;
        if (s_left == 0) {
          // Khung rong van la mot thong diep (vd text rong) — giao di de nguoi
          // goi khong ket o trang thai "dang ghep".
          s_st = RX_HEAD;
          WsPiece p = {};
          p.kind = WS_DATA; p.text = (s_msgOp == 0x1);
          p.first = s_msgFirst; p.last = s_fin; p.data = s_buf; p.len = 0;
          s_msgFirst = false;
          if (s_fin) s_msgFirst = true;
          return p;
        }
      }
      continue;
    }

    if (s_st == RX_CTRL) {
      while (s_ctlLen < s_left && g_c.available()) {
        int ch = g_c.read();
        if (ch < 0) break;
        s_ctl[s_ctlLen++] = (uint8_t)ch;
      }
      if (s_ctlLen < s_left) return none;
      s_st = RX_HEAD;

      if (s_op == 0x9) {                       // ping -> pong cung payload
        sendFrame(0xA, nullptr, 0, s_ctl, s_ctlLen);
      } else if (s_op == 0x8) {                // close
        uint16_t code = s_ctlLen >= 2 ? ((uint16_t)s_ctl[0] << 8) | s_ctl[1] : 1005;
        // Tra lai close cho dung phep roi dong. Server khong doi duoc cung
        // khong sao — socket da dong ve phia ta.
        uint8_t c[2] = { (uint8_t)(code >> 8), (uint8_t)code };
        sendFrame(0x8, nullptr, 0, c, 2);
        return closed(code);
      }
      continue;                                // pong: bo qua
    }

    // RX_DATA
    size_t want = s_left < sizeof(s_buf) ? (size_t)s_left : sizeof(s_buf);
    if ((size_t)av < want) want = av;
    int n = g_c.read(s_buf, want);
    if (n <= 0) return none;
    s_left -= (size_t)n;

    WsPiece p = {};
    p.kind  = WS_DATA;
    p.text  = (s_msgOp == 0x1);
    p.first = s_msgFirst;
    p.last  = s_fin && s_left == 0;
    p.data  = s_buf;
    p.len   = (size_t)n;
    s_msgFirst = false;
    if (s_left == 0) {
      s_st = RX_HEAD;
      if (s_fin) s_msgFirst = true;
    }
    return p;
  }
}
