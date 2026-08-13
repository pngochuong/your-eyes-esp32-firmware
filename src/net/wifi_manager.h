#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

// ============================================================================
// Ket noi va giam sat Wi-Fi
// ============================================================================
#include <Arduino.h>

// Ten mang va mat khau. Dinh nghia o dau VisionCare.ino — sua o do, khong sua
// o day. De extern nen doi mang chi phai bien dich lai sketch, khong dung toi
// cac khoi khac.
extern const char* WIFI_SSID;
extern const char* WIFI_PASSWORD;

// Noi vao mang bang hai bien tren. Tra ve true neu vao duoc, false neu het
// thoi gian cho. In day du dia chi IPv4 va IPv6 de con chan doan.
bool wifiConnect(unsigned long timeoutMs);

// Kiem tra duong mang theo tung chang: WiFi -> DNS -> TCP/TLS toi API_HOST.
// Khong chup, khong thu, khong gui — chi de biet hong o chang nao. Neu chua
// vao duoc mang thi quet song thay cho ba chang do.
void wifiSelfTest();

// Liet ke moi AP 2.4 GHz bat duoc, danh dau cai trung ten voi WIFI_SSID.
// Tu chay khi wifiConnect() het gio; goi tay bang lenh 's' tren Serial.
void wifiScanReport();

#endif  // WIFI_MANAGER_H
