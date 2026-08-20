#include "serial_console.h"

#include "audio_diag.h"
#include "../audio/audio_cues.h"
#include "../audio/audio_service.h"
#include "camera_diag.h"
#include "../net/api_client.h"
#include "../net/wifi_manager.h"
#include "../util/perf_probe.h"

void consolePoll() {
  if (!Serial.available()) return;

  int c = Serial.read();

  if (c == 'm' || c == 'M') {
    diagSlotCompare();
  } else if (c == 't' || c == 'T') {
    diagSpeakerTone();
  } else if (c == 'w' || c == 'W') {
    Serial.println("\n===== KIEM TRA MANG =====");
    wifiSelfTest();
    // In ca dia chi dang nho: neu no cu hoac tro sang mang khac thi moi lan
    // bam nut deu hong ma cac chang o tren van bao xanh het.
    apiPrintAddress();
  } else if (c == 'b' || c == 'B') {
    // Nghe thu hai tieng bao canh nhau. Nguoi dung khiem thi phai phan biet
    // duoc chung khi chi nghe mot lan, khong co co hoi so sanh.
    Serial.println("\n===== NGHE THU HAI TIENG BAO =====");
    Serial.println("1/2 — da gui xong (warm_melodic)");
    cueSent();
    delay(600);
    Serial.println("2/2 — gui hong (bip bip 400 Hz)");
    cueError();
  } else if (c == 'p' || c == 'P') {
    audioSimulatePress(3000);
  } else if (c == 'i' || c == 'I') {
    Serial.println("\n===== DAY LEN BANG GOI NHO (512 B, NoDelay) =====");
    wifiUploadTest(200 * 1024, 512, true);
  } else if (c == 'd' || c == 'D') {
    Serial.println("\n===== DO TOC DO TAI VE (khong TLS, host khac) =====");
    wifiDownloadTest();
  } else if (c == 'u' || c == 'U') {
    Serial.println("\n===== DO TOC DO DAY LEN WAN (khong TLS) =====");
    wifiUploadTest(200 * 1024, 4096, false);
  } else if (c == 's' || c == 'S') {
    Serial.println("\n===== QUET SONG WIFI =====");
    wifiScanReport();
  } else if (c == 'n' || c == 'N') {
    diagNoiseVsCamera();
  } else if (c == 'c' || c == 'C') {
    diagCaptureTest();
  } else if (c == 'r' || c == 'R') {
    memReport("theo yeu cau");
  }
}
