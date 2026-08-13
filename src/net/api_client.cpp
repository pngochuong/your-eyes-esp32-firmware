#include "api_client.h"

#include <WiFi.h>
#include "../../app_config.h"

// Ghi het len socket TLS. client.write() co the ghi thieu khi buffer day,
// nen phai lap — day la loi im lang hay gap nhat khi POST file lon.
static bool writeAll(WiFiClientSecure &c, const uint8_t *p, size_t len) {
  size_t sent = 0;
  unsigned long t0 = millis();
  while (sent < len) {
    size_t n = c.write(p + sent, len - sent);
    if (n == 0) {
      if (!c.connected() || millis() - t0 > 30000) return false;
      vTaskDelay(5 / portTICK_PERIOD_MS);
      continue;
    }
    sent += n;
    t0 = millis();
  }
  return true;
}

static bool writeAll(WiFiClientSecure &c, const String &s) {
  return writeAll(c, (const uint8_t *)s.c_str(), s.length());
}

void apiClose(ApiReply &reply) {
  reply.client.stop();
}

bool apiSendCapture(const uint8_t *jpg, size_t jpgLen,
                    const int16_t *pcm, size_t pcmBytes,
                    ApiReply &reply) {
  const char *BOUND = "----ESP32VisionCareBoundary7d91";

  reply.isMp3  = false;
  reply.fmt    = AudioFmt{ false, 0, 0, 0 };
  reply.code   = 0;

  // --- Dung san cac manh header de tinh duoc Content-Length chinh xac.
  // Multipart bat buoc phai bao truoc do dai; khong duoc vua gui vua dem.
  uint8_t wh[44];
  wavHeader(wh, pcmBytes, SR);

  String pImg = String("--") + BOUND + "\r\n"
                "Content-Disposition: form-data; name=\"image\"; filename=\"capture.jpg\"\r\n"
                "Content-Type: image/jpeg\r\n\r\n";
  String pAud = String("\r\n--") + BOUND + "\r\n"
                "Content-Disposition: form-data; name=\"audio\"; filename=\"record.wav\"\r\n"
                "Content-Type: audio/wav\r\n\r\n";
  String pEnd = String("\r\n--") + BOUND + "--\r\n";

  size_t total = pImg.length() + jpgLen
               + pAud.length() + sizeof(wh) + pcmBytes
               + pEnd.length();

  WiFiClientSecure &client = reply.client;
  // ⚠️ Bo qua kiem tra chung chi. Xem ghi chu bao mat trong huong dan.
  client.setInsecure();
  // 🔴 Ca hai deu tinh bang MILLI giay o core 3.x, va deu can:
  //   setConnectionTimeout -> timeout muc socket (connect + read + write)
  //   setTimeout           -> timeout cua Stream, tuc readStringUntil() ben duoi
  // Thieu cai thu hai thi doc dong "HTTP/1.1 200 OK" se bo cuoc sau 1 giay,
  // trong khi server can toi ~40 giay moi tra loi.
  client.setTimeout(NET_TIMEOUT_MS);

  Serial.printf("Ket noi %s:%d ...\n", API_HOST, API_PORT);
  Serial.printf("  WiFi \"%s\", IP %s, RSSI %d dBm, DNS %s\n",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                (int)WiFi.RSSI(), WiFi.dnsIP().toString().c_str());

  // 🔴 Tach DNS ra khoi TCP/TLS. client.connect(ten_mien, ...) lam ca hai viec
  // trong mot lan goi, that bai chi tra ve false — khong biet hong o dau, ma
  // hai cai sua khac han nhau:
  //   DNS hong  -> mang co captive portal chua dang nhap, hoac chan cong 53
  //   TCP hong  -> ra duoc DNS nhung bi chan cong 443, hoac khong co internet
  {
    IPAddress ip;
    unsigned long tDns = millis();
    if (!WiFi.hostByName(API_HOST, ip)) {
      Serial.printf("  DNS THAT BAI sau %lu ms — khong phan giai duoc %s\n",
                    millis() - tDns, API_HOST);
      Serial.println("  Mang nay khong cho tra DNS. Thuong gap voi WiFi cong cong");
      Serial.println("  hoac WiFi cong ty co trang dang nhap chua bam qua.");
      return false;
    }
    Serial.printf("  DNS OK (%lu ms) -> %s\n", millis() - tDns, ip.toString().c_str());
  }

  // Van ket noi bang TEN MIEN chu khong bang IP vua phan giai: Cloudflare
  // dung SNI de biet dang hoi site nao, ma connect(IPAddress,...) khong gui
  // SNI — se bi tu choi bat tay TLS.
  unsigned long tCon = millis();
  if (!client.connect(API_HOST, API_PORT, 15000)) {
    Serial.printf("  TCP/TLS THAT BAI sau %lu ms (DNS thi OK)\n", millis() - tCon);
    Serial.printf("  Heap con %u byte — bat tay TLS can khoang 45 KB.\n",
                  (unsigned)ESP.getFreeHeap());
    Serial.println("  Nghi ngo: mang chan cong 443, hoac khong ra duoc internet.");
    return false;
  }
  // connect(...,timeout) vua ghi de _timeout thanh 15 s. Phai noi lai NGAY
  // sau khi ket noi, neu khong doc du lieu se bo cuoc truoc khi server tra loi.
  client.setConnectionTimeout(NET_TIMEOUT_MS);

  // 🔴 TUYET DOI khong them "Expect: 100-continue". Da thu that: server nay
  // khong tra loi 100, ben gui se treo cho toi khi timeout. curl tu them
  // header do cho body > 1 KB nen luc test bang curl phai dung -H "Expect:".
  String head = String("POST ") + API_PATH + " HTTP/1.1\r\n"
                "Host: " API_HOST "\r\n"
                "User-Agent: ESP32S3-VisionCare/1.0\r\n"
                "Accept: audio/wav\r\n"
                "Content-Type: multipart/form-data; boundary=" + BOUND + "\r\n"
                "Content-Length: " + String(total) + "\r\n"
                "Connection: close\r\n\r\n";

  Serial.printf("Gui %u byte (anh %u + am %u)...\n",
                (unsigned)total, (unsigned)jpgLen, (unsigned)pcmBytes);
  unsigned long t0 = millis();
  reply.tStart = t0;

  // Do rieng tung chang. Khong tach ra thi chi biet "gui mat 7 giay" ma khong
  // biet nen toi uu anh hay toi uu am — hai huong sua hoan toan khac nhau.
  bool ok = writeAll(client, head) && writeAll(client, pImg);
  unsigned long tHead = millis();

  ok = ok && writeAll(client, jpg, jpgLen);
  unsigned long tImg = millis();

  ok = ok && writeAll(client, pAud)
          && writeAll(client, wh, sizeof(wh))
          && writeAll(client, (const uint8_t *)pcm, pcmBytes)
          && writeAll(client, pEnd);
  unsigned long tAud = millis();

  if (!ok) {
    Serial.println("Dut ket noi giua chung khi gui");
    client.stop();
    return false;
  }

  {
    unsigned long dImg = tImg - tHead, dAud = tAud - tImg, dAll = tAud - t0;
    Serial.printf("Gui xong %lu ms: anh %u KB/%lu ms (%.0f KB/s), am %u KB/%lu ms (%.0f KB/s)\n",
                  dAll,
                  (unsigned)(jpgLen / 1024), dImg,
                  dImg ? jpgLen / 1.024 / dImg : 0.0,
                  (unsigned)(pcmBytes / 1024), dAud,
                  dAud ? pcmBytes / 1.024 / dAud : 0.0);
    if (dAll > 0)
      Serial.printf("  tong %.0f KB/s. Chang lau hon: %s\n",
                    total / 1.024 / dAll, dImg > dAud ? "ANH" : "AM");
  }

  // --- Doc dong trang thai + header tra ve
  String status = client.readStringUntil('\n');
  status.trim();
  Serial.printf("[%lu ms] Server: %s\n", millis() - t0, status.c_str());
  if (status.length() == 0) {
    Serial.println("Server khong tra dong trang thai — dut ket noi hoac timeout");
    client.stop();
    return false;
  }
  int code = 0;
  { int sp = status.indexOf(' '); if (sp > 0) code = status.substring(sp + 1, sp + 4).toInt(); }
  reply.code = code;

  // In HET header. Khong doan mo: doi khi loi nam ngay o day (content-type
  // la application/json, hoac co Content-Length nghia la server van dang gom).
  long  clen    = -1;
  bool  chunked = false;
  int   nHdr    = 0;
  AudioFmt fmt  = { false, 0, 0, 0 };
  bool     isMp3 = false;
  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break;                  // het header
    Serial.printf("  < %s\n", line.c_str());
    nHdr++;
    String low = line; low.toLowerCase();
    if (low.startsWith("content-length:"))     clen = line.substring(15).toInt();
    if (low.startsWith("transfer-encoding:") && low.indexOf("chunked") >= 0) chunked = true;

    // Nhan MP3 qua ca hai duong: header rieng cua server, hoac Content-Type
    // chuan. Kiem ca hai de khong phu thuoc mot ben nho khong doi.
    if ((low.startsWith("x-audio-format:") && low.indexOf("mp3") >= 0) ||
        (low.startsWith("content-type:") &&
         (low.indexOf("audio/mpeg") >= 0 || low.indexOf("audio/mp3") >= 0))) {
      isMp3 = true;
    } else if (low.startsWith("x-audio-format:")) {
      parseAudioFormat(line.substring(15), fmt);
    }
  }
  Serial.printf("[%lu ms] Het header (%d dong)\n", millis() - t0, nHdr);

  if (code != 200) {
    Serial.printf("Server tra ma %d — khong phat\n", code);
    client.stop();
    return false;
  }
  if (!chunked && clen <= 0) {
    Serial.println("Tra loi khong co Content-Length va khong chunked");
    client.stop();
    return false;
  }

  reply.isMp3 = isMp3;
  reply.fmt   = fmt;
  bodyInit(reply.body, &client, chunked, clen);
  return true;
}
