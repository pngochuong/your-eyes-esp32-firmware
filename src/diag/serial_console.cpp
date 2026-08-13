#include "serial_console.h"

#include "audio_diag.h"
#include "camera_diag.h"
#include "../net/wifi_manager.h"

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
  } else if (c == 's' || c == 'S') {
    Serial.println("\n===== QUET SONG WIFI =====");
    wifiScanReport();
  } else if (c == 'n' || c == 'N') {
    diagNoiseVsCamera();
  } else if (c == 'c' || c == 'C') {
    diagCaptureTest();
  }
}
