#ifndef WEB_LED_H
#define WEB_LED_H

// ============================================================================
// Den flash tren board, dieu khien qua giao dien web
// ============================================================================
// Khac voi den chieu sang cua camera_capture.* (chan rieng, bat theo lan bam
// nut): den nay la LED_GPIO_NUM co san tren board, chay bang PWM va do trang
// /control?var=led_intensity dieu chinh.
// ============================================================================
#include <Arduino.h>
#include "../../board_config.h"

// Chuan bi kenh PWM cho den. Khong lam gi neu board khong co LED_GPIO_NUM.
void webLedBegin();

#if defined(LED_GPIO_NUM)
extern int  led_duty;      // do sang dang dat, 0-255
extern bool isStreaming;   // dang co nguoi xem /stream

void enable_led(bool en);  // Turn LED On or Off
#endif

#endif  // WEB_LED_H
