#include "camera_device.h"

#include "camera_tuning.h"
#include "../../app_config.h"
#include "../../board_config.h"

// Day la NGUON DUY NHAT cua do phan giai va muc nen. camera_config_t,
// cameraApplySettings() va cac dong log deu doc tu hai bien nay, nen
// khong con canh config mot dang, sensor mot neo.
//
// Hai bien la runtime chu khong phai constexpr vi nhanh khong co PSRAM
// ben duoi se ha chung xuong truoc khi esp_camera_init() chay.
static framesize_t camFrameSize  = FRAMESIZE_UXGA;  // 1600 x 1200
static int         camJpegQuality = 10;

// esp_camera_init() thanh cong hay chua.
static bool cameraReady = false;

framesize_t cameraFrameSize()   { return camFrameSize; }
int         cameraJpegQuality() { return camJpegQuality; }
bool        cameraIsReady()     { return cameraReady; }

// Ten hien thi cua do phan giai, chi dung cho log.
const char *cameraFrameSizeName(framesize_t size) {
  switch (size) {
    case FRAMESIZE_UXGA: return "UXGA 1600 x 1200";
    case FRAMESIZE_SXGA: return "SXGA 1280 x 1024";
    case FRAMESIZE_XGA:  return "XGA 1024 x 768";
    case FRAMESIZE_SVGA: return "SVGA 800 x 600";
    case FRAMESIZE_VGA:  return "VGA 640 x 480";
    default:             return "khac";
  }
}

// =====================================================
// Dung camera_config_t
// =====================================================
// Tach rieng khoi cameraStart() de con dung lai duoc sau esp_camera_deinit().
// Phep thu chan doan nhieu can tat camera roi bat lai; khong co ham nay thi
// toan bo cau hinh phai chep ra lam hai ban, va hai ban do chac chan se lech
// nhau sau vai lan sua.
static void buildCameraConfig(camera_config_t &config) {
  config = camera_config_t{};   // xoa sach, tranh field moi chua gia tri rac

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  // Bus du lieu camera
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  // Clock va dong bo
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  // SCCB/I2C dieu khien camera
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  // Clock camera
  config.xclk_freq_hz = CAM_XCLK_HZ;

  // JPEG de stream va gui server
  config.pixel_format = PIXFORMAT_JPEG;

  // ---------------------------------------------------
  // Kiem tra PSRAM TRUOC khi nap frame_size
  // ---------------------------------------------------
  // Quyet dinh do phan giai o day, roi moi do vao config. Nho vay
  // cameraApplySettings() doc lai dung gia tri nay.
  if (!psramFound()) {
    camFrameSize  = FRAMESIZE_SVGA;
    camJpegQuality = 12;

    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  } else {
    // Mac dinh dung PSRAM.
    config.fb_location = CAMERA_FB_IN_PSRAM;

    // Dung hai buffer de luon co frame moi.
    config.fb_count = 2;

    // Bo frame cu, lay frame gan thoi diem bam nut nhat.
    config.grab_mode = CAMERA_GRAB_LATEST;
  }

  config.frame_size = camFrameSize;

  // Gia tri nho hon = JPEG tot hon. Giu khop voi set_quality().
  config.jpeg_quality = camJpegQuality;
}

// =====================================================
// In thong tin camera
// =====================================================
static void printCameraInfo(sensor_t *sensor) {
  Serial.println();
  Serial.println("========== CAMERA INFO ==========");

  if (sensor != nullptr) {
    Serial.printf("Sensor PID      : 0x%04X\n", sensor->id.PID);
    Serial.printf("Sensor VER      : 0x%02X\n", sensor->id.VER);
    Serial.printf("XCLK            : %d Hz\n", sensor->xclk_freq_hz);
  }

  Serial.printf("PSRAM detected  : %s\n", psramFound() ? "YES" : "NO");
  // Ep kieu vi cac ham nay tra uint32_t (long unsigned int); %u mong doi
  // unsigned int. Tren ESP32 hai kieu cung 32-bit nen chay van dung, nhung
  // de nguyen thi build luon co canh bao.
  Serial.printf("PSRAM size      : %u byte\n", (unsigned)ESP.getPsramSize());
  Serial.printf("Free PSRAM      : %u byte\n", (unsigned)ESP.getFreePsram());
  Serial.printf("Free heap       : %u byte\n", (unsigned)ESP.getFreeHeap());

  Serial.println("Lens            : 120 do, cap FPC 75mm");
  Serial.printf("Resolution      : %s\n", cameraFrameSizeName(camFrameSize));
  Serial.printf("JPEG quality    : %d\n", camJpegQuality);
  Serial.printf("Grab mode       : %s\n",
                psramFound() ? "LATEST" : "WHEN_EMPTY");
  Serial.println("=================================");
  Serial.println();
}

// =====================================================
// Bat camera len
// =====================================================
bool cameraStart() {
  Serial.println(psramFound() ? "Da phat hien PSRAM"
                              : "CANH BAO: Khong phat hien PSRAM - ha xuong SVGA");

  camera_config_t config;
  buildCameraConfig(config);

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // ---------------------------------------------------
  // Khoi tao camera
  // ---------------------------------------------------
  Serial.println("Dang khoi tao camera...");

  esp_err_t cameraError = esp_camera_init(&config);

  if (cameraError != ESP_OK) {
    Serial.printf(
      "Camera init failed with error 0x%X\n",
      cameraError
    );

    Serial.println("Kiem tra:");
    Serial.println("- Camera model trong board_config.h");
    Serial.println("- Cap camera FPC");
    Serial.println("- PSRAM OPI trong Tools");
    Serial.println("- Nguon USB");

    return false;
  }

  Serial.println("Camera init thanh cong");
  cameraReady = true;

  // ---------------------------------------------------
  // Lay cam bien va ap dung profile
  // ---------------------------------------------------
  sensor_t *sensor = esp_camera_sensor_get();

  if (sensor == nullptr) {
    Serial.println("Khong lay duoc sensor camera");
    cameraReady = false;
    return false;
  }

  cameraApplySettings(sensor);

  // AEC/AGC/AWB cua OV2640 can khoang 10-15 frame moi hoi tu.
  cameraWarmUp(10);

  printCameraInfo(sensor);
  return true;
}

// Khoi tao lai camera sau esp_camera_deinit().
bool cameraReinit() {
  camera_config_t config;
  buildCameraConfig(config);

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init lai that bai: 0x%X\n", err);
    return false;
  }

  cameraApplySettings(esp_camera_sensor_get());
  return true;
}
