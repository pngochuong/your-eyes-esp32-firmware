#include "audio_i2s.h"

#include "../../app_config.h"

i2s_chan_handle_t i2sTx = NULL;
i2s_chan_handle_t i2sRx = NULL;

i2s_std_config_t i2sTxCfg;
bool             i2sTxSlotBoth = true;

uint32_t i2sCurRate = SR;

// Doi tan so lay mau cua ca cum full-duplex. Vi tx/rx dung chung mot khoi
// clock, phai tat CA HAI truoc khi reconfig — reconfig khi dang enable se loi.
bool i2sSetSampleRate(uint32_t hz) {
  if (hz == i2sCurRate) return true;
  i2s_channel_disable(i2sTx);
  i2s_channel_disable(i2sRx);

  i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(hz);
  esp_err_t e1 = i2s_channel_reconfig_std_clock(i2sTx, &clk);
  esp_err_t e2 = i2s_channel_reconfig_std_clock(i2sRx, &clk);

  i2s_channel_enable(i2sTx);
  i2s_channel_enable(i2sRx);

  if (e1 != ESP_OK || e2 != ESP_OK) {
    Serial.printf("Doi sample rate that bai: %s / %s\n",
                  esp_err_to_name(e1), esp_err_to_name(e2));
    return false;
  }
  i2sCurRate = hz;
  return true;
}

// Bom `ms` mili giay im lang vao DMA.
void i2sWriteSilence(int ms) {
  static int16_t zeros[256] = { 0 };
  size_t need = (size_t)i2sCurRate * ms / 1000 * sizeof(int16_t);
  size_t put  = 0, n;
  while (put < need) {
    size_t want = need - put;
    if (want > sizeof(zeros)) want = sizeof(zeros);
    if (i2s_channel_write(i2sTx, zeros, want, &n, 200 / portTICK_PERIOD_MS) != ESP_OK) break;
    put += n;
  }
}

bool i2sBegin() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.auto_clear = true;

  // Noi dem DMA rong hon mac dinh. Day la lop chong giat cuoi cung: bo dem
  // PSRAM ben tren lo nhip nghen cua mang, con cho nay lo nhung luc CPU ban
  // (TLS, camera) khong kip quay lai day du lieu. 8 x 480 khung = 240 ms.
  cc.dma_desc_num  = 8;
  cc.dma_frame_num = 480;
  if (i2s_new_channel(&cc, &i2sTx, &i2sRx) != ESP_OK) {
    Serial.println("i2s_new_channel that bai");
    return false;
  }

  i2s_std_config_t sc = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SR),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                  I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = PIN_BCLK,
      .ws   = PIN_WS,
      .dout = PIN_DOUT,
      .din  = PIN_DIN,
      .invert_flags = { false, false, false },
    },
  };
  // 🔴 TX va RX phai dung slot_mask KHAC NHAU. Truoc day dung chung
  // I2S_STD_SLOT_LEFT cho ca hai — dung cho mic, SAI cho loa, va mat dung 6 dB.
  //
  // RX (INMP441): chan L/R noi GND nen no phat o khe TRAI. Lay khe trai.
  //
  // TX (MAX98357A): chan SD_MODE tha noi/keo len = che do mac dinh, amp phat
  // ra (TRAI + PHAI) / 2. Chi ghi khe trai thi khe phai toan so 0, amp lay
  // trung binh ra (L + 0)/2 = MOT NUA bien do. Ghi ca hai khe cung mot mau
  // thi (L + L)/2 = L, du bien do.
  //
  // Sau 6 dB do la ca hai trieu chung "nho" VA "re": tin hieu thap di mot
  // nua trong khi tieng nen cua amp khong doi, nen ty le nen/tin hieu xau
  // di dung 6 dB va tai nghe ra ngay.
  i2s_std_config_t rxc = sc;
  rxc.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

  i2sTxCfg = sc;
  i2sTxCfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
  i2sTxSlotBoth = true;

  // KHONG dung ESP_ERROR_CHECK: no abort ca chip, keo sap luon web server.
  esp_err_t e;
  if ((e = i2s_channel_init_std_mode(i2sTx, &i2sTxCfg)) != ESP_OK ||
      (e = i2s_channel_init_std_mode(i2sRx, &rxc))      != ESP_OK ||
      (e = i2s_channel_enable(i2sTx))                   != ESP_OK ||
      (e = i2s_channel_enable(i2sRx))                   != ESP_OK) {
    Serial.printf("I2S init that bai: %s — bo qua audio, camera van chay\n",
                  esp_err_to_name(e));
    return false;
  }
  i2sCurRate = SR;
  return true;
}
