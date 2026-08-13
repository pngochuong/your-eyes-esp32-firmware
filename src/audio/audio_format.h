#ifndef AUDIO_FORMAT_H
#define AUDIO_FORMAT_H

// ============================================================================
// Dinh dang am thanh: header WAV va header x-audio-format
// ============================================================================
// Khoi la nay khong dung toi phan cung: no chi doc/ghi cac con so mo ta mot
// luong PCM. Nho vay ca duong gui (dong header WAV len server) lan duong nhan
// (doc dinh dang server tra ve) deu dung chung mot cho.
// ============================================================================
#include <Arduino.h>
#include "../net/http_body_reader.h"

// Server co the tra am thanh theo hai kieu:
//   1. PCM THO, khai bao dinh dang bang header rieng:
//        x-audio-format: pcm_s16le;rate=16000;channels=1
//      Kieu nay hop voi streaming that: khong phai bia ra do dai file, cung
//      khong ton 44 byte header o dau moi cau.
//   2. File WAV binh thuong, tu doc tan so trong khoi "fmt ".
// Co header o tren thi tin no; khong co thi lui ve doc RIFF.
struct AudioFmt {
  bool     raw;     // true = PCM tho, khong co header WAV
  uint32_t rate;
  uint16_t ch;
  uint16_t bits;
};

// Header WAV chuan 44 byte, PCM 16-bit mono. `h` phai co it nhat 44 byte.
void wavHeader(uint8_t *h, uint32_t pcmBytes, uint32_t rate);

// Doc gia tri cua header "x-audio-format". Tra ve false neu khong ho tro.
bool parseAudioFormat(const String &v, AudioFmt &f);

// Doc header WAV NGAY TREN LUONG, khong doi tron file. Vai chuc byte dau nen
// khong ton thoi gian, doi lai biet duoc tan so, so kenh, VA tong do dai —
// con so cuoi cung la thu quyet dinh luc nao bat dau phat duoc an toan.
// Tra ve false neu khong phai WAV. *dataLen = 0 nghia la server khong bao.
bool readWavHeader(BodyReader &b, AudioFmt &f, size_t *dataLen);

#endif  // AUDIO_FORMAT_H
