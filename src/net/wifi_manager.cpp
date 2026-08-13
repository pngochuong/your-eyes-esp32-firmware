#include "wifi_manager.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <lwip/netdb.h>
#include "../../app_config.h"

// =====================================================
// Ly do bi ngat — do driver bao, khong phai doan
// =====================================================
// WiFi.status() chi tra WL_DISCONNECT cho MOI kieu that bai: sai mat khau, sai
// ten, sai bang tan deu ra cung mot ma. Ma that nam trong su kien
// STA_DISCONNECTED cua driver, phai bat rieng moi thay.
static volatile int g_lastReason = 0;

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
// Goi bang TEN MIEN chu khong bang IP: Cloudflare dua vao SNI, connect bang
// IP la bi tu choi bat tay — se doc nham thanh "duong nay hong".
// Ho dia chi nao duoc dung la do trang thai IPv6 cua giao dien quyet dinh
// (xem ghi chu trong wifiConnect), khong can ep o day.
static bool tlsReachable(unsigned long timeoutSec) {
  WiFiClientSecure c;
  c.setInsecure();
  // 🔴 connect(..., timeout) chi chan o muc socket. Bat tay TLS co dong ho
  // RIENG, mac dinh rat dai — thieu dong duoi thi da do duoc 124304 ms cho
  // mot lan "timeout 15 giay".
  c.setHandshakeTimeout(timeoutSec);

  bool ok = c.connect(API_HOST, API_PORT, timeoutSec * 1000);
  if (ok) c.stop();
  return ok;
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
  // Lan dau hay truot vi duong 4G/DNS con nguoi; lan hai da am nen sat thuc te.
  bool v4Ok = false;
  unsigned long tV4 = millis();

  for (int lan = 1; lan <= 2 && !v4Ok; lan++) {
    unsigned long t = millis();
    v4Ok = tlsReachable(10);
    Serial.printf("Thu IPv4 lan %d: %s (%lu ms)\n",
                  lan, v4Ok ? "OK" : "hong", millis() - t);
  }

  if (v4Ok) {
    Serial.printf("Duong ra: IPv4 (bat tay TLS mat %lu ms). Khong bat IPv6.\n",
                  millis() - tV4);
    return true;
  }

  Serial.printf("IPv4 khong ra duoc server (hong ca 2 lan, %lu ms) — bat IPv6 du phong\n",
                millis() - tV4);

  // 🔴 Bat co IPv6 KHONG du — phai noi lai mang. Router chi quang ba SLAAC
  // luc thiet bi moi lien ket; bat co sau do thi phai ngoi cho toi luot quang
  // ba dinh ky, co the lau hon ca phut. Da do that: bat co xong cho 6 giay,
  // "IPv6 toan cuc: KHONG CO", trong khi noi lai thi co dia chi trong ~2 giay.
  WiFi.STA.enableIPv6(true);
  Serial.println("Noi lai mang de xin dia chi IPv6 (SLAAC chi chay luc lien ket)");

  WiFi.disconnect(false, false);
  delay(300);

  if (!beginAndWait(timeoutMs)) {
    Serial.println("Noi lai that bai — mat mang luon");
    Serial.printf("Ly do driver bao: %d — %s\n",
                  g_lastReason, wifiReasonText(g_lastReason));
    return false;
  }

  // Dia chi IPv6 toan cuc do router quang ba (SLAAC), mat them vai giay sau
  // khi lien ket len. Cho co roi moi bao — tren mang IPv6-only thi day moi
  // la dia chi duy nhat ra duoc internet, thieu no la hong ca chuoi.
  const unsigned long v6Start = millis();
  while (!WiFi.STA.hasGlobalIPv6() && millis() - v6Start < 8000) {
    delay(200);
  }

  if (WiFi.STA.hasGlobalIPv6()) {
    Serial.print("IPv6 toan cuc: ");
    Serial.println(WiFi.STA.globalIPv6());
  } else {
    Serial.println("IPv6 toan cuc: KHONG CO — va IPv4 cung khong ra duoc.");
    Serial.println("  Mang nay chua noi duoc internet: kiem tra router, hoac");
    Serial.println("  WiFi co trang dang nhap (captive portal) chua bam qua.");
  }
  return true;
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
    if (WiFi.hostByName(API_HOST, ip)) {
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
