#ifndef HTTP_BODY_READER_H
#define HTTP_BODY_READER_H

// ============================================================================
// Doc than tra loi HTTP
// ============================================================================
// Than tra loi HTTP co hai kieu dong goi: bao truoc do dai (Content-Length)
// hoac cat thanh mieng (chunked). Bo doc nay lo ca hai vi server da doi cach
// tra it nhat mot lan roi.
// ============================================================================
#include <Arduino.h>
#include <WiFiClientSecure.h>

struct BodyReader {
  WiFiClientSecure *c;
  bool    chunked;
  long    left;   // Content-Length: con lai ca than. Chunked: con lai mieng nay.
  bool    eof;
  char    line[24];   // dem gom dong kich thuoc mieng, giu giua cac lan goi
  uint8_t lineLen;
  uint8_t back[8];    // byte da doc trom roi tra lai luong, xem bodySniff()
  uint8_t backLen;
};

void bodyInit(BodyReader &b, WiFiClientSecure *c, bool chunked, long clen);

// Doc TOI DA `max` byte, tra ve ngay voi nhung gi dang co.
//   > 0 : so byte doc duoc
//   = 0 : chua co gi luc nay, goi lai sau
//   < 0 : het than (binh thuong) hoac dut ket noi
int bodyRead(BodyReader &b, uint8_t *dst, size_t max);

// Doc dung `len` byte, cho toi khi du. Chi dung cho header WAV (vai chuc byte).
bool bodyReadExact(BodyReader &b, uint8_t *dst, size_t len);

// Nhin `len` byte dau cua than roi TRA LAI vao luong — nguoi doc sau van thay
// chung nhu chua ai dong toi. Toi da bang sizeof(BodyReader::back).
//
// Co de chon duong phat theo NOI DUNG chu khong theo HTTP header: header co
// the mo ta sai, bon byte dau cua file thi khong.
bool bodySniff(BodyReader &b, uint8_t *dst, size_t len);

#endif  // HTTP_BODY_READER_H
