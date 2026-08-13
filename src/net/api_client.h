#ifndef API_CLIENT_H
#define API_CLIENT_H

// ============================================================================
// Gui anh + tieng len server, doc header tra ve
// ============================================================================
// Khoi nay dung o duong MANG: mo TLS, dong goi multipart, doc dong trang thai
// va header. No KHONG phat gi ra loa — sau khi ham tra ve true, nguoi goi cam
// lay `body` de doc PCM/MP3 va tu quyet dinh phat the nao.
// ============================================================================
#include <Arduino.h>
#include <WiFiClientSecure.h>

#include "http_body_reader.h"
#include "../audio/audio_format.h"

struct ApiReply {
  WiFiClientSecure client;
  BodyReader       body;

  bool     isMp3;    // than la MP3, phai giai ma truoc khi phat
  AudioFmt fmt;      // fmt.raw = true khi server bao x-audio-format (PCM tho)
  int      code;     // ma trang thai HTTP

  unsigned long tStart;   // moc millis() luc bat dau gui, de do tron mot lan
};

// Gui multipart (anh JPEG + ban thu WAV) roi doc het header tra ve.
// Tra ve true khi server tra 200 va than da san sang de doc.
// That bai thi ket noi da duoc dong san, khong can goi apiClose().
bool apiSendCapture(const uint8_t *jpg, size_t jpgLen,
                    const int16_t *pcm, size_t pcmBytes,
                    ApiReply &reply);

// Dong ket noi sau khi doc xong than.
void apiClose(ApiReply &reply);

#endif  // API_CLIENT_H
