#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

// ============================================================================
// Phat ra loa
// ============================================================================
// Vua nhan vua phat, voi bo dem tu dieu chinh: do toc do nguon roi chon muc
// dem, vuot bien do quanh cho hut, va chuyen han sang "doi nhan xong" khi
// nguon chung to la khong theo kip.
//
// Khong biet du lieu tu dau ra — do la viec cua PcmSource.
// ============================================================================
#include <Arduino.h>

#include "audio_format.h"
#include "pcm_source.h"

// Phat het mot cau tra loi. `totalBytes` = tong so byte PCM neu biet truoc
// (WAV co khai bao), 0 neu khong biet. Tra ve true khi da phat duoc tieng.
bool playPcmStream(PcmSource &src, const AudioFmt &fmt, size_t totalBytes);

// Sinh song sin sach roi day thang ra loa. Dung recBuf[] lam cho dung song.
// Chi dung khi chan doan.
void playTone(uint32_t freq, float amp, int ms);

#endif  // AUDIO_PLAYER_H
