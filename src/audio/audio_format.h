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
//        x-audio-format: ima_adpcm;rate=16000;channels=1;block=256
//      Kieu nay hop voi streaming that: khong phai bia ra do dai file, cung
//      khong ton header o dau moi cau.
//   2. File WAV binh thuong, tu doc tan so trong khoi "fmt ".
// Co header o tren thi tin no; khong co thi lui ve doc RIFF.
struct AudioFmt {
  bool     raw;     // true = khong co header WAV, dinh dang bao qua HTTP header
  bool     adpcm;   // true = IMA ADPCM 4-bit; false = PCM 16-bit
  uint32_t rate;
  uint16_t ch;
  uint16_t bits;

  // Chi co nghia khi adpcm = true.
  uint16_t blockAlign;       // so byte mot khoi
  uint16_t samplesPerBlock;  // so mau mot khoi giai ra
};

// Header WAV chuan 44 byte, PCM 16-bit mono. `h` phai co it nhat 44 byte.
void wavHeader(uint8_t *h, uint32_t pcmBytes, uint32_t rate);

// Header WAV cho IMA ADPCM mono (WAVE_FORMAT_DVI_ADPCM = 0x0011).
// Dai hon ban PCM: khoi "fmt " co them cbSize + wSamplesPerBlock, va bat buoc
// co khoi "fact" mang SO MAU that — do la thu duy nhat cho ben giai ma biet
// phai cat bo bao nhieu mau dem o khoi cuoi.
//
// `h` phai co it nhat WAV_ADPCM_HDR byte. Tra ve so byte da ghi.
#define WAV_ADPCM_HDR  60
size_t wavHeaderAdpcm(uint8_t *h, uint32_t dataBytes, uint32_t samples,
                      uint32_t rate, uint16_t blockAlign,
                      uint16_t samplesPerBlock);

// Doc gia tri cua header "x-audio-format". Tra ve false neu khong ho tro.
bool parseAudioFormat(const String &v, AudioFmt &f);

// Doc header WAV NGAY TREN LUONG, khong doi tron file. Vai chuc byte dau nen
// khong ton thoi gian, doi lai biet duoc tan so, so kenh, VA tong do dai —
// con so cuoi cung la thu quyet dinh luc nao bat dau phat duoc an toan.
// Tra ve false neu khong phai WAV nhan duoc. *dataLen = 0 nghia la server
// khong bao truoc.
//
// 🔴 *dataLen la so byte cua khoi "data", tuc byte DA NEN khi f.adpcm = true.
// Duong phat can so byte PCM SAU khi giai — nguoi goi phai tu quy doi, xem
// audio_service.cpp. Khong quy doi thi cong thuc chon muc dem trong
// audio_player nham 4 lan va no phat qua som roi hut.
bool readWavHeader(BodyReader &b, AudioFmt &f, size_t *dataLen);

#endif  // AUDIO_FORMAT_H
