#include <Arduino.h>

#include "audio_cues.h"

#include <math.h>

#include "audio_i2s.h"
#include "cue_clips_data.h"
#include "../../app_config.h"

// ------------------------------------------------------------ chinh loa re
// CUE_GAIN: bien do tieng bip loi, tinh theo he so cua 32000.
// Con re: ha 0.30 → 0.20. Khong nghe thay: nang len 0.45.
static const float CUE_GAIN = 0.30f;

int cueVersion() { return CUE_VERSION; }

const CueClip *cueFind(const char *id) {
  // 31 clip, so chuoi tuyen tinh la du — moi lan tra la mot lan phat clip,
  // khong nam tren duong nong nao.
  static CueClip hit;
  if (!id) return nullptr;
  for (size_t i = 0; i < CUE_CLIP_COUNT; i++) {
    if (strcmp(CUE_CLIP_TABLE[i].id, id) == 0) {
      hit.id      = CUE_CLIP_TABLE[i].id;
      hit.data    = CUE_CLIP_TABLE[i].data;
      hit.bytes   = CUE_CLIP_TABLE[i].bytes;
      hit.samples = CUE_CLIP_TABLE[i].samples;
      return &hit;
    }
  }
  Serial.printf("CANH BAO: khong co clip cue '%s' trong flash (cue_version %d)\n",
                id, CUE_VERSION);
  return nullptr;
}

// Day mot khoi mau ra TX, lap cho toi khi het. i2s_channel_write() co the ghi
// thieu khi DMA chua kip tieu thu, y het client.write() tren socket.
static void pushRaw(const int16_t *pcm, size_t nSamples) {
  size_t bytes = nSamples * sizeof(int16_t), put = 0, w;
  while (put < bytes) {
    if (i2s_channel_write(i2sTx, (const uint8_t *)pcm + put, bytes - put, &w,
                          1000 / portTICK_PERIOD_MS) != ESP_OK)
      break;
    put += w;
  }
}

void cueError() {
  const uint32_t FREQ    = 400;    // thap, nhip gat: khac han moi clip cua server
  const int      BEEP_MS = 150;
  const int      GAP_MS  = 90;
  // Song sin thuan o cung bien do nghe TO hon han tieng noi — no dat het
  // nang luong vao mot tan so duy nhat. Nen ha them mot nua.
  const float    AMP     = CUE_GAIN * 0.5f;

  // Sinh tung mieng 20 ms thay vi mot mang 150 ms: stack task audio co han.
  const size_t CHUNK = SR / 50;                  // 20 ms
  int16_t      buf[CHUNK];

  size_t total = (size_t)SR * BEEP_MS / 1000;
  size_t fade  = SR / 200;                       // vuot 5 ms hai dau
  if (fade > total / 2) fade = total / 2;

  i2sSetSampleRate(SR);

  for (int beep = 0; beep < 2; beep++) {
    for (size_t off = 0; off < total; off += CHUNK) {
      size_t n = total - off < CHUNK ? total - off : CHUNK;
      for (size_t i = 0; i < n; i++) {
        size_t k = off + i;
        float  v = sinf(2.0f * (float)PI * FREQ * (float)k / (float)SR)
                   * AMP * 32000.0f;
        // Vuot hai dau, neu khong tieng "tach" luc vao/ra bi nham la loa re.
        if (k < fade)              v = v * k / fade;
        else if (k >= total - fade) v = v * (total - 1 - k) / fade;
        buf[i] = (int16_t)v;
      }
      pushRaw(buf, n);
    }
    i2sWriteSilence(beep == 0 ? GAP_MS : 40);
  }
}
