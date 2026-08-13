#include "pcm_source.h"

#include "../util/mem_alloc.h"

// Helix sinh toi da 1152 mau x 2 kenh cho mot khung.
#define MP3_OUT_SAMPLES  (1152 * 2)
#define MP3_IN_SIZE      4096

void mp3Free(PcmSource &s) {
  if (s.dec) { MP3FreeDecoder(s.dec); s.dec = NULL; }
  if (s.in)  { free(s.in);  s.in  = NULL; }
  if (s.out) { free(s.out); s.out = NULL; }
}

bool mp3Init(PcmSource &s) {
  s.dec = MP3InitDecoder();
  s.in  = (uint8_t *)bigAlloc(MP3_IN_SIZE);
  s.out = (int16_t *)bigAlloc(MP3_OUT_SAMPLES * sizeof(int16_t));
  s.inLen = s.outLen = s.outPos = 0;
  s.srcEof = false;

  if (!s.dec || !s.in || !s.out) {
    Serial.println("Khong du RAM cho bo giai ma MP3");
    mp3Free(s);
    return false;
  }
  return true;
}

// Nap them du lieu nen tu socket vao dem vao. Tra ve false khi vua het luong
// vua khong con byte nao trong dem.
static bool mp3Fill(PcmSource &s) {
  if (s.inLen >= MP3_IN_SIZE) return true;
  int n = bodyRead(*s.body, s.in + s.inLen, MP3_IN_SIZE - s.inLen);
  if (n > 0) { s.inLen += n; return true; }
  if (n < 0) s.srcEof = true;
  return s.inLen > 0 || !s.srcEof;
}

// Giai mot khung. Tra ve so byte PCM sinh ra, 0 = chua du du lieu, -1 = het.
int mp3DecodeFrame(PcmSource &s) {
  if (s.inLen < 1024 && !s.srcEof) {
    if (!mp3Fill(s)) return -1;
    if (s.inLen == 0) return s.srcEof ? -1 : 0;
  }
  if (s.inLen == 0) return -1;

  int off = MP3FindSyncWord(s.in, s.inLen);
  if (off < 0) {
    // Khong co diem dong bo nao: bo het, tru lai vai byte cuoi phong khi
    // mau dong bo bi cat doi giua hai lan doc.
    size_t keep = s.inLen > 3 ? 3 : s.inLen;
    memmove(s.in, s.in + s.inLen - keep, keep);
    s.inLen = keep;
    return s.srcEof ? -1 : 0;
  }
  if (off > 0) {
    memmove(s.in, s.in + off, s.inLen - off);
    s.inLen -= off;
  }

  uint8_t *ptr  = s.in;
  int      left = (int)s.inLen;
  int      err  = MP3Decode(s.dec, &ptr, &left, s.out, 0);

  if (err == ERR_MP3_INDATA_UNDERFLOW || err == ERR_MP3_MAINDATA_UNDERFLOW) {
    // Khung chua ve du. Giu nguyen dem, doc them.
    return s.srcEof ? -1 : 0;
  }

  size_t consumed = s.inLen - (size_t)left;
  if (consumed > 0 && (size_t)left > 0) memmove(s.in, ptr, (size_t)left);
  s.inLen = (size_t)left;

  if (err != ERR_MP3_NONE) {
    // Khung hong: da bo qua no, thu khung ke tiep. MP3 tu dong bo lai duoc.
    return 0;
  }

  MP3FrameInfo fi;
  MP3GetLastFrameInfo(s.dec, &fi);
  return fi.outputSamps * (int)sizeof(int16_t);
}

// Doc toi da `max` byte PCM. Cung chu ky tra ve nhu bodyRead():
//   >0 so byte, 0 = chua co, <0 = het.
int pcmRead(PcmSource &s, uint8_t *dst, size_t max) {
  if (!s.isMp3) return bodyRead(*s.body, dst, max);

  if (s.outPos >= s.outLen) {
    int n = mp3DecodeFrame(s);
    if (n < 0) return -1;
    if (n == 0) return 0;
    s.outLen = (size_t)n;
    s.outPos = 0;
  }

  size_t have = s.outLen - s.outPos;
  if (have > max) have = max;
  memcpy(dst, (uint8_t *)s.out + s.outPos, have);
  s.outPos += have;
  return (int)have;
}
