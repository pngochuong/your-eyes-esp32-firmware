#ifndef AUDIO_BUFFERS_H
#define AUDIO_BUFFERS_H

// ============================================================================
// Hai bo dem lon cua duong am thanh
// ============================================================================
// Ca hai deu xin MOT lan luc khoi dong roi giu den khi tat may. Cap phat theo
// lan bam nut se that bai bat ky luc nao PSRAM bi phan manh, va that bai giua
// chung thi nguoi dung chi thay may im lang.
//
//   recBuf   — ban thu tu mic, cung la cho playTone()/distortionTest() muon
//              de dung song thu.
//   stageBuf — bo dem trung gian giua mang va loa khi phat.
// ============================================================================
#include <Arduino.h>

extern int16_t *recBuf;      // buffer thu am
extern size_t   recMaxSamp;  // tran so mau thu duoc mot lan
extern uint8_t *stageBuf;    // bo dem phat
extern size_t   stageCap;    // suc chua cua stageBuf, tinh bang byte

// Xin ca hai bo dem. In ro con so va nguyen nhan neu that bai.
bool audioBuffersAlloc();

// Tra lai het RAM da xin. Goi o moi nhanh loi de camera khong bi giu lam
// mat vai tram KB PSRAM ma khong ai dung.
void audioBuffersFree();

#endif  // AUDIO_BUFFERS_H
