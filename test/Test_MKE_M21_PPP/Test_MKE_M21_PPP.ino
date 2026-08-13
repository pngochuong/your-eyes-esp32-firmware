// ============================================================================
// Test_MKE_M21_PPP — ESP32 co that su CO INTERNET khong?
// ============================================================================
// Sketch DOC LAP, tach han khoi Test_MKE_M21.ino.
//
// VI SAO PHAI CO BAI TEST RIENG:
//   Test_MKE_M21 (bai T10) chi chung minh MODULE co internet: module tu mo
//   PDP context, tu lay IP, tu goi HTTP, roi doc ket qua ra UART duoi dang
//   chu. ESP32 luc do khong he co IP, khong mo socket nao — no chi doc van
//   ban. Internet nam TRONG module.
//
//   Sketch nay dung PPP: module chuyen sang che do truyen du lieu trong suot,
//   ESP32 chay ngan xep lwIP cua chinh no de len tren duong day do. Sau khi
//   PPP len, ESP32 co IP that, co DNS that, mo duoc socket that — luc do moi
//   goi la "ESP32 co internet".
//
// KHOI NAY LO: dung PPP, lay IP cho ESP32, phan giai DNS, mo socket TCP,
//              do thong luong that ma ESP32 nhan duoc.
// KHOI NAY KHONG LO: SMS, cuoc goi, am thanh — nhung thu do o Test_MKE_M21.
//
// 🔴 KHONG nap chung voi Test_MKE_M21: ca hai deu dung UART1. Thu vien PPP
// khoi tao UART1 bang driver esp_modem rieng, dung chung se tranh chap.
//
// Serial Monitor: 115200 baud.
// ============================================================================
#include <Arduino.h>
#include <PPP.h>

// Cung chan voi Test_MKE_M21 — khong phai doi day gi khi doi qua sketch nay.
#define M21_TX_PIN   2     // ESP32 phat -> chan R cua khoi cap nguon
#define M21_RX_PIN   14    // ESP32 nhan <- chan T cua khoi cap nguon

// A7680C khong co trong danh sach model cua esp_modem. SIM7600 la ho gan
// nhat (cung bo lenh A76XX cho phan PPP: ATD*99#, +CGDCONT, +CSQ...).
#define M21_MODEL    PPP_MODEM_SIM7600
#define M21_BAUD     9600

// APN do mang tu cap, doc duoc bang AT+CGDCONT? o bai T10:
//   +CGDCONT: 1,"IPV4V6","m-wap.mnc001.mcc452.gprs",...
// Phan goc la "m-wap". Doi mang khac thi sua dong nay.
#define M21_APN      "m-wap"

static void onNetEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_PPP_START:        Serial.println(F("[PPP] bat dau"));        break;
    case ARDUINO_EVENT_PPP_CONNECTED:    Serial.println(F("[PPP] da ket noi"));     break;
    case ARDUINO_EVENT_PPP_GOT_IP:       Serial.println(F("[PPP] DA CO IP"));       break;
    case ARDUINO_EVENT_PPP_LOST_IP:      Serial.println(F("[PPP] mat IP"));         break;
    case ARDUINO_EVENT_PPP_DISCONNECTED: Serial.println(F("[PPP] rot ket noi"));    break;
    case ARDUINO_EVENT_PPP_STOP:         Serial.println(F("[PPP] dung"));           break;
    default: break;
  }
}

// Bang chung quyet dinh: socket nay do lwIP CUA ESP32 mo, khong phai module.
// Chay duoc = ESP32 co internet that.
static void proveEsp32HasInternet(const char* host) {
  Serial.print(F("\n--- ESP32 tu mo socket toi ")); Serial.print(host); Serial.println(F(":80 ---"));

  // 1. DNS: ESP32 tu hoi, dung DNS server ma PPP cap.
  IPAddress ip;
  uint32_t t0 = millis();
  if (!Network.hostByName(host, ip)) {
    Serial.println(F("  DNS THAT BAI -> PPP len roi nhung chua co DNS."));
    return;
  }
  Serial.print(F("  DNS: ")); Serial.print(host); Serial.print(F(" -> "));
  Serial.print(ip); Serial.print(F("  (")); Serial.print(millis() - t0); Serial.println(F(" ms)"));

  // 2. TCP: socket cua ESP32, khong phai AT+CIPOPEN cua module.
  NetworkClient client;
  t0 = millis();
  if (!client.connect(ip, 80, 20000)) {
    Serial.println(F("  KHONG BAT TAY TCP DUOC."));
    return;
  }
  Serial.print(F("  TCP bat tay xong sau ")); Serial.print(millis() - t0); Serial.println(F(" ms"));

  client.printf("GET / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", host);

  // 3. Doc toan bo phan hoi, dem byte va do thoi gian -> thong luong THAT
  //    ma tang ung dung tren ESP32 nhan duoc.
  uint32_t nbytes = 0, tStart = 0, tLast = millis();
  String head;
  while (client.connected() || client.available()) {
    if (client.available()) {
      if (!tStart) tStart = millis();
      char c = client.read();
      if (head.length() < 40) head += c;
      nbytes++;
      tLast = millis();
    } else if (millis() - tLast > 8000) {
      break;
    } else {
      delay(2);
    }
  }
  client.stop();

  uint32_t dt = (tStart && tLast > tStart) ? (tLast - tStart) : 0;
  head.trim();
  Serial.print(F("  Dong dau: ")); Serial.println(head);
  Serial.print(F("  Nhan ")); Serial.print(nbytes);
  Serial.print(F(" byte trong ")); Serial.print(dt); Serial.println(F(" ms"));
  Serial.println(F("  (khong dung con so nay de tinh thong luong — xem ghi chu duoi)"));
}

// Do thong luong THAT.
//
// 🔴 Khong do bang mot trang nho. Voi vai tram byte, lwIP da nhan xong va
// dem san truoc khi vong doc chay — luc do phep do chi ra toc do VET BO DEM,
// cho ra con so cao hon ca tran vat ly cua baud (da in nham 12955 B/s trong
// khi 9600 baud toi da 960 B/s). Bat cu con so nao vuot tran baud deu la
// phep do hong, khong phai tin vui.
//
// Cach dung: keo mot luong lon hon bo dem nhieu lan, do trong suot qua trinh.
// Luc do duong truyen moi that su la nut that.
static bool pullFrom(const char* host, const char* path, uint32_t target,
                     uint32_t& nbytes, uint32_t& dt) {
  nbytes = 0; dt = 0;
  NetworkClient client;
  Serial.print(F("  thu ")); Serial.print(host); Serial.print(F(" ... "));
  if (!client.connect(host, 80, 20000)) { Serial.println(F("khong ket noi")); return false; }
  client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: esp32\r\n"
                "Accept: */*\r\nConnection: close\r\n\r\n", path, host);

  uint8_t tmp[512];
  uint32_t tStart = 0, tLast = millis();
  while (client.connected() || client.available()) {
    int n = client.available();
    if (n > 0) {
      if (!tStart) tStart = millis();          // moc = byte dau tien ve
      int r = client.read(tmp, n > (int)sizeof(tmp) ? sizeof(tmp) : n);
      if (r > 0) { nbytes += r; tLast = millis(); }
      if (nbytes >= target) break;
    } else if (millis() - tLast > 20000) {
      break;
    } else {
      delay(1);
    }
  }
  client.stop();
  dt = (tStart && tLast > tStart) ? (tLast - tStart) : 0;
  Serial.print(nbytes); Serial.print(F(" byte / ")); Serial.print(dt); Serial.println(F(" ms"));
  return (nbytes >= 4000 && dt > 1000);
}

static void measureThroughput() {
  const uint32_t TARGET = 20000;      // du lon de bo dem khong con y nghia
  Serial.print(F("\n--- Do thong luong: keo toi ")); Serial.print(TARGET);
  Serial.println(F(" byte ---"));

  // Nhieu nguon vi may chu do toc do hay chan/dong ket noi. Lay cai dau tien
  // tra du du lieu.
  const char* hosts[] = { "ipv4.download.thinkbroadband.com", "speedtest.tele2.net",
                          "proof.ovh.net", "www.google.com" };
  const char* paths[] = { "/5MB.zip", "/1MB.zip", "/files/1Mb.dat", "/" };

  uint32_t nbytes = 0, dt = 0;
  bool ok = false;
  for (uint8_t i = 0; i < 4 && !ok; i++) ok = pullFrom(hosts[i], paths[i], TARGET, nbytes, dt);

  if (!ok) {
    Serial.println(F("  Khong nguon nao tra du du lieu -> chua ket luan duoc"));
    Serial.println(F("  thong luong. Ket qua PPP o tren van co gia tri."));
    return;
  }
  uint32_t bps = nbytes * 1000UL / dt;
  Serial.print(F("  --> ~")); Serial.print(bps); Serial.println(F(" byte/giay"));
  Serial.print(F("  Tran ly thuyet UART o ")); Serial.print(M21_BAUD);
  Serial.print(F(" baud (8N1) = ")); Serial.print(M21_BAUD / 10); Serial.println(F(" byte/giay"));
  Serial.println(F("  Can: WAV 48kHz = 96000 · WAV 16kHz = 32000 · MP3 64kbps = 8000"));
  if (bps > (uint32_t)(M21_BAUD / 10)) {
    Serial.println(F("  !! Vuot tran baud -> phep do van chua dang tin."));
  }
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println(F("\n\n===== Test_MKE_M21_PPP — ESP32 co internet chua? ====="));
  Serial.print(F("UART1: TX=GPIO")); Serial.print(M21_TX_PIN);
  Serial.print(F("  RX=GPIO")); Serial.print(M21_RX_PIN);
  Serial.print(F("  baud ")); Serial.print(M21_BAUD);
  Serial.print(F("  APN ")); Serial.println(F(M21_APN));

  Network.onEvent(onNetEvent);

  PPP.setApn(M21_APN);
  // Khong goi setResetPin(): chan RST cua khoi SIM chua noi vao ESP32.
  PPP.setPins(M21_TX_PIN, M21_RX_PIN, -1, -1, ESP_MODEM_FLOW_CONTROL_NONE);

  Serial.println(F("\nDang bat modem qua esp_modem (co the mat 30-60 giay)..."));
  if (!PPP.begin(M21_MODEL, 1, M21_BAUD)) {
    Serial.println(F("PPP.begin() THAT BAI."));
    Serial.println(F("  - Con sketch Test_MKE_M21 dang chay? Hai cai dung chung UART1."));
    Serial.println(F("  - Module con ket o che do data tu lan truoc -> tat bat nguon module."));
    return;
  }

  Serial.print(F("Model:    ")); Serial.println(PPP.moduleName());
  Serial.print(F("IMEI:     ")); Serial.println(PPP.IMEI());
  Serial.print(F("Nha mang: ")); Serial.println(PPP.operatorName());
  Serial.print(F("RSSI:     ")); Serial.println(PPP.RSSI());

  Serial.print(F("Cho gan vao mang gio"));
  uint32_t t0 = millis();
  while (!PPP.attached() && millis() - t0 < 60000) { Serial.print('.'); delay(500); }
  Serial.println();
  if (!PPP.attached()) {
    Serial.println(F("KHONG GAN DUOC. Chay lai bai T3 cua Test_MKE_M21 truoc."));
    return;
  }

  // 🔴 PPP.begin() CHI dung modem day len va noi chuyen bang lenh AT — no
  // KHONG chuyen sang che do du lieu. Thieu buoc mode() duoi day thi modem
  // nam mai o che do lenh, PPP.connected() vinh vien false va nhin ra ngoai
  // giong het "mang khong len". Da mat mot vong debug vi cai nay.
  Serial.println(F("\nChuyen modem sang che do du lieu..."));
  bool up = false;
  if (PPP.mode(ESP_MODEM_MODE_DATA)) {
    up = PPP.waitStatusBits(ESP_NETIF_CONNECTED_BIT, 45000);
    Serial.print(F("  che do DATA: ")); Serial.println(up ? F("len") : F("khong len"));
  }
  if (!up) {
    // CMUX ghep lenh va du lieu tren cung duong; nang hon nhung mot so ban
    // firmware chi chiu kieu nay.
    Serial.println(F("  thu tiep che do CMUX..."));
    if (PPP.mode(ESP_MODEM_MODE_CMUX)) {
      up = PPP.waitStatusBits(ESP_NETIF_CONNECTED_BIT, 45000);
      Serial.print(F("  che do CMUX: ")); Serial.println(up ? F("len") : F("khong len"));
    }
  }

  if (!up || !PPP.connected()) {
    Serial.println(F("\n>>> ESP32 CHUA CO INTERNET — PPP khong len duoc."));
    Serial.println(F("    - APN sai? Doc lai bang AT+CGDCONT? o sketch Test_MKE_M21"));
    Serial.println(F("    - PDP con la IPV4V6? Bam 'p' ben sketch do de ep ve IPv4"));
    Serial.println(F("    - Baud 9600 co the qua cham cho PPP; can AT+IPR cao hon"));
    return;
  }

  Serial.println(F("\n===== ESP32 DA CO IP RIENG ====="));
  Serial.print(F("  IP      : ")); Serial.println(PPP.localIP());
  Serial.print(F("  Gateway : ")); Serial.println(PPP.gatewayIP());
  Serial.print(F("  DNS     : ")); Serial.println(PPP.dnsIP(0));

  proveEsp32HasInternet("example.com");
  measureThroughput();

  Serial.println(F("\n===== KET LUAN ====="));
  Serial.println(F("Chay den day nghia la ngan xep lwIP CUA ESP32 tu phan giai"));
  Serial.println(F("ten mien va tu mo socket TCP — internet khong con nam trong"));
  Serial.println(F("module nua. Tu day WiFiClient/NetworkClient dung duoc binh"));
  Serial.println(F("thuong, api_client khong phai viet lai theo lenh AT."));
}

void loop() {
  delay(1000);
}
