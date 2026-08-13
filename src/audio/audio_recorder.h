#ifndef AUDIO_RECORDER_H
#define AUDIO_RECORDER_H

// ============================================================================
// Thu am tu mic vao recBuf[]
// ============================================================================
// Chi doc mic. Loc, chan on va khuech dai la viec cua audio_dsp.*.
// ============================================================================
#include <Arduino.h>

// Thu cho toi khi nha nut hoac cham tran. Tra ve so byte PCM thu duoc.
size_t recordWhileHeld();

// Thu 1.5 giay im lang roi tra ve nen nhieu NGHE DUOC (da loc thong cao 76 Hz).
// Dung cho phep thu A/B khi chan doan, khong gui di dau. Ham nay CO sua
// recBuf[], nen dung goi khi dang giu mot ban thu can dung.
double measureNoiseOnly();

#endif  // AUDIO_RECORDER_H
