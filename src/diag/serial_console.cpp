#include "serial_console.h"

#include "audio_diag.h"
#include "../audio/audio_cues.h"
#include "../audio/audio_service.h"
#include "camera_diag.h"
#include "../camera/camera_live.h"
#include "../net/device_session.h"
#include "../net/device_token.h"
#include "../net/wifi_manager.h"
#include "../util/perf_probe.h"

// Lenh '=': nap token vao NVS. Doc phan con lai cua dong.
//
// 🔴 KHONG in token, ke ca mot phan — spec cam (FIRMWARE_ENDPOINTS muc 1, buoc
// 1) va log Serial hay bi dan vao chat/issue. Chi in do dai.
static void readToken() {
  char buf[DEVICE_TOKEN_MAX + 2];
  size_t n = 0;
  unsigned long t = millis();
  // Cho co gioi han: day la task audio, cho vo han la nut chet theo.
  while (millis() - t < 3000) {
    if (!Serial.available()) { vTaskDelay(5 / portTICK_PERIOD_MS); continue; }
    int c = Serial.read();
    if (c == '\n' || c == '\r') break;
    if (n < sizeof(buf) - 1) buf[n++] = (char)c;
    t = millis();
  }
  buf[n] = 0;
  // Bo khoang trang hai dau — dan tu .env hay dinh dau cach cuoi.
  char *s = buf;
  while (*s == ' ' || *s == '\t') s++;
  size_t len = strlen(s);
  while (len && (s[len - 1] == ' ' || s[len - 1] == '\t')) s[--len] = 0;

  if (len > DEVICE_TOKEN_MAX) {
    Serial.printf("Token dai %u ky tu, qua tran %u — KHONG luu\n",
                  (unsigned)len, (unsigned)DEVICE_TOKEN_MAX);
  } else if (tokenSave(s)) {
    Serial.printf("Da %s token (%u ky tu) vao NVS — mo lai phien\n",
                  len ? "luu" : "xoa", (unsigned)len);
    sessionKick();
  } else {
    Serial.println("Ghi NVS HONG — token khong doi");
  }
  memset(buf, 0, sizeof(buf));
}

void consolePoll() {
  if (!Serial.available()) return;

  int c = Serial.read();

  if (c == '=') {
    readToken();
  } else if (c == 'm' || c == 'M') {
    diagSlotCompare();
  } else if (c == 't' || c == 'T') {
    diagSpeakerTone();
  } else if (c == 'w' || c == 'W') {
    Serial.println("\n===== KIEM TRA MANG =====");
    wifiSelfTest();
    sessionPrintStatus();
  } else if (c == 'v' || c == 'V') {
    Serial.println("\n===== PHIEN WEBSOCKET =====");
    sessionPrintStatus();
    livePrintStatus();
  } else if (c == 'b' || c == 'B') {
    // Nghe thu tieng loi canh tieng "da nhan" cua server. Nguoi dung khiem thi
    // phai phan biet duoc chung khi chi nghe mot lan.
    Serial.println("\n===== NGHE THU TIENG BAO =====");
    Serial.println("tieng loi (bip bip 400 Hz)");
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
    // photoCapture() tra anh cu truoc khi chup — ma anh cu co the dang duoc
    // task mang day len. Cho duong len ranh moi chup.
    if (!sessionUplinkIdle()) Serial.println("Duong len dang day anh — thu lai sau");
    else                      diagCaptureTest();
  } else if (c == 'r' || c == 'R') {
    memReport("theo yeu cau");
  }
}
