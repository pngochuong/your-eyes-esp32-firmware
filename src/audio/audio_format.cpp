#include "audio_format.h"

// Ghi qua memcpy chu khong ep con tro. Header ADPCM co truong 16-bit nam o
// offset le so voi dau bo dem, ma Xtensa khong doc/ghi duoc tu dia chi lech —
// ep con tro o do la mot cai LoadStoreError luc chay, khong phai loi bien dich.
static inline void putU16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static inline void putU32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

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

size_t wavHeaderAdpcm(uint8_t *h, uint32_t dataBytes, uint32_t samples,
                      uint32_t rate, uint16_t blockAlign,
                      uint16_t samplesPerBlock) {
  uint32_t avgBps = samplesPerBlock
                  ? (uint32_t)((uint64_t)rate * blockAlign / samplesPerBlock)
                  : rate;

  memcpy(h + 0, "RIFF", 4);
  putU32(h + 4, (WAV_ADPCM_HDR - 8) + dataBytes);
  memcpy(h + 8, "WAVEfmt ", 8);

  putU32(h + 16, 20);                  // khoi fmt dai 20, khong phai 16
  putU16(h + 20, 0x0011);              // WAVE_FORMAT_DVI_ADPCM
  putU16(h + 22, 1);                   // mono
  putU32(h + 24, rate);
  // Byte/giay THAT: 16 kHz, khoi 256 byte, 505 mau/khoi -> 8110. ffmpeg ghi
  // 16000 vao cho nay (no lay bit_rate/8 chu khong tinh lai) — sai theo dac
  // ta. Khong sao lai theo no: da do, ffmpeg doc file cua ta binh thuong, vi
  // moi bo giai ma deu lay blockAlign + samplesPerBlock chu khong lay o day.
  putU32(h + 28, avgBps);
  putU16(h + 32, blockAlign);
  putU16(h + 34, 4);                   // 4 bit mot mau
  putU16(h + 36, 2);                   // cbSize: con 2 byte phan mo rong
  putU16(h + 38, samplesPerBlock);

  // 🔴 Khoi "fact" la BAT BUOC voi moi dinh dang nen. No mang so mau THAT,
  // nho do ben giai ma cat duoc phan dem cua khoi cuoi. Thieu no thi ffmpeg
  // van doc duoc nhung tra thua toi 505 mau im lang o duoi — va mot so bo
  // giai ma khac tu choi han file.
  memcpy(h + 40, "fact", 4);
  putU32(h + 44, 4);
  putU32(h + 48, samples);

  memcpy(h + 52, "data", 4);
  putU32(h + 56, dataBytes);
  return WAV_ADPCM_HDR;
}

bool parseAudioFormat(const String &v, AudioFmt &f) {
  String s = v;
  s.toLowerCase();
  s.trim();

  // Hai dinh dang duy nhat duong phat nay nuot duoc:
  //   pcm_s16le  — PCM 16-bit little endian, dung thu I2S dang cau hinh
  //   ima_adpcm  — IMA/DVI ADPCM 4-bit, giai ma o adpcm_codec truoc khi phat
  bool isAdpcm = s.indexOf("ima_adpcm") >= 0 || s.indexOf("adpcm") >= 0;
  if (!isAdpcm && s.indexOf("s16le") < 0) {
    Serial.printf("Dinh dang '%s' khong ho tro — can pcm_s16le hoac ima_adpcm\n",
                  s.c_str());
    return false;
  }

  // Dung bien tam: hong o giua thi khong de lai mot AudioFmt nua voi nua sai.
  AudioFmt t = {};
  t.raw   = true;
  t.adpcm = isAdpcm;
  t.ch    = 1;
  t.bits  = isAdpcm ? 4 : 16;

  int i = s.indexOf("rate=");
  if (i >= 0) t.rate = (uint32_t)s.substring(i + 5).toInt();
  i = s.indexOf("channels=");
  if (i >= 0) t.ch = (uint16_t)s.substring(i + 9).toInt();
  i = s.indexOf("block=");
  if (i >= 0) t.blockAlign = (uint16_t)s.substring(i + 6).toInt();

  if (t.rate == 0 || (t.ch != 1 && t.ch != 2)) {
    Serial.printf("x-audio-format thieu rate/channels: '%s'\n", s.c_str());
    return false;
  }

  if (isAdpcm) {
    // ADPCM chi lam mono: bien the stereo cai rang tung nibble theo kenh,
    // duong nay chua bao gio chay nen khong nhan bua.
    if (t.ch != 1) {
      Serial.println("ADPCM chi ho tro mono");
      return false;
    }
    // 🔴 Khong co mau dong bo trong ADPCM: ranh gioi khoi HOAN TOAN do
    // blockAlign quyet dinh. Doan sai con so nay thi moi khoi lay 4 byte giua
    // du lieu lam pred/idx — do duoc bang ffmpeg + Python: SNR tut tu 35 dB
    // xuong -10.7 dB, tuc nhieu thuan, nghe ra la "loa re rat re".
    //
    // Van phai co mac dinh de khong chet han, nhung PHAI keu len: mot con so
    // doan mo im lang la thu bien loi cau hinh cua server thanh loi phan cung
    // trong mat nguoi dung.
    if (t.blockAlign < 8) {
      t.blockAlign = 256;
      Serial.println("CANH BAO: x-audio-format bao ima_adpcm nhung thieu 'block='.");
      Serial.println("  Doan la 256. Doan sai = tieng re nhu nhieu trang. "
                     "Server phai ghi ro block=<so byte>.");
    }
    t.samplesPerBlock = (uint16_t)((t.blockAlign - 4) * 2 + 1);
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
  f = AudioFmt{};
  *dataLen = 0;

  for (int guard = 0; guard < 16; guard++) {
    uint8_t ck[8];
    if (!bodyReadExact(b, ck, sizeof(ck))) return false;
    uint32_t sz;
    memcpy(&sz, ck + 4, 4);

    if (!memcmp(ck, "fmt ", 4) && sz >= 16) {
      // Doc TOI 20 byte chu khong dung 16: voi ADPCM, hai byte quan trong nhat
      // (wSamplesPerBlock) nam o offset 18 — chinh la phan ma ban PCM khong co
      // va truoc day bi vut di cung voi phan du cua khoi.
      uint8_t  d[20];
      uint32_t take = sz >= 20 ? 20 : 16;
      if (!bodyReadExact(b, d, take)) return false;
      memcpy(&wfmt,         d + 0,  2);
      memcpy(&f.ch,         d + 2,  2);
      memcpy(&f.rate,       d + 4,  4);
      memcpy(&f.blockAlign, d + 12, 2);
      memcpy(&f.bits,       d + 14, 2);
      if (take == 20) memcpy(&f.samplesPerBlock, d + 18, 2);
      // Bo phan du cua khoi fmt neu co (WAVE_FORMAT_EXTENSIBLE).
      for (uint32_t left = (sz > take ? sz - take : 0) + (sz & 1); left > 0; ) {
        uint8_t sink[32];
        uint32_t take2 = left > sizeof(sink) ? sizeof(sink) : left;
        if (!bodyReadExact(b, sink, take2)) return false;
        left -= take2;
      }
    } else if (!memcmp(ck, "data", 4)) {
      // 0 hoac 0xFFFFFFFF = server dinh dang dan, chua biet dai bao nhieu.
      *dataLen = (sz == 0 || sz >= 0xFFFFFF00UL) ? 0 : (size_t)sz;
      if (f.rate == 0) return false;

      if (wfmt == 0x0011) {              // IMA/DVI ADPCM
        if (f.bits != 4 || f.ch != 1 || f.blockAlign < 8) return false;
        // Server bao khoi fmt ngan (16 byte) thi tu suy ra — cong thuc co
        // dinh cua dinh dang, khong phai phong doan.
        if (f.samplesPerBlock == 0)
          f.samplesPerBlock = (uint16_t)((f.blockAlign - 4) * 2 + 1);
        f.adpcm = true;
        return true;
      }
      return wfmt == 1 && f.bits == 16 && (f.ch == 1 || f.ch == 2);
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
