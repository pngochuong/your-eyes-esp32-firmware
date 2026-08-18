#ifndef API_CLIENT_H
#define API_CLIENT_H

// ============================================================================
// Gui anh + tieng len server, doc header tra ve
// ============================================================================
// Khoi nay dung o duong MANG: nho dia chi server, mo TLS, dong goi multipart,
// doc dong trang thai va header. No KHONG phat gi ra loa — sau khi co header,
// nguoi goi cam lay `body` de doc PCM/MP3 va tu quyet dinh phat the nao.
//
// ============================================================================
// Vi sao mot lan gui bi tach thanh BON buoc
// ============================================================================
// Do that tren hotspot 4G: duong day len chi 5.9-9 KB/s. Mot buc anh 720p
// khoang 65 KB mat 7 giay, ban thu 3 giay mat gan 3 giay nua. Neu doi nha nut
// roi moi bat dau lam tat ca thi nguoi dung ngoi cho tron 10 giay.
//
// Nhung anh da co TU LUC BAM NUT, va nguoi dung thi noi mat vai giay. Khoang
// do la thoi gian chet — du de day het buc anh len. Nen mot lan gui duoc chia
// nho de tung manh di ngay khi no san sang:
//
//   apiBegin()      luc bam nut     mo TLS
//   apiPushImage()  chup xong       day header + phan anh
//   apiAudioOpen()  bat dau thu     mo phan tieng cua multipart
//   apiPushAudio()  moi khoi 256 B  day khoi do len NGAY, con dang thu
//   apiWaitReply()  sau khi nha nut cho header tra ve
//
// Bon buoc dau tra ve TUC THI. Viec that chay trong mot task rieng ghim o core
// 0 (noi WiFi/TCP dang chay san), de core 1 danh tron cho I2S. Nho vay lenh
// keu tieng bao o audio_service chay SONG SONG voi luc dang day tieng len.
//
// 🔴 Than gui di dung Transfer-Encoding: chunked, khong dung Content-Length.
// Bat buoc phai vay: Content-Length doi biet TONG do dai ngay tu dong header
// dau tien, ma luc do ban thu con chua duoc thu. Da do that: server (FastAPI
// + uvicorn, sau Cloudflare) nhan chunked binh thuong, tra 200 — khong phai
// sua mot dong nao ben server.
// ============================================================================
#include <Arduino.h>
#include <WiFiClientSecure.h>

#include "http_body_reader.h"
#include "../audio/audio_format.h"

struct ApiReply {
  BodyReader body;

  bool     isMp3;    // than la MP3, phai giai ma truoc khi phat
  AudioFmt fmt;      // fmt.raw = true khi server bao x-audio-format (PCM tho)
  int      code;     // ma trang thai HTTP

  unsigned long tStart;   // moc millis() luc bat dau gui, de do tron mot lan
};

// ============================================================================
// Nho dia chi server — chay MOT lan luc khoi dong
// ============================================================================
// 🔴 Truoc kia moi lan bam nut deu goi WiFi.hostByName() roi
// client.connect(ten_mien,...) — tuc phan giai DNS HAI lan ngay tren duong bam
// nut. Do that tren hotspot Redmi: 14 giay moi luot, va tren mang chi cap DNS
// IPv6 thi luot hoi ban ghi A con chet han sau 7 giay.
//
// Gio phan giai dung mot lan, nho lai IP, va tu do connect BANG IP kem SNI.
// `force = true` de ep hoi lai (dia chi qua cu, hoac vua ket noi hong).
bool apiResolve(bool force);

// Dat thang dia chi server, bo qua DNS.
//
// 🔴 Nen dung cai nay thay cho apiResolve() khi co the: wifiConnect() da bat
// tay TLS tron ven toi mot dia chi cu the roi, va dia chi DA DUOC THU thi
// dang tin hon dia chi vua phan giai. Do that (2026-08-18): cung mot ten mien,
// 104.21.67.134 bat tay xong trong 3212 ms con 172.67.176.233 bat tay TCP xong
// nhung TLS treo vinh vien. Hoi lai DNS la tu nguyen doi mot dia chi da co
// bang chung lay mot dia chi chua biet gi.
void apiSetAddress(IPAddress ip);

// Da co dia chi trong tay chua.
bool apiHaveAddress();

// ============================================================================
// Bat tay TLS truoc, luc khoi dong, roi GIU phien lai
// ============================================================================
// 🔴 Truoc kia wifiConnect() tu bat tay mot lan de "chung minh duong di" roi
// stop() ngay. Do that (2026-08-19, hotspot iPhone): lan bat tay do XONG trong
// 5496 ms, nhung vut di; toi luc bam nut, bat tay lai tren DUNG dia chi ay
// chet o 8059 ms voi mbedtls -1. Tuc phep thu khong he du bao duoc lan that —
// no chi tieu mat mot lan bat tay dang le dung duoc.
//
// Ham nay lam dung viec do nhung khong vut: bat tay xong thi danh dau phien la
// dung lai duoc, va apiBegin() o lan bam dau tien di thang vao ghi byte. Bat
// tay la chang dat nhat con lai sau khi DNS da duoc nho san (do duoc 1321 toi
// 5496 ms), nen day la chang dang tiet kiem nhat.
//
// Tra ve true khi phien dang mo. Hong thi khong sao — apiBegin() se tu bat tay
// o lan bam nut.
bool apiWarmUp();

// In dia chi dang nho + tuoi cua no. Chi phuc vu lenh chan doan.
void apiPrintAddress();

// ============================================================================
// Bon buoc cua mot lan gui — xem giai thich o dau file
// ============================================================================

// Buoc 1, goi NGAY khi nut vua bam. Mo TLS trong task rieng. Tra ve tuc thi.
// Goi lai khi dang co mot lan gui do dang thi khong lam gi.
void apiBegin();

// Buoc 2, goi ngay sau khi chup xong. `jpg` phai con song toi khi
// apiWaitReply() tra ve — task mang doc thang tu do, khong sao chep.
void apiPushImage(const uint8_t *jpg, size_t jpgLen);

// Buoc 3a, goi MOT lan ngay truoc khi bat dau thu. Mo phan "audio" cua
// multipart va giao bo dem ma tieng se duoc nen dan vao. `buf` phai con song
// toi khi apiWaitReply() tra ve — task mang doc thang tu do, khong sao chep.
//
// 🔴 `filename` va `contentType` do NGUOI GOI dua xuong, khong hard-code o
// day. api_client khong duoc biet tieng ma hoa kieu gi — no chi day byte va
// dan nhan theo loi khai cua audio_service. Doi codec thi sua mot cho.
void apiAudioOpen(const uint8_t *buf, const char *filename,
                  const char *contentType);

// Buoc 3b, goi moi khi co them du lieu. `totalBytes` la so byte hop le tinh
// TU DAU bo dem da giao o apiAudioOpen() — chi duoc tang, khong duoc lui.
// `done = true` o lan goi cuoi: khong con byte nao nua, dong goi multipart lai.
//
// 🔴 Day la cho an tien cua ca luong. Nen + day tung khoi 256 byte ngay
// trong luc nguoi dung con dang noi thi luc nha nut chi con mot khoi cuoi phai
// day — do duoc duoi 0.2 giay tren moi duong truyen. Doi thu xong het roi moi
// gui la tu chuoc lay 3 giay ngoi im tren hotspot 9 KB/s.
//
// 🔴 Do la ly do than tieng KHONG con la file WAV. Header WAV phai di TRUOC
// du lieu ma ba truong cua no (RIFF size, data size, so mau trong `fact`) chi
// biet duoc khi da thu xong. Gio gui ADPCM tho, tham so nam trong
// `contentType`, server tu dung lai header.
void apiPushAudio(size_t totalBytes, bool done);

// Buoc 4. Chan cho toi khi doc xong header tra ve. Tra ve true khi server tra
// 200 va than da san sang doc qua `reply.body`.
// That bai thi ket noi da duoc dong san, khong can goi apiClose().
bool apiWaitReply(ApiReply &reply, unsigned long timeoutMs);

// Bo lan gui dang do (nhan qua ngan, khong co anh, mat WiFi...). Khong goi thi
// socket nam do om ~45 KB heap cho toi lan bam sau.
void apiAbort();

// Dong ket noi sau khi doc xong than.
void apiClose(ApiReply &reply);

#endif  // API_CLIENT_H
