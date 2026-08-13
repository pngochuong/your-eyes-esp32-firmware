#ifndef APP_CONFIG_H
#define APP_CONFIG_H

// ============================================================================
// Cau hinh dung chung cho ca du an
// ============================================================================
// Day la file DUY NHAT can sua khi doi mang, doi server hay doi day noi chan.
// Cac nut van rieng cua tung khoi (do loi phat, muc dem, nguong do net...) nam
// trong chinh file .cpp cua khoi do, ngay canh doan ma dung toi chung.
//
// Chon model camera va so do chan van o board_config.h / camera_pins.h.
// ============================================================================
#include <Arduino.h>

// ---------------------------------------------------------------------- WiFi
// Ten mang va mat khau KHONG nam o day nua — chung o ngay dau VisionCare.ino,
// la file dau tien mo len khi nap chuong trinh. Doi mang la viec lam thuong
// xuyen nhat, khong nen bat nguoi dung phai lan sang file khac.
// Khai bao dung o src/net/wifi_manager.h.

// ------------------------------------------------------------------ Server
// Server xu ly. Doi 3 dong nay neu endpoint thay doi.
#define API_HOST    "api.visioncare-host.uk"
#define API_PATH    "/process"
#define API_PORT    443

// Do thuc te: server xu ly rat lau moi tra byte dau tien. Moi timeout duoi day
// phai rong hon con so do, neu khong se ngat ngay truoc khi co ket qua.
static const int NET_TIMEOUT_MS = 90000;

// Hai hang muc cho khac han nhau, khong duoc dung chung mot con so:
//
//   FIRST_AUDIO_MS — tu luc gui xong toi khi co DU tieng de mo loa. Server
//     con dang chay OCR + VLM + TTS. No con co the flush mot cuc im lang mo
//     dau de day header ra som, nen "co du lieu ve" KHONG co nghia la sap co
//     tieng. Rong tay o day.
//
//   BODY_GAP_MS — khoang im GIUA CHUNG khi da bat dau nhan duoc file. Luc nay
//     server dang do du lieu ra; im qua lau la xong hoac hong. Con phai co
//     con so nay vi server KHONG gui mieng ket thuc "0\r\n\r\n" va cung khong
//     dong ket noi — khong co no thi ESP32 ngoi cho vinh vien sau byte cuoi.
static const int FIRST_AUDIO_MS = 180000;

// 🔴 30 giay, khong phai 8. Da do that: server sinh tieng theo TUNG CAU, va
// khoang nghi giua hai cau co the vuot 8 giay khi TTS cham. De 8 giay thi
// firmware coi khoang nghi la het luong va VUT MAT phan con lai cua cau tra
// loi — da xay ra dung nhu vay, chi phat duoc 7 giay tieng roi bo.
// Cai gia cua so nay chi la cho them khi server that su chet; cai gia cua so
// qua nho la mat noi dung, nang hon nhieu.
static const int BODY_GAP_MS = 30000;

// ------------------------------------------------------------------ Camera
// Xung nhip cap cho camera (XCLK).
//
// Module dang dung: OV2640 ong kinh goc rong 120°, cap FPC 75mm.
//
// 🔴 Chieu dai cap la ly do con so nay khong con la 20 MHz. Cap 75mm dai gap
// hon ba lan cap 21mm di kem board, ma bus camera la 8 duong du lieu song
// song chay song song nhau, khong co boc chong nhieu. Cap cang dai thi dien
// dung ky sinh va nhieu xuyen kenh cang lon, suon xung cang bi bo tron — toi
// mot muc nao do ESP32 lay mau trung luc tin hieu chua on dinh.
//
// Trieu chung khi qua nhanh, xep theo muc do nang dan:
//   - anh co vet ngang, soc mau la, nua duoi bi lech
//   - log "EV-EOF-OVF" hoac "cam_hal: FB-OVF"
//   - esp_camera_init() tra loi 0x105 (ESP_ERR_NOT_FOUND)
//
// 16 MHz la muc an toan cho cap dai. Neu anh sach va on dinh thi co the thu
// nang lai 20 MHz de duoc khung hinh nhanh hon; thay vet soc thi ha ve 10 MHz.
//
// Danh doi: XCLK thap thi moi hang anh doc ra lau hon, nen cung mot muc phoi
// sang se mat nhieu thoi gian thuc hon. Voi thiet bi doc chu khi dung yen thi
// khong dang ke; anh sach quan trong hon vai khung hinh moi giay.
#define CAM_XCLK_HZ  16000000

// ----------------------------------------------------------- Chan I2S / nut
#define PIN_BCLK    GPIO_NUM_41
#define PIN_WS      GPIO_NUM_42
#define PIN_DOUT    GPIO_NUM_21
#define PIN_DIN     GPIO_NUM_47
#define BTN         1
#define BTN_ACTIVE  LOW

// ------------------------------------------------------------ Den chieu sang
// Anh mo vi PHONG TOI, khong phai vi thiet lap sai. Trong toi, AEC chi co hai
// duong: keo dai thoi gian phoi sang (-> nhoe chuyen dong) hoac tang gain
// (-> nhieu hat). Ca hai deu giet OCR. Moi thiet lap phan mem chi dich can can
// giua hai cai do chu KHONG tao them anh sang — day la vat ly, khong phai bug.
//
// Loi thoat duy nhat: them den. Noi mot LED trang vao chan duoi day (qua tro
// ~100R xuong GND, hoac dung chan den co san tren board) thi thoi gian phoi
// tut xuong vai ms va gain ve gan 1X — het ca nhoe LAN nhieu, cung mot luc.
//
// Cac chan CON TRONG tren board nay (camera an 4-13,15-18; audio an 21,41,42,47;
// nut an 1): 2, 3, 14, 19, 20, 38, 39, 40, 45, 46, 48.
// Nhieu board ESP32-S3 co san LED o GPIO 48.
//
// Dat -1 de tat hoan toan.
#define FLASH_LED_PIN   -1
#define FLASH_LED_ON    HIGH    // doi thanh LOW neu LED noi kieu keo xuong
#define FLASH_SETTLE_MS 250     // cho AEC tinh lai sau khi bat den

// Ha cong suat phat WiFi trong luc dang phat loa. Radio bung dinh ~300 mA
// moi lan phat; khong co tu bulk thi dinh do di thang vao rail va nghe
// duoc thanh tieng lach tach deu deu. Luc dang phat loa thi khong can
// truyen gi, nen ha xuong khong mat mat gi. Dat 0 de tat tinh nang nay.
#define LOWER_WIFI_TX   1

// ---------------------------------------------------------------- Thu am
static const uint32_t SR      = 16000;   // tan so thu cua mic
static const int    MAX_SECS  = 10;      // tran thu am khi co PSRAM
static const int    DRAM_SECS = 3;       // tran thu am khi KHONG co PSRAM
static const int    MIN_MS    = 300;     // nhan ngan hon coi nhu bam nham

#endif  // APP_CONFIG_H
