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
// Server xu ly. Doi 3 dong nay neu endpoint thay doi.
#define API_HOST    "api.visioncare-host.uk"
#define API_PATH    "/process"
#define API_PORT    443

// Do thuc te: server xu ly rat lau moi tra byte dau tien. Moi timeout duoi day
// phai rong hon con so do, neu khong se ngat ngay truoc khi co ket qua.
static const int NET_TIMEOUT_MS = 60000;

// ---- Ba con so DUNG DAU: chung quyet dinh bao lau thi biet la mang hong.
//
// 🔴 Tach han khoi NET_TIMEOUT_MS o tren. Hai nhom nay do hai thu khac nhau:
// nhom nay do "co voi toi server duoc khong" — cau tra loi phai den NHANH de
// con bao cho nguoi dung bam lai; NET_TIMEOUT_MS do "server nghi xong chua" —
// cai do buoc phai rong vi server that su cham.
//
// Do that tren hotspot Redmi (Viettel, 2026-08-18): DNS 3.7-14 s, bat tay TCP
// toi Cloudflare 7.4 s. Nen 9 giay la nguong vua du cho mang cham that, va du
// gat de mot lan hong khong bat nguoi dung dung do nua phut.
// Gat het muc con AN TOAN — nhung "an toan" o day la con so do duoc, khong
// phai con so mong muon.
//
// 🔴 Bat tay TLS THANH CONG da do duoc: 1321, 3212, 3321, **5117** ms. Nen dat
// tran 5 giay la loai oan chinh cai duong dang chay duoc — da xay ra dung nhu
// vay mot lan. 8 giay la muc gat nhat con bao duoc lan 5117 ms, va van du som
// de keu tieng bip cho nguoi dung bam lai.
//
// 🔴 NET_HANDSHAKE_S chan RIENG lan bat tay (tang mbedtls). KHONG duoc dung
// con so gat nay lam timeout cua socket: start_ssl_client() nap timeout cua
// socket ngay luc connect va no tro thanh tran cho MOI lan ghi/doc sau do. Da
// tra gia: dat 5000 thi lan ghi anh chet o dung "5004 ms" du duong van tot.
// Socket luon dung NET_TIMEOUT_MS.
static const int NET_DNS_MS      = 3000;   // phan giai ten mien, chi lam 1 lan luc khoi dong
static const int NET_CONNECT_MS  = 9000;   // ngan sach CHO task mang xong mot buoc
static const int NET_HANDSHAKE_S = 15;      // 🔴 setHandshakeTimeout() tinh bang GIAY

// ---- Giu phien TLS song de dung lai cho lan bam sau
//
// 🔴 Bat tay TLS do duoc 3212 ms tren FTTH — chang dat nhat cua ca luong sau
// khi DNS da duoc nho san. Nhung mot phien TLS dung duoc cho NHIEU request
// lien tiep neu ca hai ben khong dong ket noi, nen tu lan bam thu hai tro di
// chang do co the bang 0.
//
// 8 phut: Cloudflare giu ket noi keep-alive rong khoang 900 giay, nen 480 giay
// con du bien de khong bao gio ngoi doi mot socket da bi ben kia dong.
//
// 🔴 Dieu kien BAT BUOC de dung lai: than cua lan truoc phai duoc doc HET.
// Con byte nao chua doc thi no se bi doc thanh dong dau tien cua tra loi lan
// sau — hai luong lech nhau vinh vien, va trieu chung la "tu nhien tra loi
// sai bet tu lan bam thu hai".
static const unsigned long TLS_KEEPALIVE_MS = 8UL * 60UL * 1000UL;

// Bao lau thi coi nhu dia chi da phan giai la cu va di hoi lai. Cloudflare doi
// IP kha thuong xuyen, nhung ta con co duong tu phan giai lai khi ket noi
// hong, nen con so nay chi la luoi do thu hai.
static const unsigned long DNS_CACHE_MS = 10UL * 60UL * 1000UL;

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
// Do that: server nghi ~40 giay. 60 giay chua 20 giay du phong, va cat truoc
// moc 100 giay ma Cloudflare tu ngat (loi 524) — nho vay khi hong ta biet la
// server cham that chu khong phai Cloudflare vua dong tay giua chung.
static const int FIRST_AUDIO_MS = 60000;

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

// Am luong ra loa KHONG nam o day — no la nut van rieng cua tung khoi phat:
//   cau tra loi tu server : PLAY_GAIN_PCT trong src/audio/audio_player.cpp
//   hai tieng bao tai cho : CUE_GAIN      trong src/audio/audio_cues.cpp
// Hai cho do deu co ghi san cach chinh khi loa re.

// ---------------------------------------------------------------- Thu am
static const uint32_t SR      = 16000;   // tan so thu cua mic
static const int    MAX_SECS  = 10;      // tran thu am khi co PSRAM
static const int    DRAM_SECS = 3;       // tran thu am khi KHONG co PSRAM
static const int    MIN_MS    = 300;     // nhan ngan hon coi nhu bam nham

#endif  // APP_CONFIG_H
