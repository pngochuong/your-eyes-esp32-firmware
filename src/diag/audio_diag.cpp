#include "audio_diag.h"

#include <math.h>
#include <WiFi.h>
#include "esp_camera.h"

#include "../audio/audio_buffers.h"
#include "../audio/audio_i2s.h"
#include "../audio/audio_player.h"
#include "../audio/audio_recorder.h"
#include "../camera/camera_device.h"
#include "../../app_config.h"

// -------------------------------------------------------- do meo tieng loa
// Goertzel: nang luong tai DUNG mot tan so, khong phai lam ca FFT. Re, chinh
// xac, va chi can vai dong.
static double goertzel(const int16_t *x, size_t n, double freq) {
  double w     = 2.0 * PI * freq / (double)SR;
  double coeff = 2.0 * cos(w);
  double s1 = 0.0, s2 = 0.0;

  for (size_t i = 0; i < n; i++) {
    double s0 = (double)x[i] + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  double mag2 = s1 * s1 + s2 * s2 - coeff * s1 * s2;
  return (mag2 > 0 ? sqrt(mag2) : 0.0) / ((double)n / 2.0);
}

// Phat sin 440 Hz ra loa VA thu lai bang mic cung luc, roi do ty le hai so
// voi song co ban. Day la phep do khach quan thay cho viec nghe bang tai:
//   THD thap o moi muc          -> loa/amp tot, re den tu cho khac
//   THD tang manh theo bien do  -> sut nguon hoac amp qua tai (thieu tu loc)
//   THD cao o moi muc           -> loa hong hoac qua nho so voi cong suat
// Ngoai ra so "co ban" cho biet bien do ra co TANG TUYEN TINH theo % khong;
// khong tang nua la da cham tran.
//
// 400 mau o 16 kHz = dung 11 chu ky cua 440 Hz, nen lap lai lien mach khong
// co diem gay pha.
static void distortionTest(int pct) {
  const uint32_t F     = 440;
  const size_t   TONEN = 400;
  const size_t   RECN  = SR / 2;              // thu 0.5 s

  int16_t tone[TONEN];
  for (size_t i = 0; i < TONEN; i++)
    tone[i] = (int16_t)(sinf(2.0f * (float)PI * F * (float)i / (float)SR)
                        * (pct / 100.0f) * 32000.0f);

  size_t got = 0;
  unsigned long t0 = millis();

  while (got < RECN && millis() - t0 < 3000) {
    size_t w = 0, r = 0;
    i2s_channel_write(i2sTx, tone, sizeof(tone), &w, 200 / portTICK_PERIOD_MS);
    i2s_channel_read(i2sRx, (uint8_t *)(recBuf + got), sizeof(tone), &r,
                     200 / portTICK_PERIOD_MS);
    got += r / sizeof(int16_t);
  }

  if (got < SR / 5) { Serial.println("  thu khong du de do"); return; }

  // Bo 1/3 dau: loa con dang len tieng va AEC cua mic con dang chinh.
  size_t skip = got / 3;
  const int16_t *x = recBuf + skip;
  size_t n = got - skip;

  // Quet ca DUOI tan so goc, khong chi cac hai bac tren. Ly do: neu driver
  // rai mau xen ke hai khe thay vi nhan doi, tan so ra bi CHIA DOI — luc do
  // do o 440 Hz se thay gan nhu khong co gi, ma nang luong that nam o 220.
  // Chi do cac hai bac tren thi khong bao gio phat hien duoc loi kieu do.
  const double f[] = { 110, 220, 330, 440, 550, 660, 880, 1320 };
  double m[8];
  double peak = 0;
  int    peakI = 0;

  for (int i = 0; i < 8; i++) {
    m[i] = goertzel(x, n, f[i]);
    if (m[i] > peak) { peak = m[i]; peakI = i; }
  }

  Serial.printf("  %3d%%:", pct);
  for (int i = 0; i < 8; i++) Serial.printf(" %.0f=%.0f", f[i], m[i]);
  Serial.printf("   | dinh o %.0f Hz\n", f[peakI]);
}

// ============================================================================
// 'm' — so sanh cach ghi khe I2S
// ============================================================================
void diagSlotCompare() {
  // Nghe thu HAI kieu ghi khe, ngay sat nhau, de tai so sanh truc tiep.
  // Doi khe phai init lai kenh TX; RX khong dung toi nen khong anh huong.
  Serial.println("\n===== SO SANH CACH GHI KHE =====");
  i2s_channel_disable(i2sRx);

  // 🔴 Dung reconfig_std_slot, KHONG dung init_std_mode lan hai: IDF tra
  // loi "the channel has initialized already" va kenh ket o trang thai
  // da tat. Kenh phai DANG TAT khi reconfig.
  for (int k = 0; k < 2; k++) {
    bool both = (k == 1);
    i2sTxCfg.slot_cfg.slot_mask = both ? I2S_STD_SLOT_BOTH : I2S_STD_SLOT_LEFT;

    i2s_channel_disable(i2sTx);
    esp_err_t re = i2s_channel_reconfig_std_slot(i2sTx, &i2sTxCfg.slot_cfg);
    i2s_channel_enable(i2sTx);

    if (re != ESP_OK) {
      Serial.printf("Doi khe that bai: %s\n", esp_err_to_name(re));
      break;
    }
    i2sTxSlotBoth = both;

    Serial.printf("  [%c] %s — nghe ky\n", 'A' + k,
                  both ? "CA HAI khe (dang dung)" : "CHI khe TRAI (kieu cu)");
    playTone(440, 0.7f, 1500);
    i2sWriteSilence(500);
  }

  // Tra ve cau hinh dang dung.
  i2sTxCfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
  i2s_channel_disable(i2sTx);
  i2s_channel_reconfig_std_slot(i2sTx, &i2sTxCfg.slot_cfg);
  i2s_channel_enable(i2sTx);
  i2sTxSlotBoth = true;
  i2s_channel_enable(i2sRx);

  Serial.println("A = chi khe trai (cu), B = ca hai khe (moi).");
  Serial.println("Bao lai: cai nao TO hon, cai nao SACH hon?");
}

// ============================================================================
// 't' — thu loa bang song sin
// ============================================================================
void diagSpeakerTone() {
  // Phat song sin SACH do chinh ESP32 sinh ra, tang dan bien do.
  // Muc dich: tach phan cung khoi phan mem.
  //   sin cung re            -> loi o amp/loa/nguon, khong phai du lieu
  //   re tang theo bien do   -> sut nguon hoac amp qua tai (thieu tu loc)
  //   re ngay ca khi im lang -> tieng nen cua amp class-D luon dong cat
  //   sin sach het           -> loi nam o file server gui hoac duong phat
  Serial.println("\n===== THU LOA BANG SONG SIN =====");
  Serial.println("Nghe ky va nho: RE bat dau tu muc nao?");

  i2s_channel_disable(i2sRx);
#if LOWER_WIFI_TX
  wifi_power_t txSaved = WiFi.getTxPower();
  WiFi.setTxPower(WIFI_POWER_11dBm);
#endif
  Serial.println("  [1] Im lang 2 giay — day la tieng nen cua amp");
  i2sWriteSilence(2000);

  const int pct[] = { 25, 50, 70, 100 };
  for (int k = 0; k < 4; k++) {
    Serial.printf("  [%d] Sin 440 Hz, bien do %d%%\n", k + 2, pct[k]);
    playTone(440, pct[k] / 100.0f, 1200);
    i2sWriteSilence(300);
  }

  // Do bang may thay vi nghe bang tai. Mic nam ngay canh loa nen thu
  // lai duoc chinh tieng loa vua phat; hai bac cao la meo do amp/loa
  // sinh ra, khong phai co trong tin hieu goc.
  Serial.println("\n  --- DO MEO TIENG (mic thu lai tieng loa) ---");
  Serial.println("  Giu yen, dung noi gi trong 8 giay toi.");
  // Quet ca cac muc RAT THAP. Neu tieng ra da ngung tang tu 10% thi amp
  // bao hoa ngay tu day — luc do moi viec chinh phan mem deu vo nghia,
  // van de nam o nguon hoac o loa.
  if (i2s_channel_enable(i2sRx) == ESP_OK) {
    const int lv[] = { 5, 10, 20, 40, 70, 100 };
    for (int k = 0; k < 6; k++) distortionTest(lv[k]);
    i2s_channel_disable(i2sRx);
  }

#if LOWER_WIFI_TX
  WiFi.setTxPower(txSaved);
#endif
  if (i2s_channel_enable(i2sRx) != ESP_OK)
    Serial.println("Khong bat lai duoc mic");

  Serial.println("Xong. Bao lai: re tu muc nao?");
}

// ============================================================================
// 'n' — nen nhieu khi camera bat va khi camera tat
// ============================================================================
void diagNoiseVsCamera() {
  // Phep thu A/B: camera la nghi pham so mot cho nen nhieu tang gap 4
  // sau khi thay module 120 do cap 75mm. Cap FPC 75mm mang 8 duong du
  // lieu song song dap o 16 MHz — no la mot cai ang-ten. Tat han camera
  // roi do lai la biet ngay, khong phai doan.
  Serial.println("\n===== DO NEN NHIEU: CAMERA BAT vs TAT =====");
  Serial.println("Giu im lang trong 4 giay toi...");

  double onRms = measureNoiseOnly();
  Serial.printf("Camera BAT : nen nhieu = %.1f\n", onRms);

  esp_err_t de = esp_camera_deinit();
  if (de != ESP_OK) {
    Serial.printf("Khong tat duoc camera: %s\n", esp_err_to_name(de));
    return;
  }

  vTaskDelay(400 / portTICK_PERIOD_MS);   // cho rail on dinh lai
  double offRms = measureNoiseOnly();
  Serial.printf("Camera TAT : nen nhieu = %.1f\n", offRms);

  if (offRms > 0 && onRms > offRms * 1.5)
    Serial.printf("=> CAMERA LA NGUON NHIEU (gap %.1f lan). Tach day mic "
                  "ra xa cap FPC, hoac ha CAM_XCLK_HZ.\n", onRms / offRms);
  else
    Serial.println("=> Camera KHONG phai nguon nhieu. Tim o duong nguon mic.");

  Serial.println("Bat lai camera...");
  if (!cameraReinit()) Serial.println("LOI: khong bat lai duoc camera, hay reset board.");
}
