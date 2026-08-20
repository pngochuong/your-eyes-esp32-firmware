#include "adpcm_codec.h"

// Hai bang tra chuan cua IMA ADPCM. Khong duoc doi mot so nao: ben giai ma
// cua server dung dung hai bang nay, lech mot o la ra tieng rit.
static const int16_t stepTab[89] = {
      7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
     19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
     50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
    876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
   2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
   5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
  15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static const int8_t idxTab[16] = {
  -1, -1, -1, -1, 2, 4, 6, 8,
  -1, -1, -1, -1, 2, 4, 6, 8
};

static inline int16_t clampSample(int32_t v) {
  return v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v);
}

static inline int clampIndex(int i) {
  return i < 0 ? 0 : (i > 88 ? 88 : i);
}

// ---------------------------------------------------------------------------
// Mot mau. `pred` va `idx` la trang thai chay, ham sua tai cho.
//
// 🔴 Ben nen phai TAI DUNG lai mau y het cach ben giai se lam (bien `delta`
// duoi day), roi lay ket qua do lam moc cho mau ke tiep — khong duoc lay mau
// goc. Neu lay mau goc thi sai so moi mau khong bao gio duoc bu lai, no cong
// don suot ca khoi 505 mau va tieng troi han khoi duong bao.
// ---------------------------------------------------------------------------
static inline uint8_t encodeSample(int16_t sample, int16_t &pred, int &idx) {
  int32_t step = stepTab[idx];
  int32_t diff = (int32_t)sample - (int32_t)pred;

  uint8_t code = 0;
  if (diff < 0) { code = 8; diff = -diff; }

  int32_t delta = step >> 3;
  if (diff >= step) { code |= 4; diff -= step; delta += step; }
  step >>= 1;
  if (diff >= step) { code |= 2; diff -= step; delta += step; }
  step >>= 1;
  if (diff >= step) { code |= 1;              delta += step; }

  pred = clampSample((int32_t)pred + ((code & 8) ? -delta : delta));
  idx  = clampIndex(idx + idxTab[code]);
  return code;
}

static inline int16_t decodeSample(uint8_t code, int16_t &pred, int &idx) {
  int32_t step  = stepTab[idx];
  int32_t delta = step >> 3;
  if (code & 4) delta += step;
  if (code & 2) delta += step >> 1;
  if (code & 1) delta += step >> 2;

  pred = clampSample((int32_t)pred + ((code & 8) ? -delta : delta));
  idx  = clampIndex(idx + idxTab[code]);
  return pred;
}

// ---------------------------------------------------------------------------
size_t adpcmEncodedBytes(size_t samples) {
  if (samples == 0) return 0;
  size_t blocks = (samples + ADPCM_BLOCK_SAMPLES - 1) / ADPCM_BLOCK_SAMPLES;
  return blocks * ADPCM_BLOCK_BYTES;
}

size_t adpcmEncodeBlock(AdpcmEnc &st, const int16_t *pcm, size_t samples,
                        uint8_t *dst) {
  if (samples == 0) return 0;
  if (samples > (size_t)ADPCM_BLOCK_SAMPLES) samples = ADPCM_BLOCK_SAMPLES;

  // `pred` dat lai o dau moi khoi (bat buoc — phan dau khoi chua mau do
  // nguyen ven), rieng st.idx thi mang tiep tu khoi truoc. Xem ghi chu o header.
  int16_t pred = pcm[0];
  dst[0] = (uint8_t)(pred & 0xFF);
  dst[1] = (uint8_t)((pred >> 8) & 0xFF);
  dst[2] = (uint8_t)st.idx;
  dst[3] = 0;

  size_t  p  = 4;
  uint8_t lo = 0;
  for (size_t i = 1; i < samples; i++) {
    uint8_t code = encodeSample(pcm[i], pred, st.idx);
    if (i & 1) lo = code;                         // nibble thap truoc
    else       dst[p++] = (uint8_t)(lo | (code << 4));
  }
  if (!(samples & 1)) dst[p++] = lo;              // so mau chan -> con mot nua

  // Dem not khoi cho tron. Bo giai ma chuan doc theo khoi, khoi cut co the bi
  // bo nguyen — tuc mat toi 31 ms cuoi cau hoi cua nguoi dung.
  //
  // Dem bang 0x08 chu khong phai 0x00: nibble thap 8 = lui mot buoc/8, nibble
  // cao 0 = tien mot buoc/8, hai cai triet tieu nhau. Dem toan 0 thi moi mau
  // deu tien mot chieu, cong don thanh mot buoc DC o cuoi ban thu.
  //
  // 🔴 Da DO: ffmpeg KHONG cat theo khoi "fact" — no tra ve du 505 mau cua
  // khoi cuoi (do duoc: 32000 mau vao, 32320 mau ra). Nen phan dem phai tu no
  // vo hai, khong duoc trong cho ben kia don.
  while (p < (size_t)ADPCM_BLOCK_BYTES) dst[p++] = 0x08;
  return ADPCM_BLOCK_BYTES;
}

size_t adpcmEncode(const int16_t *pcm, size_t samples, uint8_t *dst) {
  AdpcmEnc st;
  adpcmEncReset(st);

  size_t out = 0, done = 0;
  while (done < samples) {
    size_t n = samples - done;
    if (n > (size_t)ADPCM_BLOCK_SAMPLES) n = ADPCM_BLOCK_SAMPLES;
    out  += adpcmEncodeBlock(st, pcm + done, n, dst + out);
    done += n;
  }
  return out;
}

size_t adpcmDecodeBlock(const uint8_t *src, size_t srcBytes,
                        int16_t *dst, size_t maxSamples) {
  if (srcBytes < 4 || maxSamples == 0) return 0;

  int16_t pred = (int16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
  int     idx  = clampIndex(src[2]);

  dst[0] = pred;
  size_t n = 1;

  for (size_t p = 4; p < srcBytes && n < maxSamples; p++) {
    dst[n++] = decodeSample(src[p] & 0x0F, pred, idx);
    if (n < maxSamples) dst[n++] = decodeSample(src[p] >> 4, pred, idx);
  }
  return n;
}
