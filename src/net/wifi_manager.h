#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

// ============================================================================
// Ket noi va giam sat Wi-Fi
// ============================================================================
#include <Arduino.h>

// Ten mang va mat khau. Dinh nghia o dau sketch chinh
// (your-eyes-esp32-firmware.ino) — sua o do, khong sua
// o day. De extern nen doi mang chi phai bien dich lai sketch, khong dung toi
// cac khoi khac.
extern const char* WIFI_SSID;
extern const char* WIFI_PASSWORD;

// Noi vao mang bang hai bien tren. Tra ve true neu vao duoc, false neu het
// thoi gian cho. In day du dia chi IPv4 va IPv6 de con chan doan.
bool wifiConnect(unsigned long timeoutMs);

// Dia chi server ma wifiConnect() da CHUNG MINH la dung duoc — no khong chi
// phan giai ra dia chi nay, no da bat tay TLS tron ven toi day roi.
// Tra ve false neu chua co (chua noi mang, hoac moi chang deu hong).
//
// 🔴 Co ham nay vi mot lan tra gia (2026-08-18): wifiConnect() bat tay TLS OK
// toi 104.21.67.134, roi api_client hoi DNS lai va nhan 172.67.176.233 — cai
// thu hai bat tay TCP xong nhung TLS thi TREO VINH VIEN (15 giay cung khong
// xong). Cloudflare tra nhieu IP cho mot ten mien va khong phai duong nao
// cung cho qua goi full-size, nen dia chi da DUOC THU moi la dia chi dang tin.
// Phan giai lai la tu nguyen bo bang chung da mua bang 3 giay bat tay.
bool wifiProvenServerIp(IPAddress &out);

// Kiem tra duong mang theo tung chang: WiFi -> DNS -> TCP/TLS toi API_HOST.
// Khong chup, khong thu, khong gui — chi de biet hong o chang nao. Neu chua
// vao duoc mang thi quet song thay cho ba chang do.
void wifiSelfTest();

// Day mot khoi du lieu that len WAN qua cong 80 (KHONG TLS) va do toc do.
//
// 🔴 wifiSelfTest() chi bat tay TLS roi dong ngay — no chua tung ghi mot byte
// nao SAU bat tay, nen dong "TCP/TLS OK" cua no chua bao gio chung minh phien
// cho duoc du lieu. Da tra gia: moi chang deu xanh ma lan bam nut nao cung
// chet o byte dau tien cua anh. Ham nay bit dung lo hong do, va bo TLS ra
// ngoai de tach hai nguyen nhan khac han nhau:
//   day nhanh      -> duong WAN tot, loi nam o TLS/mbedtls hoac o phia server
//   day khong noi  -> mang bop/chan chinh thiet bi nay, firmware vo can
// chunkBytes + noDelay quyet dinh KICH THUOC GOI thuc su bay len duong:
// noDelay = false thi Nagle gop cac lan ghi nho lai thanh goi day MSS, nen
// muon thu goi nho that su phai bat noDelay. Hai tham so nay ton tai de tra
// loi mot cau hoi cu the: duong ra chet vi TOC DO, hay vi KICH THUOC GOI.
void wifiUploadTest(size_t totalBytes, size_t chunkBytes, bool noDelay);

// Tai mot file co san tren mang ve va do toc do, cung qua cong 80 khong TLS.
//
// Di doi voi wifiUploadTest(): mot chieu len, mot chieu xuong. Hai so nay
// canh nhau moi noi duoc duong ra hong the nao — chi chieu len bi bop (dau
// hieu cua QoS/shaping theo thiet bi) hay ca hai chieu deu chet (duong ra
// hong han). Chu dich toi mot mirror KHAC han server API, de tach "mang bop
// thiet bi nay" khoi "rieng duong toi server API co van de".
void wifiDownloadTest();

// Liet ke moi AP 2.4 GHz bat duoc, danh dau cai trung ten voi WIFI_SSID.
// Tu chay khi wifiConnect() het gio; goi tay bang lenh 's' tren Serial.
void wifiScanReport();

#endif  // WIFI_MANAGER_H
