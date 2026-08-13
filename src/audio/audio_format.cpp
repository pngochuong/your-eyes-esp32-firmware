#include "audio_format.h"

// Header WAV chuan 44 byte, PCM 16-bit mono.
void wavHeader(uint8_t *h, uint32_t pcmBytes, uint32_t rate) {
  uint32_t byteRate = rate * 2;          // mono, 2 byte/mau
  memcpy(h + 0,  "RIFF", 4);
  *(uint32_t *)(h + 4)  = 36 + pcmBytes;
  memcpy(h + 8,  "WAVEfmt ", 8);
  *(uint32_t *)(h + 16) = 16;            // kich thuoc khoi fmt
  *(uint16_t *)(h + 20) = 1;             // PCM khong nen
  *(uint16_t *)(h + 22) = 1;             // 1 kenh
  *(uint32_t *)(h + 24) = rate;
  *(uint32_t *)(h + 28) = byteRate;
  *(uint16_t *)(h + 32) = 2;             // block align
  *(uint16_t *)(h + 34) = 16;            // bit/mau
  memcpy(h + 36, "data", 4);
  *(uint32_t *)(h + 40) = pcmBytes;
}

bool parseAudioFormat(const String &v, AudioFmt &f) {
  String s = v;
  s.toLowerCase();
  s.trim();

  // Chi ho tro 16-bit little endian co dau — dung thu I2S dang cau hinh.
  if (s.indexOf("s16le") < 0) {
    Serial.printf("Dinh dang '%s' khong ho tro — can pcm_s16le\n", s.c_str());
    return false;
  }

  // Dung bien tam: hong o giua thi khong de lai mot AudioFmt nua voi nua sai.
  AudioFmt t = { true, 0, 1, 16 };

  int i = s.indexOf("rate=");
  if (i >= 0) t.rate = (uint32_t)s.substring(i + 5).toInt();
  i = s.indexOf("channels=");
  if (i >= 0) t.ch = (uint16_t)s.substring(i + 9).toInt();

  if (t.rate == 0 || (t.ch != 1 && t.ch != 2)) {
    Serial.printf("x-audio-format thieu rate/channels: '%s'\n", s.c_str());
    return false;
  }
  f = t;
  return true;
}

// Duyet cac khoi RIFF thay vi gia dinh header dai dung 44 byte: nhieu bo ma
// hoa chen them khoi LIST/fact, luc do PCM khong bat dau o offset 44.
bool readWavHeader(BodyReader &b, AudioFmt &f, size_t *dataLen) {
  uint8_t riff[12];
  if (!bodyReadExact(b, riff, sizeof(riff))) return false;
  if (memcmp(riff, "RIFF", 4) || memcmp(riff + 8, "WAVE", 4)) return false;

  uint16_t wfmt = 0;
  f.raw = false; f.rate = 0; f.ch = 0; f.bits = 0;
  *dataLen = 0;

  for (int guard = 0; guard < 16; guard++) {
    uint8_t ck[8];
    if (!bodyReadExact(b, ck, sizeof(ck))) return false;
    uint32_t sz;
    memcpy(&sz, ck + 4, 4);

    if (!memcmp(ck, "fmt ", 4) && sz >= 16) {
      uint8_t d[16];
      if (!bodyReadExact(b, d, sizeof(d))) return false;
      memcpy(&wfmt,   d + 0,  2);
      memcpy(&f.ch,   d + 2,  2);
      memcpy(&f.rate, d + 4,  4);
      memcpy(&f.bits, d + 14, 2);
      // Bo phan du cua khoi fmt neu co (WAVE_FORMAT_EXTENSIBLE).
      for (uint32_t left = (sz > 16 ? sz - 16 : 0) + (sz & 1); left > 0; ) {
        uint8_t sink[32];
        uint32_t take = left > sizeof(sink) ? sizeof(sink) : left;
        if (!bodyReadExact(b, sink, take)) return false;
        left -= take;
      }
    } else if (!memcmp(ck, "data", 4)) {
      // 0 hoac 0xFFFFFFFF = server dinh dang dan, chua biet dai bao nhieu.
      *dataLen = (sz == 0 || sz >= 0xFFFFFF00UL) ? 0 : (size_t)sz;
      return wfmt == 1 && f.bits == 16 && (f.ch == 1 || f.ch == 2) && f.rate > 0;
    } else {
      for (uint32_t left = sz + (sz & 1); left > 0; ) {
        uint8_t sink[32];
        uint32_t take = left > sizeof(sink) ? sizeof(sink) : left;
        if (!bodyReadExact(b, sink, take)) return false;
        left -= take;
      }
    }
  }
  return false;
}
