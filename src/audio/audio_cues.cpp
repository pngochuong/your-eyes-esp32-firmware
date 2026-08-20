#include <Arduino.h>

#include "audio_cues.h"

#include <math.h>

#include "audio_i2s.h"
#include "cue_sent_pcm.h"
#include "../../app_config.h"

// ------------------------------------------------------------ chinh loa re
// CUE_GAIN: bien do cua hai tieng bao, tinh theo he so cua mau goc.
//
// Thap hon han PLAY_GAIN_PCT ben audio_player, va co ly do: hai tieng nay
// khong mang noi dung gi, chung chi bao hieu. Keu to bang cau tra loi la vua
// gay giat minh vua lam sut rail 5 V dung luc amp sap phai phat cau tra loi.
//
// Con re o hai tieng bao: ha 0.30 → 0.20. Khong nghe thay: nang len 0.45.
static const float CUE_GAIN = 0.30f;

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

// Nhu tren nhung ha muc theo CUE_GAIN truoc khi day.
//
// Phai chep qua mot bo dem tam vi nguon nam trong FLASH — khong the nhan tai
// cho. Dem 256 mau (512 byte) la du nho de khong dung toi stack cua task
// audio, ma van du to de khong goi i2s_channel_write() qua vun.
static void pushScaled(const int16_t *pcm, size_t nSamples) {
  int16_t tmp[256];
  for (size_t off = 0; off < nSamples; off += 256) {
    size_t n = nSamples - off < 256 ? nSamples - off : 256;
    for (size_t i = 0; i < n; i++) {
      // int32_t roi moi nhan: int16 x he so o dang float roi ep lai deu an
      // toan, nhung di qua int32 thi khong phu thuoc vao thu tu ep kieu.
      int32_t v = (int32_t)(pcm[off + i] * CUE_GAIN);
      tmp[i] = (int16_t)v;
    }
    pushRaw(tmp, n);
  }
}

void cueSent() {
  // Neu ai do doi SR ma quen sinh lai mang thi tieng se cao/thap sai nhip.
  // Bao mot dong con hon de nguoi dung tu doan tai sao tieng nghe la.
  if (CUE_SENT_RATE != SR)
    Serial.printf("CANH BAO: tieng bao %u Hz nhung I2S dang %u Hz\n",
                  (unsigned)CUE_SENT_RATE, (unsigned)SR);

  // Bo dem TX dang o tan so nao la do lan phat truoc quyet dinh. Sau khi phat
  // cau tra loi, playPcmStream() da tra ve SR roi; nhung lan bam dau tien thi
  // chua co gi dam bao, nen dat lai cho chac. Ham nay tu bo qua neu dang dung.
  i2sSetSampleRate(SR);

  // 🔴 Doc thang nhu mang RAM. KHONG pgm_read_word(): no tra uint16_t nen mau
  // am (0xFFFF = -1) hoa thanh 65535, lat nua duoi cua dang song thanh so
  // duong khong lo. Tren ESP32 flash anh xa vao khong gian dia chi nen doc
  // truc tiep la dung — PROGMEM chi co nghia tren AVR.
  pushScaled(CUE_SENT_PCM, CUE_SENT_SAMPLES);

  // Day not doan duoi ra khoi DMA va dua mang loa ve 0, neu khong se nghe mot
  // tieng "tach" khi dong im lang ke tiep toi.
  i2sWriteSilence(40);
}

void cueError() {
  const uint32_t FREQ    = 400;    // thap hon han tieng bao thanh cong
  const int      BEEP_MS = 150;
  const int      GAP_MS  = 90;
  // Song sin thuan o cung bien do nghe TO hon han tieng nhac — no dat het
  // nang luong vao mot tan so duy nhat. Nen lay CUE_GAIN roi ha them mot nua,
  // neu khong hai tieng nay se at han cau tra loi ma no dang bao truoc.
  const float    AMP     = CUE_GAIN * 0.5f;

  // Sinh tung mieng 20 ms thay vi dung mot mang 150 ms: task audio chi co
  // 12 KB stack, ma bat tay TLS trong cung task da an ~8 KB. Mot mang 2400
  // mau (4.8 KB) o day la duong ngan nhat toi tran stack.
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
      pushRaw(buf, n);       // AMP da gom san CUE_GAIN o tren
    }
    i2sWriteSilence(beep == 0 ? GAP_MS : 40);
  }
}
