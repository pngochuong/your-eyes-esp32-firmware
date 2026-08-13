#include "audio_recorder.h"

#include <math.h>
#include "audio_buffers.h"
#include "audio_dsp.h"
#include "audio_i2s.h"
#include "../../app_config.h"

// ============================================================================
// Thu am cho toi khi nha nut
// ============================================================================
// Doc tung mieng nho (~32 ms) de vong lap con kip thay nut da nha.
// Tra ve so byte PCM thu duoc.
size_t recordWhileHeld() {
  const size_t CHUNK = 512 * sizeof(int16_t);
  size_t got = 0, n;
  unsigned long t0 = millis();

  // 🔴 KHONG tat kenh TX o day de "cat nhieu tu amp". Da thu va that bai:
  // full-duplex thi TX voi RX dung chung BCLK/WS, va xung nhip do PHIA TX
  // phat. Tat TX la tat luon dong ho cua mic — chi thu duoc phan con sot
  // trong DMA (~190 ms) roi i2s_channel_read() timeout lien tuc.
  // Do la ly do chieu nay khong doi xung voi i2s_channel_disable(rx) luc phat.
  // Muon tat amp khi thu thi phai dung chan SD_MODE cua MAX98357A, khong
  // phai dung I2S.

  while (digitalRead(BTN) == BTN_ACTIVE && got < recMaxSamp * sizeof(int16_t)) {
    size_t want = recMaxSamp * sizeof(int16_t) - got;
    if (want > CHUNK) want = CHUNK;
    esp_err_t e = i2s_channel_read(i2sRx, (uint8_t *)recBuf + got, want, &n,
                                   200 / portTICK_PERIOD_MS);
    if (e == ESP_ERR_TIMEOUT) continue;
    if (e != ESP_OK) {
      Serial.printf("LOI read: %s\n", esp_err_to_name(e));
      break;
    }
    got += n;
  }
  Serial.printf("Thu %lu ms, %u byte\n", millis() - t0, (unsigned)got);

  // Canh bao khi so byte khong khop voi thoi gian giu nut — dau hieu mic bi
  // doi, xung nhip hong, hoac read() timeout. Dung de mot lan nua troi qua
  // ma khong ai de y.
  {
    size_t expect = (size_t)(millis() - t0) * SR / 1000 * sizeof(int16_t);
    if (expect > 0 && got < expect / 2)
      Serial.printf("=> CHI THU DUOC %u%% so voi thoi gian giu nut. Mic dang bi doi.\n",
                    (unsigned)((uint64_t)got * 100 / expect));
  }

  if (got >= recMaxSamp * sizeof(int16_t))
    Serial.printf("(cham tran %u giay — phan nha nut sau do bi cat)\n",
                  (unsigned)(recMaxSamp / SR));
  return got;
}

// Thu 1.5 giay im lang roi tra ve nen nhieu NGHE DUOC (da loc thong cao 76 Hz).
// Dung cho phep thu A/B, khong gui di dau. Khong dung recBuf[] cho viec khac
// trong luc nay — chap nhan duoc vi chi chay khi go lenh chan doan.
double measureNoiseOnly() {
  const size_t want = (size_t)SR * 3 / 2 * sizeof(int16_t);   // 1.5 s
  size_t got = 0, n;
  unsigned long t0 = millis();

  while (got < want && millis() - t0 < 4000) {
    size_t take = want - got;
    if (take > 1024) take = 1024;
    esp_err_t e = i2s_channel_read(i2sRx, (uint8_t *)recBuf + got, take, &n,
                                   200 / portTICK_PERIOD_MS);
    if (e == ESP_ERR_TIMEOUT) continue;
    if (e != ESP_OK) break;
    got += n;
  }

  size_t cnt = got / sizeof(int16_t);
  if (cnt <= SR / 10) return -1.0;

  // Loc thong cao 76 Hz giong duong gui, de con so so sanh duoc voi log
  // cua lan bam nut binh thuong.
  const float A = 0.97f;
  float hpX = 0.0f, hpY = 0.0f;
  for (size_t i = 0; i < cnt; i++) {
    float x = (float)recBuf[i];
    float y = A * (hpY + x - hpX);
    hpX = x;
    hpY = y;
    recBuf[i] = y > 32767.0f ? 32767 : (y < -32768.0f ? -32768 : (int16_t)y);
  }
  double rms = quietestRms(cnt);

  // Tach dai tan ngay tai day. Day moi la thu phan biet duoc ba nguyen nhan
  // hoan toan khac nhau ma con so RMS tron gop chung lam mot:
  //   dai cao troi han  -> nhieu SO / buc xa dien tu
  //   dai thap troi han -> u nguon, HOAC tieng on that trong phong (quat,
  //                        dieu hoa, xe co) — hai thu nay deu nam o dai thap
  //   hai dai gan bang  -> nhieu nhiet cua chinh mic
  double hi = highBandRms(quietestStart, cnt);
  if (rms > 0 && hi >= 0) {
    double lo2 = rms * rms - hi * hi;
    double lo  = lo2 > 0 ? sqrt(lo2) : 0.0;
    Serial.printf("   (dai thap %.1f | dai cao %.1f)\n", lo, hi);
  }
  return rms;
}
