#ifndef WS_CLIENT_H
#define WS_CLIENT_H

// ============================================================================
// WebSocket client toi gian (RFC 6455) — MOT socket cho ca phien
// ============================================================================
// Khoi nay chi lo khung WebSocket: bat tay HTTP Upgrade, dong goi khung gui,
// cat khung nhan thanh tung mieng, tu tra loi ping va close. No KHONG biet
// JSON, KHONG biet giao thuc cua server (hello, turn_start...) — do la viec
// cua device_session.
//
// Vi sao tu viet: core arduino-esp32 3.3.10 KHONG kem esp_websocket_client, va
// thu vien ngoai thi phai cai rieng tren moi may build. Phan can dung chi la
// vai tram dong tren chinh NetworkClient/WiFiClientSecure ma project da dung
// (va da tra gia de biet cach dat timeout cho dung).
//
// 🔴 KHONG thread-safe. Chi MOT task (task "wssess" cua device_session) duoc
// goi vao day — ca doc lan ghi. mbedtls khong chiu hai task cung dung mot
// phien TLS, va loi do khong bao gi, chi hong du lieu.
// ============================================================================
#include <Arduino.h>

// Mo TCP (+TLS khi API_USE_TLS) roi bat tay WebSocket toi `path`.
// Tra ve true khi server tra 101 va Sec-WebSocket-Accept khop.
bool wsOpen(const char *host, uint16_t port, const char *path);

// Gui khung close (neu con mo) roi dong socket.
void wsClose(uint16_t code);

// Dong socket NGAY, khong gui gi. Dung khi duong da chet (mat Wi-Fi, khong
// nhan duoc gi qua lau): ghi khung close len mot duong chet la ngoi cho het
// tran ghi 20 s.
void wsAbort();

bool wsIsOpen();

// Gui mot khung text tron.
bool wsSendText(const char *s, size_t n);

// Gui mot khung binary: byte dau la `type` (0x01 anh, 0x03 tieng, 0x04 live),
// sau do la `p`. Chan cho toi khi ghi het hoac duong len dung qua lau.
bool wsSendTyped(uint8_t type, const uint8_t *p, size_t n);

// ---- nhan
enum WsKind : uint8_t {
  WS_NOTHING,   // chua co gi
  WS_DATA,      // mot mieng cua mot thong diep text/binary
  WS_CLOSED,    // socket da dong — closeCode cho biet vi sao
};

struct WsPiece {
  WsKind         kind;
  bool           text;       // thong diep text (JSON) hay binary
  bool           first;      // mieng dau cua thong diep
  bool           last;       // mieng cuoi cua thong diep
  const uint8_t *data;       // hop le toi lan goi wsPoll() ke tiep
  size_t         len;
  uint16_t       closeCode;  // WS_CLOSED: ma server gui, 1006 = dut khong bao
};

// Doc nhung gi dang co, KHONG chan. Thong diep lon (khoi 0x02 vai KB) den
// thanh nhieu mieng — nguoi goi tu ghep hoac tu xa thang.
WsPiece wsPoll();

// millis() luc nhan duoc byte cuoi cung. Dung de phat hien socket "song gia":
// connected() van true nhung khong con gi di qua.
unsigned long wsLastRxMs();

#endif  // WS_CLIENT_H
