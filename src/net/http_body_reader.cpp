#include "http_body_reader.h"

#include "../../app_config.h"

void bodyInit(BodyReader &b, WiFiClientSecure *c, bool chunked, long clen) {
  b.c = c;
  b.chunked = chunked;
  b.left = chunked ? 0 : clen;   // chunked: 0 = phai doc dong kich thuoc truoc
  b.eof = false;
  b.lineLen = 0;
  b.line[0] = 0;
}

// Doc TOI DA `max` byte, tra ve ngay voi nhung gi dang co.
//   > 0 : so byte doc duoc
//   = 0 : chua co gi luc nay, goi lai sau
//   < 0 : het than (binh thuong) hoac dut ket noi
int bodyRead(BodyReader &b, uint8_t *dst, size_t max) {
  if (b.eof || max == 0) return -1;

  // Chunked: het mieng cu thi doc dong kich thuoc mieng moi.
  //
  // 🔴 KHONG dung readStringUntil() o day. Neu dong kich thuoc ve nua chung
  // ("\r\n" + "1" roi het), no CHAN roi tra ve manh cut "1" -> strtol ra 1 ->
  // khung chunked lech tu do tro di va moi thu doc sau deu la rac. Gom tung
  // byte vao b.line, giu qua nhieu lan goi, khong bao gio chan.
  if (b.chunked && b.left == 0) {
    while (b.c->available()) {
      int c = b.c->read();
      if (c < 0) break;

      if (c == '\n') {
        b.line[b.lineLen] = 0;
        uint8_t had = b.lineLen;
        b.lineLen = 0;
        if (had == 0) continue;                      // dong rong: CRLF cuoi mieng
        long n = strtol(b.line, NULL, 16);
        if (n <= 0) { b.eof = true; return -1; }     // mieng 0 = het than
        b.left = n;
        break;
      }
      if (c != '\r' && b.lineLen < sizeof(b.line) - 1)
        b.line[b.lineLen++] = (char)c;
    }

    if (b.left == 0) {                               // chua gom du dong
      if (!b.c->connected() && b.c->available() == 0) { b.eof = true; return -1; }
      return 0;
    }
  }

  if (!b.chunked && b.left <= 0) { b.eof = true; return -1; }

  size_t want = max;
  if ((long)want > b.left) want = (size_t)b.left;

  int n = b.c->read(dst, want);
  if (n > 0) { b.left -= n; return n; }

  if (!b.c->connected() && b.c->available() == 0) { b.eof = true; return -1; }
  return 0;
}

// Doc dung `len` byte, cho toi khi du. Chi dung cho header WAV (vai chuc byte).
bool bodyReadExact(BodyReader &b, uint8_t *dst, size_t len) {
  size_t got = 0;
  unsigned long t0 = millis();
  while (got < len) {
    int n = bodyRead(b, dst + got, len - got);
    if (n < 0) return false;
    if (n > 0) { got += n; t0 = millis(); continue; }
    if (millis() - t0 > FIRST_AUDIO_MS) return false;
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
  return true;
}
