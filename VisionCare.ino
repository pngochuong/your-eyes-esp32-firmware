// ============================================================================
// VisionCare — camera OCR + mo ta canh + tra loi bang giong noi
// ============================================================================
// File nay CHI lo trinh tu khoi dong va viec trong chung Wi-Fi. Moi chi tiet
// nam trong src/, moi thu muc mot viec:
//
//   src/camera/  bat camera, nap profile cam bien, chup anh de gui
//   src/net/     Wi-Fi, doc than HTTP, POST anh + tieng len server
//   src/web/     web server xem truc tiep va chinh cam bien
//   src/audio/   I2S, thu mic, xu ly tin hieu, giai ma MP3, phat ra loa
//   src/diag/    cac lenh chan doan go qua Serial
//   src/util/    tien ich chung
//
// Ten + mat khau Wi-Fi ngay ben duoi. Endpoint, chan GPIO o app_config.h.
// Chon model camera o board_config.h.
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>

#include "app_config.h"
#include "src/audio/audio_service.h"
#include "src/camera/camera_device.h"
#include "src/camera/camera_tuning.h"
#include "src/net/wifi_manager.h"
#include "src/web/web_led.h"
#include "src/web/web_server.h"

// ============================== SUA O DAY ==================================
// Ten mang va mat khau Wi-Fi. Doi hai dong nay roi nap lai la xong.
//
// 🔴 ESP32-S3 chi bat duoc song 2.4 GHz. Mang 5 GHz se KHONG hien ra du go
// dung ten — router hai bang tan dung chung mot ten van OK, nhung neu nha
// mang tach thanh hai ten rieng thi phai go ten cua bang 2.4 GHz.
//
// Ten mang phan biet chu hoa chu thuong va tinh ca dau cach. Mang mo (khong
// mat khau) thi de mat khau la chuoi rong "".
const char* WIFI_SSID     = "Huong";
const char* WIFI_PASSWORD = "20061029";
// ===========================================================================

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println("YOUR EYES - CAMERA OCR + SCENE + AUDIO");
  Serial.println("========================================");

  // Khong co camera thi khong con gi de phuc vu: dung han o day, loop() se
  // ngu tiep va khong dung web server len.
  if (!cameraStart()) return;

  webLedBegin();

  // Mat Wi-Fi thi KHONG return: nut bam, mic va loa van phai hoat dong,
  // va loop() ben duoi se tu ket noi lai khi song quay ve.
  bool wifiOk = wifiConnect(30000);

  if (wifiOk) {
    startCameraServer();

    Serial.println();
    Serial.println("Camera Ready!");
    Serial.print("Mo trinh duyet: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Bo qua web server vi chua co mang");
  }

  // Goi SAU startCameraServer() de camera va httpd chiem RAM truoc;
  // phan PSRAM con lai moi la phan audio that su duoc dung.
  if (!startAudio()) {
    Serial.println("Audio KHONG khoi dong duoc — camera van chay binh thuong");
  }

  cameraPrintSettings();
}

void loop() {
  // Web server va camera chay trong task rieng, audio chay trong task
  // "audio" ghim o core 1. Loop chi con viec trong chung Wi-Fi: rot mang
  // giua chung thi task audio khong POST duoc, nen phai noi lai.
  static bool serverStarted = (WiFi.status() == WL_CONNECTED);

  if (!cameraIsReady()) {
    delay(10000);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Mat WiFi — dang ket noi lai...");
    WiFi.disconnect();

    if (wifiConnect(20000) && !serverStarted) {
      // Lan dau vao duoc mang sau khi setup() that bai: gio moi co IP
      // de web server bind vao.
      startCameraServer();
      serverStarted = true;

      Serial.print("Mo trinh duyet: http://");
      Serial.println(WiFi.localIP());
    }
  }

  delay(10000);
}
