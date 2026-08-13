#ifndef PCM_SOURCE_H
#define PCM_SOURCE_H

// ============================================================================
// Nguon PCM: lay thang tu socket, hoac giai ma MP3 truoc
// ============================================================================
// Duong phat (audio_player.*) khong quan tam am thanh tu dau ra. Bo no sau
// mot lop nguon: PCM tho thi lay thang tu socket, MP3 thi giai ma truoc. Nho
// vay khong phai viet hai vong phat.
//
// Bo giai ma MP3 Helix chep thang vao src/libhelix-mp3 thay vi phu thuoc thu
// vien ngoai. Lay tu ESP8266Audio (giay phep RCSL/RPSL kem theo trong thu
// muc), nhung chi lay phan giai ma — khong keo theo ca framework audio voi
// lop I2S rieng cua no, vi cho nay da co duong I2S tu cau hinh.
// ============================================================================
#include <Arduino.h>

#include "../net/http_body_reader.h"

extern "C" {
#include "../libhelix-mp3/mp3dec.h"
}

struct PcmSource {
  BodyReader *body;
  bool        isMp3;

  // --- rieng cho MP3
  HMP3Decoder dec;
  uint8_t    *in;          // dem du lieu NEN doc tu socket
  size_t      inLen;       // so byte dang co trong `in`
  int16_t    *out;         // PCM cua mot khung vua giai
  size_t      outLen;      // so BYTE PCM dang co
  size_t      outPos;      // da lay ra bao nhieu byte
  bool        srcEof;      // socket het du lieu
};

// Xin RAM cho bo giai ma MP3. Chi goi khi than tra ve la MP3.
bool mp3Init(PcmSource &s);
void mp3Free(PcmSource &s);

// Giai mot khung. Tra ve so byte PCM sinh ra, 0 = chua du du lieu, -1 = het.
int mp3DecodeFrame(PcmSource &s);

// Doc toi da `max` byte PCM, bat ke nguon la gi. Cung chu ky tra ve nhu
// bodyRead(): >0 so byte, 0 = chua co, <0 = het.
int pcmRead(PcmSource &s, uint8_t *dst, size_t max);

#endif  // PCM_SOURCE_H
