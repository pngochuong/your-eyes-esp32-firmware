#include <esp_heap_caps.h>
#include "wifi_manager.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <lwip/dns.h>
#include <lwip/netdb.h>
#include "../../app_config.h"

// =====================================================
// Ly do bi ngat — do driver bao, khong phai doan
// =====================================================
// WiFi.status() chi tra WL_DISCONNECT cho MOI kieu that bai: sai mat khau, sai
// ten, sai bang tan deu ra cung mot ma. Ma that nam trong su kien
// STA_DISCONNECTED cua driver, phai bat rieng moi thay.
static volatile int g_lastReason = 0;

// Dia chi da bat tay TLS tron ven duoc. Xem ghi chu o wifi_manager.h.
static IPAddress g_provenIp;
static bool      g_haveProven = false;

bool wifiProvenServerIp(IPAddress &out) {
  if (!g_haveProven) return false;
  out = g_provenIp;
  return true;
}

static void wifiOnDisconnect(WiFiEvent_t /*event*/, WiFiEventInfo_t info) {
  g_lastReason = info.wifi_sta_disconnected.reason;
}

static const char* wifiReasonText(int reason) {
  switch (reason) {
    case 201: return "NO_AP_FOUND — khong thay ten mang nay tren song 2.4 GHz";
    case 15:  return "4WAY_HANDSHAKE_TIMEOUT — gan nhu chac chan SAI MAT KHAU";
    case 202: return "AUTH_FAIL — AP tu choi xac thuc (sai pass hoac WPA3-only)";
    case 203: return "ASSOC_FAIL — AP tu choi ket nap (het cho / loc MAC)";
    case 204: return "HANDSHAKE_TIMEOUT — bat tay do dang, song yeu hoac WPA3";
    case 205: return "CONNECTION_FAIL — noi hong khong ro nguyen nhan";
    case 2:   return "AUTH_EXPIRE — het han xac thuc, thuong do song yeu";
    case 4:   return "ASSOC_EXPIRE — AP da tha thiet bi ra";
    case 8:   return "ASSOC_LEAVE — AP chu dong duoi";
    case 200: return "BEACON_TIMEOUT — mat song giua chung, AP tat hoac qua xa";
    case 36:  return "STA_LEAVING — chinh ESP tu bo cuoc, AP chua he tu choi";
    case 0:   return "chua he bi ngat lan nao (khong nhan duoc gi tu AP)";
    default:  return "xem wifi_err_reason_t trong esp_wifi_types.h";
  }
}

// =====================================================
// Thu voi tay toi server bang DUNG mot ho dia chi
// =====================================================
// 🔴 WiFi.hostByName() KHONG thay the duoc ham nay. Core 3.3.x co doan
// "workaround" trong NetworkManager::hostByName(): he interface co dia chi
// IPv6 toan cuc la no hoi AAAA truoc va dung luon, KHONG BAO GIO thu IPv4.
// Nen khi TLS hong, ket qua cua hostByName() khong cho biet IPv4 co thong
// hay khong — no chua tung thu IPv4. Phai ep ai_family bang tay.
//
// Bat tay TLS khong can o day: TCP bat tay xong la du biet goi tin co ra toi
// Cloudflare hay khong, ma lai nhanh hon va khong ton ~45 KB heap.
struct ProbeResult {
  bool          dnsOk = false;
  bool          tcpOk = false;
  IPAddress     ip;
  unsigned long dnsMs = 0;
  unsigned long tcpMs = 0;
};

static ProbeResult probeFamily(int family, unsigned long timeoutMs) {
  ProbeResult r;
  struct addrinfo  hints;
  struct addrinfo* res = nullptr;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = family;
  hints.ai_socktype = SOCK_STREAM;

  unsigned long t = millis();
  if (lwip_getaddrinfo(API_HOST, "0", &hints, &res) != 0 || res == nullptr) {
    r.dnsMs = millis() - t;
    return r;
  }
  r.dnsMs = millis() - t;
  r.dnsOk = true;

  if (res->ai_family == AF_INET6) {
    r.ip = IPAddress(IPv6, ((struct sockaddr_in6*)res->ai_addr)->sin6_addr.s6_addr);
  } else {
    r.ip = IPAddress(((struct sockaddr_in*)res->ai_addr)->sin_addr.s_addr);
  }
  lwip_freeaddrinfo(res);

  NetworkClient c;
  t = millis();
  r.tcpOk = c.connect(r.ip, API_PORT, timeoutMs);
  r.tcpMs = millis() - t;
  if (r.tcpOk) c.stop();

  return r;
}

// 🔴 Bat tay TCP KHONG du de ket luan duong nay dung duoc. Do that tren
// hotspot 4G: TCP toi 443 "OK" trong 1340 ms, nhung TLS tren dung socket do
// chet sau 124 giay. Nha mang co middlebox/CGNAT tu tra SYN-ACK thay server,
// nen bat tay TCP xong ma goi tin khong he ra toi Cloudflare. Chi bat tay TLS
// tron ven moi la bang chung.
//
// 🔴 Nhan vao IP DA phan giai san chu khong nhan ten mien. Hai ly do:
//   - Truyen ten mien vao thi ham tu phan giai, ma phan giai lai di qua dung
//     cai hostByName() da noi o tren — no chon ho dia chi thay ta, nen phep
//     thu "ep IPv4" khong con ep duoc gi.
//   - Do that tren hotspot Redmi: mot luot DNS ton 4-14 giay. Gop DNS vao
//     trong ngan sach cua phep thu TLS thi ngan sach het truoc khi bat tay
//     kip bat dau, va ket qua doc ra thanh "duong nay hong" trong khi that ra
//     duong do tot.
//
// Van phai dua API_HOST vao lam SNI: Cloudflare dua vao SNI de biet dang hoi
// site nao, connect bang IP tran la bi tu choi bat tay.
static bool tlsReachableOn(IPAddress ip, unsigned long timeoutSec) {
  WiFiClientSecure c;
  c.setInsecure();
  c.setConnectionTimeout(timeoutSec * 1000);
  // 🔴 connect(..., timeout) chi chan o muc socket. Bat tay TLS co dong ho
  // RIENG, mac dinh rat dai — thieu dong duoi thi da do duoc 124304 ms cho
  // mot lan "timeout 15 giay".
  c.setHandshakeTimeout(timeoutSec);

  bool ok = c.connect(ip, API_PORT, API_HOST, nullptr, nullptr, nullptr);

  // 🔴 Hong thi phai in RA ma loi, dung de lai mot chu "hong".
  //
  // mbedtls co in ly do bang log_e(), nhung ban dung nay khong dat DebugLevel
  // nen moi dong do bi bien dich BO. Khong hoi lastError() thi "TLS hong
  // (0 ms)" va "TLS hong (8003 ms)" nhin nhu cung mot loi, trong khi 0 ms la
  // hong TRUOC khi goi tin roi board (het bo nho, khong mo noi socket) con
  // 8003 ms la het gio cho ben kia tra loi — hai huong sua nguoc nhau.
  if (!ok) {
    char msg[128] = {0};
    int  e = c.lastError(msg, sizeof(msg));
    Serial.printf("  mbedtls %d (-0x%04X) %s | heap %u B, khoi lien lon nhat %u B\n",
                  e, (unsigned)(-e), msg,
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)heap_caps_get_largest_free_block(
                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }

  c.stop();
  return ok;
}

// Phan giai API_HOST bang DUNG mot ho dia chi. Tra ve false neu mang khong co
// ban ghi loai do — day chinh la ca cua hotspot Redmi voi ban ghi A.
static bool resolveFamily(int family, IPAddress &out, unsigned long &ms) {
  struct addrinfo  hints;
  struct addrinfo *res = nullptr;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = family;
  hints.ai_socktype = SOCK_STREAM;

  unsigned long t = millis();
  int rc = lwip_getaddrinfo(API_HOST, "0", &hints, &res);
  ms = millis() - t;
  if (rc != 0 || res == nullptr) return false;

  if (res->ai_family == AF_INET6) {
    out = IPAddress(IPv6, ((struct sockaddr_in6 *)res->ai_addr)->sin6_addr.s6_addr);
  } else {
    out = IPAddress(((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr);
  }
  lwip_freeaddrinfo(res);
  return out != IPAddress((uint32_t)0);
}

// =====================================================
// Nhet them may chu DNS IPv4 vao cac o con trong
// =====================================================
// 🔴 Do that tren hotspot Redmi (Viettel, 2026-08-18): no chi cap DUY NHAT
// mot may chu DNS, va do la dia chi IPv6 (2402:9d80:384:ae2c::79). Hoi ban
// ghi A qua no thi chet han sau 7 giay. Hau qua khong nam o DNS: firmware doc
// "IPv4 hong" roi bat IPv6 du phong — trong khi that ra IPv4 chua bao gio
// duoc thu, chi la khong co ai phan giai ten mien ho no.
//
// 🔴 Dat resolver cong cong len o 0, DAY resolver cua DHCP xuong o 2 — khong
// phai chi nhet vao cho trong nhu truoc.
//
// Ban truoc chi lap o trong, va do that tren hotspot iPhone "Huong"
// (2026-08-19) danh sach ra dung nhu mong doi:
//     [0]=172.20.10.1  [1]=8.8.8.8  [2]=1.1.1.1
// nhung hoi ban ghi A van chet o 18000 roi 15000 ms. lwip hoi o 0 truoc va
// thu lai vai luot moi chuyen sang o ke — nen mot resolver do chung o o 0
// nuot tron ca ngan sach thoi gian, hai o sau gan nhu khong bao gio toi luot.
// Cung ten mien do, hoi tu PC tren DUNG hotspot ay: 17 ms.
//
// Doi lai gi: mat kha nang phan giai ten trong mang LAN. Board nay chi phan
// giai DUY NHAT mot ten mien cong cong (API_HOST), nen khong mat gi that.
static void dnsSetPublicFirst() {
  const ip_addr_t *dhcp = dns_getserver(0);
  ip_addr_t keep = (dhcp && !ip_addr_isany(dhcp)) ? *dhcp : *IP_ADDR_ANY;

  ip_addr_t g, cf;
  IP_ADDR4(&g,  8, 8, 8, 8);
  IP_ADDR4(&cf, 1, 1, 1, 1);
  dns_setserver(0, &g);
  dns_setserver(1, &cf);
  if (!ip_addr_isany(&keep) && DNS_MAX_SERVERS > 2) dns_setserver(2, &keep);

  Serial.print("May chu DNS dang dung:");
  for (int i = 0; i < DNS_MAX_SERVERS; i++) {
    const ip_addr_t *cur = dns_getserver(i);
    if (cur && !ip_addr_isany(cur)) Serial.printf(" [%d]=%s", i, ipaddr_ntoa(cur));
  }
  Serial.println();
}

static void probeFamilyPrint(const char* label, int family) {
  ProbeResult r = probeFamily(family, 8000);

  if (!r.dnsOk) {
    Serial.printf("%-5s DNS HONG (%lu ms) — mang khong tra ban ghi nay\n",
                  label, r.dnsMs);
    return;
  }
  Serial.printf("%-5s DNS -> %s (%lu ms)\n",
                label, r.ip.toString().c_str(), r.dnsMs);
  Serial.printf("%-5s TCP 443 %s (%lu ms)%s\n", label,
                r.tcpOk ? "OK" : "HONG", r.tcpMs,
                r.tcpOk ? " — chi la bat tay TCP, chua chac TLS qua duoc"
                        : " — goi tin khong ra toi noi");
}

// =====================================================
// Quet song — cho biet AP co thuc su o day khong
// =====================================================
// Doi chieu tung ky tu ten mang: khoang trang thua va chu hoa/thuong la hai
// loi khong nhin ra duoc bang mat khi so tren man hinh dien thoai.
void wifiScanReport() {
  // 🔴 Phai bo dot ket noi dang do TRUOC khi quet. Quet trong luc STA con bam
  // kenh thi driver tra ve so AM (-1 dang chay, -2 that bai) — gop chung voi
  // 0 se bao "khong thay mang nao" trong khi song van day. Da doc nham dung
  // kieu do mot lan roi.
  WiFi.disconnect(false, false);
  delay(300);

  Serial.println("Dang quet song 2.4 GHz...");

  // show_hidden = true: AP giau ten van hien ra (ten rong), de con phan biet
  // "khong co AP" voi "co AP nhung giau ten".
  int n = WiFi.scanNetworks(false, true);

  // Quet hong thi thu lai mot lan — lan dau hay vap khi radio vua bi ngat.
  if (n < 0) {
    Serial.printf("Quet loi (ma %d), thu lai...\n", n);
    WiFi.scanDelete();
    delay(700);
    n = WiFi.scanNetworks(false, true);
  }

  if (n < 0) {
    Serial.printf("QUET THAT BAI (ma %d) — day la loi driver, KHONG phai\n", n);
    Serial.println("bang chung la khong co song. -1 = dang quet, -2 = that bai.");
    WiFi.scanDelete();
    return;
  }

  if (n == 0) {
    Serial.println("Quet chay xong nhung KHONG thay AP nao — ke ca mang hang xom.");
    Serial.println("O cho co nguoi o thi dieu nay bat thuong: nghi anten chua");
    Serial.println("gan / dut, hoac nguon 5V yeu lam radio khong phat duoc.");
    WiFi.scanDelete();
    return;
  }

  bool found = false;

  Serial.printf("Thay %d mang (chi 2.4 GHz moi hien ra day):\n", n);
  for (int i = 0; i < n; i++) {
    const String ssid = WiFi.SSID(i);
    const bool match  = ssid.equals(WIFI_SSID);
    found |= match;

    Serial.printf("  %c [%s] %d dBm  kenh %d  %s\n",
                  match ? '>' : ' ',
                  ssid.length() ? ssid.c_str() : "<giau ten>",
                  WiFi.RSSI(i),
                  WiFi.channel(i),
                  WiFi.encryptionType(i) == WIFI_AUTH_OPEN     ? "mo"
                  : WiFi.encryptionType(i) == WIFI_AUTH_WPA3_PSK ? "WPA3 (ESP32-S3 co the hong)"
                  : WiFi.encryptionType(i) == WIFI_AUTH_WEP    ? "WEP"
                                                               : "WPA/WPA2");
  }

  Serial.println();
  if (found) {
    Serial.printf("Ten \"%s\" CO tren song => loi nam o mat khau hoac o AP,\n",
                  WIFI_SSID);
    Serial.println("khong phai o ten mang hay bang tan.");
  } else {
    Serial.printf("Ten \"%s\" KHONG co trong danh sach tren. Nguyen nhan:\n",
                  WIFI_SSID);
    Serial.println("  - hotspot dang o 5 GHz (Redmi mac dinh hay chon 5 GHz)");
    Serial.println("  - ten sai mot ky tu / thua khoang trang / khac chu hoa");
    Serial.println("  - AP an SSID, hoac vua tu tat vi khong ai ket noi");
  }

  WiFi.scanDelete();
}

// Goi begin() roi cho toi khi lien ket xong. Tach rieng vi phai chay HAI lan:
// lan dau thu IPv4, lan sau noi lai de xin dia chi IPv6 (xem ben duoi).
static bool beginAndWait(unsigned long timeoutMs) {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Dang ket noi WiFi");

  const unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - startTime >= timeoutMs) {
      Serial.println();
      return false;
    }
  }
  Serial.println();
  return true;
}

// =====================================================
// Ket noi Wi-Fi. Tra ve true neu vao duoc mang.
// =====================================================
bool wifiConnect(unsigned long timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  g_lastReason = 0;
  WiFi.onEvent(wifiOnDisconnect, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  // 🔴 KHONG bat IPv6 o day. Bat len la mat quyen chon duong: hostByName()
  // cua core thay co IPv6 toan cuc la hoi AAAA truoc va dung luon, khong bao
  // gio thu IPv4 nua. Ma enableIPv6(false) chi xoa co WANT_IP6, KHONG thu hoi
  // dia chi da cap — lo bat roi thi phai noi lai mang moi go duoc.
  //
  // Da tra gia cho ca hai chieu:
  //   hotspot dien thoai — IPv4 khong ra duoc internet, BUOC phai co IPv6
  //   router Ngoc Phat   — router quang ba IPv6 nhung nha mang khong dinh
  //                        tuyen; bat IPv6 len la moi ket noi chet o 15 s
  //                        du IPv4 van tot
  // Nen khong the chon cung mot ben. Duoi kia thu IPv4 truoc, hong moi bat
  // IPv6 — IPv4 chay duoc o hau het mang, IPv6 chi la loi thoat.

  // In trong dau nhon de thay khoang trang thua o dau/cuoi — loi nay nhin
  // bang mat khong ra, ma driver thi coi la mot ten mang khac han.
  Serial.printf("SSID    : [%s] (%u ky tu)\n", WIFI_SSID, (unsigned)strlen(WIFI_SSID));
  Serial.printf("Mat khau: %u ky tu\n", (unsigned)strlen(WIFI_PASSWORD));

  if (!beginAndWait(timeoutMs)) {
    Serial.println("Khong ket noi duoc WiFi");
    Serial.printf("Ly do driver bao: %d — %s\n",
                  g_lastReason, wifiReasonText(g_lastReason));
    Serial.println();
    wifiScanReport();
    return false;
  }

  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("WiFi RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");

  // --- Chon ho dia chi: IPv4 truoc, IPv6 chi khi IPv4 khong ra duoc
  // Chua bat IPv6 nen giao dien khong co dia chi v6 toan cuc, hostByName() vi
  // the buoc phai tra ban ghi A => phep thu nay do dung duong IPv4.
  // Mat them vai giay luc khoi dong, doi lai khong phai sua tay khi doi mang.
  // 🔴 Thu HAI lan truoc khi bo sang IPv6. Truot oan mot lan la tra gia dat:
  // bat IPv6 len roi thi khong go duoc trong phien nay, ma tren mang chi co
  // IPv4 dinh tuyen (router gia dinh) thi moi ket noi sau do deu chet o 15 s.
  // ===========================================================================
  // 🔴 KHONG bat tay TLS o day nua. Chi dat DNS roi tra ve.
  // ===========================================================================
  // Ban truoc bat tay tron ven mot lan de "chung minh duong di", roi stop()
  // ngay va giao lai moi dia chi. Do that (2026-08-19, hotspot iPhone): lan
  // bat tay ay XONG trong 5496 ms roi bi vut; toi luc bam nut, bat tay lai
  // tren DUNG dia chi do chet o 8059 ms voi mbedtls -1. Phep thu khong du bao
  // duoc lan that — no chi tieu mat mot lan bat tay dang le dung duoc, va moi
  // lan bat tay bo di con lam phan manh RAM noi (mbedtls xin ~45 KB LIEN).
  //
  // Gio viec do api_client lam bang apiWarmUp(): cung mot lan bat tay, nhung
  // GIU phien lai cho lan bam nut dau tien. Mot lan bat tay cho ca phien, thay
  // vi mot lan bo di cong mot lan that.
  //
  // Chuoi day du nam o file .ino: wifiConnect() -> apiResolve() -> apiWarmUp().
  dnsSetPublicFirst();
  return true;
}

// =====================================================
// Do toc do tai ve tu mot host khac han server API
// =====================================================
// Dia chi de thang o day chu khong len app_config.h: no chi phuc vu mot phep
// do chan doan, khong khoi nao khac dung toi.
void wifiDownloadTest() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Chua vao mang — khong do duoc");
    return;
  }

  const char *HOST = "mirror.bizflycloud.vn";
  const char *PATH = "/ubuntu/dists/noble/Release";

  NetworkClient c;
  c.setTimeout(30000);

  unsigned long t = millis();
  if (!c.connect(HOST, 80, 15000)) {
    Serial.printf("Khong noi duoc %s:80 sau %lu ms\n", HOST, millis() - t);
    return;
  }
  Serial.printf("TCP %s:80 OK (%lu ms)\n", HOST, millis() - t);

  String req = String("GET ") + PATH + " HTTP/1.1\r\n"
               "Host: " + HOST + "\r\n"
               "User-Agent: ESP32S3-VisionCare/1.0\r\n"
               "Connection: close\r\n\r\n";
  c.write((const uint8_t *)req.c_str(), req.length());

  String status = c.readStringUntil('\n');
  status.trim();
  Serial.printf("Server: %s\n", status.length() ? status.c_str() : "(khong tra loi)");

  // Nuot het header roi moi bat dong ho — de con so do duoc la toc do cua
  // THAN, khong lan thoi gian server nghi.
  while (c.connected() || c.available()) {
    String line = c.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break;
  }

  static uint8_t sink[2048];
  size_t got = 0, nextMark = 65536;
  unsigned long t0 = millis(), tLast = t0;

  while (c.connected() || c.available()) {
    int n = c.read(sink, sizeof(sink));
    if (n > 0) {
      got += n;
      tLast = millis();
      if (got >= nextMark) {
        unsigned long d = millis() - t0;
        Serial.printf("  %u KB sau %lu ms (%.1f KB/s)\n",
                      (unsigned)(got / 1024), d, d ? got / 1.024 / d : 0.0);
        nextMark += 65536;
      }
      continue;
    }
    if (millis() - tLast > 20000) { Serial.println("  DUNG HAN 20 s"); break; }
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }

  unsigned long dAll = millis() - t0;
  Serial.printf("Tai ve %u byte trong %lu ms (%.1f KB/s)\n",
                (unsigned)got, dAll, dAll ? got / 1.024 / dAll : 0.0);
  c.stop();
}

// =====================================================
// Do toc do day du lieu that ra WAN, khong qua TLS
// =====================================================
// Gui lai dung mot khoi 4 KB nhieu lan cho du so byte, thay vi xin mot vung
// 200 KB: chi can BIET byte co ra duoc hay khong, khong can byte co nghia. Va
// nhu vay ham nay khong dung toi bo dem cua khoi audio — giu dung quy uoc
// "khoi khong goi sang khoi ngang hang".
void wifiUploadTest(size_t totalBytes, size_t chunkBytes, bool noDelay) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Chua vao mang — khong do duoc");
    return;
  }

  static uint8_t chunk[4096];
  memset(chunk, 'A', sizeof(chunk));
  if (chunkBytes == 0 || chunkBytes > sizeof(chunk)) chunkBytes = sizeof(chunk);

  NetworkClient c;
  c.setTimeout(30000);

  unsigned long t = millis();
  if (!c.connect(API_HOST, 80, 15000)) {
    Serial.printf("Khong noi duoc cong 80 sau %lu ms\n", millis() - t);
    return;
  }
  Serial.printf("TCP cong 80 OK (%lu ms)\n", millis() - t);

  // 🔴 setNoDelay PHAI goi SAU connect(). Truoc do chua co socket, ham tra
  // "errno 9 Bad file number" roi di tiep im lang — va Nagle van gom cac lan
  // ghi 512 byte thanh goi day MSS, nen phep thu "goi nho" am tham bien
  // thanh phep thu "goi to" ma khong ai biet. Da dinh dung mot lan.
  c.setNoDelay(noDelay);
  Serial.printf("Moi lan ghi %u byte, NoDelay %s\n",
                (unsigned)chunkBytes, noDelay ? "BAT" : "tat");

  String head = String("POST ") + API_PATH + " HTTP/1.1\r\n"
                "Host: " API_HOST "\r\n"
                "User-Agent: ESP32S3-VisionCare/1.0\r\n"
                "Content-Type: application/octet-stream\r\n"
                "Content-Length: " + String(totalBytes) + "\r\n"
                "Connection: close\r\n\r\n";
  c.write((const uint8_t *)head.c_str(), head.length());

  // In moc tung 32 KB. Con so nay moi tra loi duoc cau hoi that: dung han o
  // 0 byte la duong ra bi chan, con bo tu tu la bi bop bang thong.
  size_t sent = 0, nextMark = 32768;
  unsigned long t0 = millis(), tLast = t0;
  bool stalled = false;

  while (sent < totalBytes) {
    size_t want = totalBytes - sent;
    if (want > chunkBytes) want = chunkBytes;
    size_t n = c.write(chunk, want);
    if (n == 0) {
      if (!c.connected()) { Serial.println("  ket noi bi dong giua chung"); break; }
      if (millis() - tLast > 20000) { stalled = true; break; }
      vTaskDelay(5 / portTICK_PERIOD_MS);
      continue;
    }
    sent += n;
    tLast = millis();
    if (sent >= nextMark) {
      unsigned long d = millis() - t0;
      Serial.printf("  %u KB sau %lu ms (%.1f KB/s)\n",
                    (unsigned)(sent / 1024), d, d ? sent / 1.024 / d : 0.0);
      nextMark += 32768;
    }
  }

  unsigned long dAll = millis() - t0;
  Serial.printf("Day duoc %u/%u byte trong %lu ms (%.1f KB/s)%s\n",
                (unsigned)sent, (unsigned)totalBytes, dAll,
                dAll ? sent / 1.024 / dAll : 0.0,
                stalled ? " — DUNG HAN 20 s khong nhuc nhich" : "");

  String status = c.readStringUntil('\n');
  status.trim();
  Serial.printf("Server: %s\n", status.length() ? status.c_str() : "(khong tra loi)");
  c.stop();
}

// =====================================================
// Kiem tra duong mang theo tung chang
// =====================================================
// Tach ba chang de biet hong o dau: WiFi -> DNS -> TCP/TLS.
void wifiSelfTest() {
  Serial.printf("WiFi   : %s\n",
                WiFi.status() == WL_CONNECTED ? "da noi" : "CHUA NOI");
  Serial.printf("SSID   : %s\n", WiFi.SSID().c_str());
  Serial.printf("IP     : %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
  Serial.printf("DNS    : %s\n", WiFi.dnsIP().toString().c_str());
  Serial.printf("RSSI   : %d dBm\n", (int)WiFi.RSSI());

  if (WiFi.status() == WL_CONNECTED) {
    IPAddress ip;
    unsigned long t = millis();
    // 🔴 So == 1. That bai ham nay tra ve ma loi err_t AM, ma so am trong C
    // cung la "true" — viet if (WiFi.hostByName(...)) la nhanh loi bi doc
    // thanh thanh cong. Da in ra "DNS -> 0.0.0.0 (14001 ms)" roi di tiep nhu
    // khong co gi, va moi chang sau do doc ra deu vo nghia.
    if (WiFi.hostByName(API_HOST, ip) == 1) {
      Serial.printf("DNS %s -> %s (%lu ms)\n",
                    API_HOST, ip.toString().c_str(), millis() - t);

      WiFiClientSecure c2;
      c2.setInsecure();
      // 🔴 Bat tay TLS co dong ho rieng, khong theo tham so cua connect().
      // Thieu dong nay da do duoc 124304 ms cho mot lan "timeout 15 giay".
      c2.setHandshakeTimeout(15);
      t = millis();
      if (c2.connect(API_HOST, API_PORT, 15000)) {
        Serial.printf("TCP/TLS 443 OK (%lu ms) — duong mang tot\n", millis() - t);
        c2.stop();
      } else {
        Serial.printf("TCP/TLS 443 HONG (%lu ms)\n", millis() - t);
        Serial.printf("Heap con %u byte (bat tay TLS can ~45 KB)\n",
                      (unsigned)ESP.getFreeHeap());
      }
    } else {
      Serial.printf("DNS HONG (%lu ms) — mang khong phan giai duoc ten mien\n",
                    millis() - t);
    }

    // Chang tren dung dung ho dia chi ma core tu chon. Hai dong duoi ep thu
    // ca hai, de biet loi la "khong ra duoc internet" hay chi la "chon nham
    // ho dia chi" — hai chuyen phai sua khac han nhau.
    Serial.println("--- thu rieng tung ho dia chi ---");
    probeFamilyPrint("IPv4", AF_INET);
    probeFamilyPrint("IPv6", AF_INET6);
  } else {
    // Chua vao duoc mang thi ba chang sau khong con y nghia — doi lai thanh
    // cau hoi "AP co ton tai khong" va "driver bao hong o dau".
    Serial.printf("Ly do ngat gan nhat: %d — %s\n",
                  g_lastReason, wifiReasonText(g_lastReason));
    Serial.println();
    wifiScanReport();
  }
}
