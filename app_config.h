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
// Ten mang va mat khau KHONG nam o day nua — chung o ngay dau sketch chinh
// (your-eyes-esp32-firmware.ino),
// la file dau tien mo len khi nap chuong trinh. Doi mang la viec lam thuong
// xuyen nhat, khong nen bat nguoi dung phai lan sang file khac.
// Khai bao dung o src/net/wifi_manager.h.

// ------------------------------------------------------------------ Server
// WebSocket mot phien cho ca thoi gian song: wss://API_HOST/v2/session.
// Hop dong: FIRMWARE_ENDPOINTS.md. Doi endpoint thi sua dung khoi nay.
//
// 🔴 CONG TAC TLS.
// 1 = wss qua Cloudflare tunnel (ban dem di that).
// 0 = ws tran, noi THANG toi server trong LAN (`uvicorn ... --port 8000`).
//
// Lich su: 2026-08-27 da tam ve 0 vi mot phien mbedtls doi ~45 KB RAM noi
// LIEN, ma sau khi audio cap phat board chi con 31-35 KB — duong POST cu bat
// tay lai MOI luot nen chet tu luot thu hai. WS giu MOT phien suot doi may, va
// phien do mo ngay sau Wi-Fi, TRUOC httpd + audio (xem startSession()), nen
// bat tay chi tra mot lan luc RAM con thoang. Rot ma khong mo lai duoc vi het
// RAM lien thi audio_service tu khoi dong lai (RESTART_ON_TLS_STARVE).
//
// ⚠️ Dat 0 thi tieng noi, anh VA token di TRAN tren LAN. Chi cho demo trong phong.
#define API_USE_TLS 1

#if API_USE_TLS
#define API_HOST    "api.visioncare-host.uk"
#define API_PORT    443
#else
// IP LAN cua may chay `uvicorn app:app --host 0.0.0.0 --port 8000`.
#define API_HOST    "172.20.51.177"
#define API_PORT    8000
#endif
#define API_PATH    "/v2/session"

// Khai trong `hello.fw` (server chi ghi log). Tang khi doi giao thuc.
#define FW_VERSION  "2.1.0"

// 🔴 Bat tay TLS THANH CONG da do duoc: 1321, 3212, 3321, **5117** ms. Dat tran
// 5 giay la loai oan chinh cai duong dang chay duoc — da xay ra mot lan.
// setHandshakeTimeout() tinh bang GIAY, va chan RIENG lan bat tay (tang
// mbedtls), khong dinh toi timeout cua socket.
static const int NET_HANDSHAKE_S = 15;

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
// duoc thanh tieng lach tach deu deu. Luc dang phat loa thi duong len chi con
// ping va khung live (nho), ha xuong khong mat gi dang ke. Dat 0 de tat.
#define LOWER_WIFI_TX   1

// Am luong ra loa KHONG nam o day — no la nut van rieng cua tung khoi phat:
//   tieng server + clip cue : PLAY_GAIN_PCT trong src/audio/audio_player.cpp
//   tieng bip loi cuc bo    : CUE_GAIN      trong src/audio/audio_cues.cpp
// Hai cho do deu co ghi san cach chinh khi loa re.

// ---------------------------------------------------------------- Thu am
static const uint32_t SR      = 16000;   // tan so thu cua mic
static const int    MAX_SECS  = 10;      // tran thu am khi co PSRAM
static const int    DRAM_SECS = 3;       // tran thu am khi KHONG co PSRAM

#endif  // APP_CONFIG_H
