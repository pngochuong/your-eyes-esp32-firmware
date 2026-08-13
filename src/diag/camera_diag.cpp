#include "camera_diag.h"

#include "../camera/camera_capture.h"

void diagCaptureTest() {
  Serial.println("\n===== THU CHUP (khong gui) =====");
  unsigned long t = millis();
  if (photoCapture()) {
    Serial.printf("Mat %lu ms\n", millis() - t);
    float sharp = photoMeasureSharpness(photoData(), photoSize(), 1600, 1200);
    Serial.printf("Do net = %.1f\n", sharp);
  }
  photoRelease();
}
