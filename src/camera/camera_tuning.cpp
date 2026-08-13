#include "camera_tuning.h"

#include "camera_device.h"
#include "../../board_config.h"

// =====================================================
// Cau hinh cam bien cho OCR + mo ta canh
// =====================================================
void cameraApplySettings(sensor_t *sensor) {
  if (sensor == nullptr) {
    Serial.println("Khong tim thay camera sensor");
    return;
  }

  // ---------------------------------------------------
  // Do phan giai va JPEG
  // ---------------------------------------------------
  // Lay dung gia tri da nap vao camera_config_t. Ep cung UXGA o day se
  // keo nguoc board khong PSRAM len lai do phan giai no khong kham noi.
  sensor->set_framesize(sensor, cameraFrameSize());

  // JPEG: gia tri cang nho thi anh cang dep va file cang lon.
  // Duoi 10, gioi han chat luong la ong kinh OV2640 chu khong phai
  // muc nen, nen OCR khong doc them duoc gi ma file lai nang hon
  // va de gap loi tran buffer (EV-EOF-OVF) lam anh cut nua duoi.
  sensor->set_quality(sensor, cameraJpegQuality());

  // ---------------------------------------------------
  // Mau sac va do tuong phan
  // ---------------------------------------------------
  sensor->set_brightness(sensor, 0);

  // Tang nhe tuong phan de chu de tach khoi nen.
  sensor->set_contrast(sensor, 1);

  // Giu nguyen mau. PaddleOCR chuyen grayscale nen giam saturation
  // khong giup OCR, chi lam ngheo tin hieu mau ma VLM dua vao de
  // mo ta khong gian (ao do, den xanh, vach vang...).
  sensor->set_saturation(sensor, 0);

  // Khong dung hieu ung mau dac biet.
  sensor->set_special_effect(sensor, 0);

  // ---------------------------------------------------
  // Do net va khu nhieu
  // ---------------------------------------------------
  if (sensor->set_sharpness != nullptr) {
    // Khong nen de 2 ngay tu dau vi co the tao quang quanh chu.
    sensor->set_sharpness(sensor, 1);
  }

  if (sensor->set_denoise != nullptr) {
    sensor->set_denoise(sensor, 1);
  }

  // ---------------------------------------------------
  // Can bang trang
  // ---------------------------------------------------
  sensor->set_whitebal(sensor, 1);
  sensor->set_awb_gain(sensor, 1);
  sensor->set_wb_mode(sensor, 0);  // Auto

  // ---------------------------------------------------
  // Phoi sang tu dong
  // ---------------------------------------------------
  sensor->set_exposure_ctrl(sensor, 1);
  sensor->set_aec2(sensor, 1);

  // Giu 0. Da thu -1 de rut ngan phoi sang trong phong toi, nhung thiet bi
  // nay chi can doc chu o noi du sang — ma luc du sang thi AEC von da dung
  // thoi gian phoi rat ngan, ha them chi lam anh toi va giam tuong phan chu.
  sensor->set_ae_level(sensor, 0);

  // ---------------------------------------------------
  // Gain tu dong
  // ---------------------------------------------------
  sensor->set_gain_ctrl(sensor, 1);

  // Cho AEC dung gain thay vi keo dai thoi gian phoi sang.
  // Doi nhieu hat lay anh net: OCR chiu nhieu tot hon chiu nhoe
  // chuyen dong khi nguoi dung vua di vua chup.
  //
  // Da thu 16X de rut ngan phoi sang, nhung do ra thi anh mo KHONG phai do
  // phoi sang (cac khung chup lien tiep giong het nhau, doi phoi sang khong
  // cai thien). 16X chi doi lay them nhieu hat ma khong duoc gi — ma nhieu
  // hat lam hong mau, thu VLM can de mo ta canh. Tra ve 8X.
  sensor->set_gainceiling(sensor, GAINCEILING_8X);

  // ---------------------------------------------------
  // Sua loi diem anh va thau kinh
  // ---------------------------------------------------
  sensor->set_bpc(sensor, 1);
  sensor->set_wpc(sensor, 1);
  sensor->set_raw_gma(sensor, 1);

  // Bu giam sang ria anh. Voi ong kinh 120° day KHONG con la tuy chon:
  // goc cang rong thi ria anh cang toi so voi tam (hieu ung vignette tang
  // rat nhanh theo goc). Tat cai nay thi chu nam sat mep khung se chim han,
  // va do chinh la vung ong kinh goc rong thu them duoc so voi ong thuong.
  sensor->set_lenc(sensor, 1);

  sensor->set_dcw(sensor, 1);

  // Khong bat thanh mau test.
  sensor->set_colorbar(sensor, 0);

  // ---------------------------------------------------
  // Huong hinh
  // ---------------------------------------------------
#if defined(CAMERA_MODEL_ESP32S3_EYE)
  // Board ESP32-S3-EYE thuong can lat doc.
  sensor->set_vflip(sensor, 1);
  sensor->set_hmirror(sensor, 0);
#else
  sensor->set_vflip(sensor, 0);
  sensor->set_hmirror(sensor, 0);
#endif
}

// =====================================================
// Bo cac frame dau de auto exposure va AWB on dinh
// =====================================================
void cameraWarmUp(uint8_t frameCount) {
  Serial.println("Dang on dinh camera...");

  // Dem hai loai hong de phan biet "chua hoi tu" voi "cap co van de".
  uint8_t failed = 0;      // khong lay duoc frame
  uint8_t tiny   = 0;      // frame nho bat thuong = anh cut / hong du lieu

  for (uint8_t i = 0; i < frameCount; i++) {
    camera_fb_t *frameBuffer = esp_camera_fb_get();

    if (frameBuffer != nullptr) {
      // JPEG UXGA cua canh that hiem khi duoi 8 KB. Nho hon the gan nhu luon
      // la frame cut vi loi truyen tren bus, khong phai canh don gian.
      bool suspicious = frameBuffer->len < 8000;
      if (suspicious) tiny++;

      Serial.printf(
        "Frame %u: %u x %u, %u byte%s\n",
        static_cast<unsigned>(i + 1),
        static_cast<unsigned>(frameBuffer->width),
        static_cast<unsigned>(frameBuffer->height),
        static_cast<unsigned>(frameBuffer->len),
        suspicious ? "   <- NHO BAT THUONG" : ""
      );

      esp_camera_fb_return(frameBuffer);
    } else {
      failed++;
      Serial.printf(
        "Khong lay duoc frame %u\n",
        static_cast<unsigned>(i + 1)
      );
    }

    delay(120);
  }

  // Cap FPC 75mm la doan dai khong boc chong nhieu, va la cho hong de xay ra
  // nhat sau khi thay module: cam chua het, khoa chua gat, hoac gap gay mach.
  // Bat ngay o day, dung de nguoi dung di truy nguyen nhan tu tam anh xau.
  if (failed > 0 || tiny > 0) {
    Serial.println();
    Serial.printf("CANH BAO: %u frame hong, %u frame nho bat thuong.\n",
                  static_cast<unsigned>(failed), static_cast<unsigned>(tiny));
    Serial.println("Nghi ngo duong truyen camera. Kiem tra theo thu tu:");
    Serial.println("  1. Cap FPC cam het chan chua, khoa da gat chua");
    Serial.println("  2. Mat tiep xuc cua cap dat dung chieu chua");
    Serial.println("  3. Ha CAM_XCLK_HZ xuong 10000000 roi nap lai");
    Serial.println();
  }
}

// =====================================================
// In lai profile dang ap dung
// =====================================================
void cameraPrintSettings() {
  Serial.println();
  Serial.println("SETTING HIEN TAI:");
  Serial.printf("- Resolution  : %s\n", cameraFrameSizeName(cameraFrameSize()));
  Serial.printf("- Quality     : %d\n", cameraJpegQuality());
  Serial.println("- Brightness  : 0");
  Serial.println("- Contrast    : 1");
  Serial.println("- Saturation  : 0");
  Serial.println("- Sharpness   : 1");
  Serial.println("- Denoise     : 1");
  Serial.println("- Gain ceiling: 8X");
  Serial.println();
}
