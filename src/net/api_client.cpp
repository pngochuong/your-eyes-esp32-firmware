#include "api_client.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
#include "../../app_config.h"

// 🔴 KHONG co vong thu lai trong ma. Bat tay TLS thanh cong do duoc 1321 va
// 3321 ms; qua ngan sach NET_HANDSHAKE_S la duong do dang hong that, cho them
// cung khong xanh len. Ma nguoi dung khiem thi ngoi cho im lang la te nhat.
//
// Hong thi keu tieng bip roi ve — nguoi dung bam lai. Do la co che thu lai
// don gian nhat va cung nhanh nhat, khong ton mot dong trang thai nao.

// ============================================================================
// Bien cua KHOI, khong phai cua mot lan goi
// ============================================================================
// Socket phai song tu luc bam nut toi luc doc xong than; dia chi phai song ca
// phien. Ca hai deu dai hon bat ky bien cuc bo nao ben goi.
static WiFiClientSecure g_client;

static IPAddress     g_ip;
static bool          g_haveIp = false;
static unsigned long g_ipAt   = 0;

// Phien TLS dang giu de dung lai. `g_aliveAt` = luc doc xong than lan truoc.
// Chi dat khi than da duoc doc HET — con sot byte thi khong duoc dung lai, xem
// ghi chu o TLS_KEEPALIVE_MS.
static bool          g_reusable = false;
static unsigned long g_aliveAt  = 0;

static const char *BOUND = "----ESP32VisionCareBoundary7d91";

// ============================================================================
// May trang thai cua mot lan gui
// ============================================================================
// 🔴 Quy uoc ghi/doc, khong co mutex nao ca — dung sai la hong im lang:
//   g_st            CHI task mang ghi.  Task audio chi doc.
//   g_img/g_aud/... CHI task audio ghi, va chi ghi MOT lan moi lan bam, TRUOC
//                   khi task mang co the thay chung (task mang doi con tro
//                   khac null moi doc). Sau do khong ai ghi nua.
//   g_abort         CHI task audio ghi.
// Mot bien volatile la du cho kieu quan he nay: mot nguoi ghi, mot nguoi doc,
// va khong co buoc nao doc-roi-ghi-lai.
enum NetState {
  ST_IDLE,        // khong co lan gui nao dang chay
  ST_CONNECT,     // dang bat tay TLS
  ST_WAIT_IMG,    // TLS xong, dang doi anh
  ST_SEND_IMG,    // dang day header + phan anh
  ST_WAIT_AUD,    // anh xong, dang doi tieng
  ST_SEND_AUD,    // dang day phan tieng + dong goi
  ST_READ_HDR,    // dang doc dong trang thai + header
  ST_DONE,        // co header roi, than san sang
  ST_FAIL         // hong o dau do
};
static volatile NetState g_st = ST_IDLE;

// 🔴 Rieng bien nay KHONG suy ra duoc tu g_st. apiAbort() co tran cho: het
// tran ma task chua thoat thi no van dat g_st = ST_IDLE de lan bam sau con
// dung duoc. Luc do trang thai noi "ranh" trong khi task cu VAN dang chay va
// van dang ghi vao g_client. Mo them mot task nua o day la hai task cung ghi
// mot socket — hong im lang, va rat kho lan ra.
// Task tu dat co nay: bat luc vao, tat ngay truoc vTaskDelete.
static volatile bool g_taskAlive = false;

static const uint8_t *volatile g_img    = nullptr;
static volatile size_t         g_imgLen = 0;
static volatile bool           g_abort  = false;

// ============================================================================
// Phan tieng di theo LUONG
// ============================================================================
// Task audio nen duoc bao nhieu khoi ADPCM thi task mang day len bay nhieu,
// khong doi thu xong. Luc nguoi dung nha nut chi con khoi cuoi phai day.
//
// 🔴 Mot bien dem tang dan la du — khong hang doi, khong mutex. Task audio la
// nguoi ghi DUY NHAT cua g_audFill va no chi tang; task mang la nguoi doc duy
// nhat va chi day khoang [da_gui, g_audFill). Bo dem thi khong bao gio bi ghi
// de: no du chua tron MAX_SECS giay. Them khoa vao chi to hon chu khong an
// toan hon.
static const uint8_t *volatile g_aud     = nullptr;
static volatile size_t         g_audFill = 0;
static volatile bool           g_audDone = false;
static const char             *g_audName = "record.bin";
static const char             *g_audType = "application/octet-stream";

// Ket qua task mang doc duoc, task audio nhat ve sau khi thay ST_DONE.
static int      g_code    = 0;
static bool     g_chunked = false;
static long     g_clen    = -1;
static bool     g_isMp3   = false;
static AudioFmt g_fmt;
static unsigned long g_tSend = 0;

// Hoi mbedtls xem vi sao hong, roi in ra.
//
// 🔴 NetworkClientSecure::write() tra 0 cho HAI ca khac han nhau: phien da
// dong tu truoc (_connected = false), va mbedtls vua bao loi nen ham do tu goi
// stop(). Khong hoi lastError() thi hai ca nay nhin giong het nhau, ma huong
// sua thi nguoc nhau. Chinh mbedtls co in ly do bang log_e(), nhung ban dung
// hien tai khong dat DebugLevel nen moi dong do bi bien dich BO — nen phai tu
// lay ma loi ra day.
static void tlsWhy(const char *where) {
  char msg[128] = {0};
  int e = g_client.lastError(msg, sizeof(msg));
  Serial.printf("  %s: connected=%d, mbedtls %d (-0x%04X) %s\n",
                where, (int)g_client.connected(), e, (unsigned)(-e), msg);
  Serial.printf("  RSSI %d dBm, heap %u B, khoi lien lon nhat %u B\n",
                (int)WiFi.RSSI(), (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(
                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

// Ghi het len socket TLS. client.write() co the ghi thieu khi buffer day,
// nen phai lap — day la loi im lang hay gap nhat khi POST file lon.
static bool writeAll(const uint8_t *p, size_t len) {
  size_t sent = 0;
  unsigned long t0 = millis();
  while (sent < len) {
    if (g_abort) return false;
    size_t n = g_client.write(p + sent, len - sent);
    if (n == 0) {
      if (!g_client.connected() || millis() - t0 > (unsigned long)NET_TIMEOUT_MS) {
        // Bao nhieu byte da qua duoc la con so tach hai ca: dung o 0 byte
        // nghia la phien chet ngay sau bat tay (server/middlebox da dong),
        // dung o giua nghia la duong truyen doi y giua chung.
        Serial.printf("  ghi dung o %u/%u byte sau %lu ms\n",
                      (unsigned)sent, (unsigned)len, millis() - t0);
        tlsWhy("ghi hong");
        return false;
      }
      vTaskDelay(5 / portTICK_PERIOD_MS);
      continue;
    }
    sent += n;
    t0 = millis();
  }
  return true;
}

static bool writeAll(const String &s) {
  return writeAll((const uint8_t *)s.c_str(), s.length());
}

// Mot mieng cua than chunked: do dai he 16, xuong dong, du lieu, xuong dong.
//
// Ca phan mo dau multipart lan du lieu deu di qua day. Mieng to bao nhieu
// cung duoc — mbedtls tu cat thanh ban ghi TLS 16 KB, con Cloudflare thi gom
// lai theo y no. Nen mot mieng cho mot phan la du, khong can cat nho tay.
static bool writeChunk(const uint8_t *p, size_t len) {
  if (len == 0) return true;
  char head[16];
  int  n = snprintf(head, sizeof(head), "%X\r\n", (unsigned)len);
  return writeAll((const uint8_t *)head, n)
      && writeAll(p, len)
      && writeAll((const uint8_t *)"\r\n", 2);
}

static bool writeChunk(const String &s) {
  return writeChunk((const uint8_t *)s.c_str(), s.length());
}

// ============================================================================
// Phan giai ten mien — mot lan cho ca phien
// ============================================================================
// Gia tri cua khoi nay khong phai o viec phan giai (core lam roi) ma o viec
// NHO KET QUA. Truoc kia moi lan bam nut deu hoi DNS hai luot; do that tren
// hotspot Redmi la 14 giay moi luot, cong thang vao khoang nguoi dung ngoi cho
// sau khi nha nut. Gio hoi mot lan luc khoi dong, roi connect bang IP.
void apiSetAddress(IPAddress ip) {
  g_ip     = ip;
  g_haveIp = true;
  g_ipAt   = millis();
  Serial.printf("Dia chi server dat truc tiep: %s (%s) — da bat tay TLS duoc\n",
                ip.toString().c_str(), ip.type() == IPv6 ? "IPv6" : "IPv4");
}

bool apiHaveAddress() { return g_haveIp; }

void apiPrintAddress() {
  if (!g_haveIp) {
    Serial.println("Dia chi server: CHUA phan giai duoc");
    return;
  }
  Serial.printf("Dia chi server: %s (%s, nho duoc %lu giay)\n",
                g_ip.toString().c_str(),
                g_ip.type() == IPv6 ? "IPv6" : "IPv4",
                (millis() - g_ipAt) / 1000);
}

bool apiResolve(bool force) {
  if (!force && g_haveIp && millis() - g_ipAt < DNS_CACHE_MS) return true;
  if (WiFi.status() != WL_CONNECTED) return false;

  // Dung ham co san cua core, KHONG tu goi lwip_getaddrinfo nua.
  // NetworkManager::hostByName() cua 3.3.10 da lam dung ba viec ta can:
  //   - giao dien co IPv6 toan cuc  -> hoi AAAA truoc va dung luon
  //   - con lai                     -> AF_UNSPEC, ma lwip thi uu tien IPv4
  //   - trang thai dia chi vua doi   -> tu goi dns_clear_cache()
  // Dung y ta muon, lai duoc them vu xoa cache mien phi.
  //
  // 🔴 Phai so == 1, KHONG duoc viet if (WiFi.hostByName(...)). That bai no
  // tra ve ma loi err_t AM, ma so am trong C cung la "true" — nen nhanh loi bi
  // doc thanh thanh cong, va dia chi nhan duoc la 0.0.0.0 (ham dat nhu vay o
  // dong dau). Da doc nham dung kieu do mot lan: log in ra
  // "DNS -> 0.0.0.0 (14001 ms)" roi di tiep nhu khong co gi.
  IPAddress     ip;
  unsigned long t = millis();
  bool ok = (WiFi.hostByName(API_HOST, ip) == 1) && ip != IPAddress((uint32_t)0);
  unsigned long ms = millis() - t;

  if (ok) {
    g_ip = ip; g_haveIp = true; g_ipAt = millis();
    Serial.printf("Phan giai %s -> %s (%s, %lu ms)\n", API_HOST,
                  ip.toString().c_str(),
                  ip.type() == IPv6 ? "IPv6" : "IPv4", ms);
    return true;
  }

  Serial.printf("Phan giai %s HONG (%lu ms)\n", API_HOST, ms);

  // Giu lai dia chi cu neu co: mot lan hoi truot khong co nghia la dia chi cu
  // da sai, ma mat no thi lan bam tiep theo khong con gi de ket noi.
  return g_haveIp;
}

// ============================================================================
// Mo TLS toi dia chi da nho
// ============================================================================
// 🔴 Connect bang IP nhung VAN phai dua ten mien vao lam SNI. Cloudflare dung
// SNI de biet dang hoi site nao; connect(IPAddress, port) khong gui SNI nen se
// bi tu choi bat tay. Nap chong 6 tham so duoi day la duong duy nhat vua bo
// duoc DNS vua giu duoc SNI.
static bool openTls() {
  if (!g_haveIp) return false;
  IPAddress ip = g_ip;

  // Con phien cu dung duoc thi khong bat tay lai — day la chang dat nhat cua
  // ca luong (3212 ms do tren FTTH) va tu lan bam thu hai no co the bang 0.
  if (g_reusable && g_client.connected() &&
      millis() - g_aliveAt < TLS_KEEPALIVE_MS) {
    Serial.printf("  Dung lai phien TLS cu (mo %lu giay truoc) — bo qua bat tay\n",
                  (millis() - g_aliveAt) / 1000);
    return true;
  }
  if (g_reusable)
    Serial.println("  Phien cu khong dung lai duoc — bat tay moi");
  g_reusable = false;

  g_client.stop();
  // ⚠️ Bo qua kiem tra chung chi. Xem ghi chu bao mat trong huong dan.
  g_client.setInsecure();
  // 🔴 Ca hai deu tinh bang MILLI giay o core 3.x, va deu can:
  //   setConnectionTimeout -> timeout muc socket (connect + read + write)
  //   setTimeout           -> timeout cua Stream, tuc readStringUntil()
  // 🔴 Dat RONG ngay tu day, khong dat gat roi noi lai sau connect.
  // start_ssl_client() nap gia tri nay vao SO_SNDTIMEO/SO_RCVTIMEO cua socket
  // NGAY luc connect; goi setConnectionTimeout() sau do khong con doi duoc
  // option da nap. Da tra gia dung mot lan: dat 5000 truoc connect roi noi lai
  // 60000 sau connect, ket qua la lan ghi anh bao "ghi dung o 0/60537 byte sau
  // 5004 ms" — socket tu chet o dung 5 giay, dung con so cu.
  //
  // Muon chan RIENG lan bat tay thi dung setHandshakeTimeout() ben duoi — no
  // o tang mbedtls, khong dinh gi toi option cua socket.
  g_client.setConnectionTimeout(NET_TIMEOUT_MS);
  g_client.setTimeout(NET_TIMEOUT_MS);
  // 🔴 Bat tay TLS co dong ho RIENG, mac dinh 120 giay, khong theo tham so
  // cua connect(). Thieu dong nay da do duoc 124304 ms cho mot lan "timeout
  // 15 giay". Don vi la GIAY.
  g_client.setHandshakeTimeout(NET_HANDSHAKE_S);

  unsigned long t = millis();
  bool ok = g_client.connect(ip, API_PORT, API_HOST, nullptr, nullptr, nullptr);

  if (!ok) {
    Serial.printf("  TCP/TLS toi %s HONG sau %lu ms\n",
                  ip.toString().c_str(), millis() - t);
    tlsWhy("bat tay hong");
    return false;
  }
  Serial.printf("  TCP/TLS %s OK (%lu ms)\n", ip.toString().c_str(), millis() - t);
  return true;
}

bool apiWarmUp() {
  if (!g_haveIp) return false;

  unsigned long t = millis();
  if (!openTls()) {
    Serial.printf("Bat tay TLS truoc KHONG xong (%lu ms) — se thu lai o lan bam nut\n",
                  millis() - t);
    return false;
  }

  // 🔴 Danh dau dung lai duoc NGAY o day. Khong co hai dong nay thi phien vua
  // mo van bi openTls() cua lan bam dau tien coi la "khong dung lai duoc" va
  // bat tay lai tu dau — tuc quay ve dung cai loi vua sua.
  g_reusable = true;
  g_aliveAt  = millis();
  Serial.printf("Bat tay TLS truoc XONG (%lu ms) — giu phien cho lan bam dau\n",
                millis() - t);
  return true;
}

// Doi mot con tro duoc giao toi, hoac bo cuoc. Tra ve false khi het gio / bi
// huy — luc do ca lan gui coi nhu hong.
static bool waitFor(const uint8_t *volatile *p, unsigned long timeoutMs) {
  unsigned long t = millis();
  while (*p == nullptr) {
    if (g_abort) return false;
    if (millis() - t > timeoutMs) return false;
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  return true;
}

// ============================================================================
// Doc dong trang thai + header tra ve
// ============================================================================
static bool readHeaders() {
  String status = g_client.readStringUntil('\n');
  status.trim();
  Serial.printf("[%lu ms] Server: %s\n", millis() - g_tSend, status.c_str());
  if (status.length() == 0) {
    Serial.println("Server khong tra dong trang thai — dut ket noi hoac timeout");
    return false;
  }

  g_code = 0;
  { int sp = status.indexOf(' '); if (sp > 0) g_code = status.substring(sp + 1, sp + 4).toInt(); }

  // In HET header. Khong doan mo: doi khi loi nam ngay o day (content-type
  // la application/json, hoac co Content-Length nghia la server van dang gom).
  g_clen    = -1;
  g_chunked = false;
  g_isMp3   = false;
  g_fmt     = AudioFmt{};
  int nHdr  = 0;

  while (g_client.connected() || g_client.available()) {
    String line = g_client.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break;                  // het header
    Serial.printf("  < %s\n", line.c_str());
    nHdr++;
    String low = line; low.toLowerCase();
    if (low.startsWith("content-length:"))     g_clen = line.substring(15).toInt();
    if (low.startsWith("transfer-encoding:") && low.indexOf("chunked") >= 0) g_chunked = true;

    // Nhan MP3 qua ca hai duong: header rieng cua server, hoac Content-Type
    // chuan. Kiem ca hai de khong phu thuoc mot ben nho khong doi.
    if ((low.startsWith("x-audio-format:") && low.indexOf("mp3") >= 0) ||
        (low.startsWith("content-type:") &&
         (low.indexOf("audio/mpeg") >= 0 || low.indexOf("audio/mp3") >= 0))) {
      g_isMp3 = true;
    } else if (low.startsWith("x-audio-format:")) {
      parseAudioFormat(line.substring(15), g_fmt);
    }
  }
  Serial.printf("[%lu ms] Het header (%d dong)\n", millis() - g_tSend, nHdr);

  if (g_code != 200) {
    Serial.printf("Server tra ma %d — khong phat\n", g_code);
    return false;
  }
  if (!g_chunked && g_clen <= 0) {
    Serial.println("Tra loi khong co Content-Length va khong chunked");
    return false;
  }
  return true;
}

// ============================================================================
// Task mang — chay tron mot lan gui tu dau toi khi co header
// ============================================================================
// Chay tron mot lan gui. Tra ve false o bat ky chang nao hong — nguoi goi lo
// viec dong socket va dat trang thai, nen o day chi can `return false`.
static bool netRun() {
  // --- Buoc 1: bat tay TLS. Chay trong luc dang chup anh.
  //
  // Thu het danh sach dang nho; van hong thi hoi lai DNS roi thu het lan nua.
  // 🔴 Vong hai la bat buoc, khong phai cho chac an: do that, ca danh sach
  // dang nho co the da cu — Cloudflare doi IP va bo cu ngung nhan ket noi.
  // Lan hoi lai tra ve dia chi khac han va bat tay duoc ngay.
  g_st = ST_CONNECT;
  if (!openTls()) return false;

  // --- Buoc 2: doi anh, roi day header + phan anh.
  // Tran doi = thoi gian chup mot loat, rong tay. Qua han nghia la ben kia da
  // bo cuoc ma quen bao.
  g_st = ST_WAIT_IMG;
  if (!waitFor(&g_img, 20000)) return false;

  g_st   = ST_SEND_IMG;
  g_tSend = millis();

  // 🔴 TUYET DOI khong them "Expect: 100-continue". Da thu that: server nay
  // khong tra loi 100, ben gui se treo cho toi khi timeout. curl tu them
  // header do cho body > 1 KB nen luc test bang curl phai dung -H "Expect:".
  //
  // 🔴 Transfer-Encoding: chunked chu khong Content-Length — luc gui dong nay
  // ban thu con chua duoc thu, khong the biet tong do dai. Da do that: server
  // FastAPI/uvicorn sau Cloudflare nhan chunked va tra 200 binh thuong.
  //
  // Accept liet ke theo thu tu UU TIEN GIAM DAN. ADPCM dat truoc WAV tho vi
  // no it hon 4 lan byte ma server encode ton ~0 — server chi can doi tham so
  // codec cua ffmpeg. Van giu ca ba de doi server khong dong bo voi doi board
  // thi khong ai chet: server cu tra thu no biet lam, board nuot duoc het.
  String head = String("POST ") + API_PATH + " HTTP/1.1\r\n"
                "Host: " API_HOST "\r\n"
                "User-Agent: ESP32S3-VisionCare/1.0\r\n"
                "Accept: audio/wav;codec=ima_adpcm, audio/mpeg, audio/wav\r\n"
                "Content-Type: multipart/form-data; boundary=" + BOUND + "\r\n"
                "Transfer-Encoding: chunked\r\n"
                // 🔴 keep-alive chu khong close. Bat tay TLS mat 3212 ms, va no
                // la chang dat nhat con lai sau khi DNS da duoc nho san. Giu
                // phien lai thi tu lan bam thu hai tro di chang do bang 0.
                // Doi lai mot rang buoc: than tra ve phai duoc doc HET, xem
                // apiClose().
                "Connection: keep-alive\r\n\r\n";

  String pImg = String("--") + BOUND + "\r\n"
                "Content-Disposition: form-data; name=\"image\"; filename=\"capture.jpg\"\r\n"
                "Content-Type: image/jpeg\r\n\r\n";

  size_t        imgLen = g_imgLen;
  unsigned long tImg0  = millis();

  if (!writeAll(head) || !writeChunk(pImg) ||
      !writeChunk((const uint8_t *)g_img, imgLen)) {
    Serial.println("Dut ket noi khi day anh");
    return false;
  }

  unsigned long dImg = millis() - tImg0;
  Serial.printf("Day xong anh %u KB trong %lu ms (%.1f KB/s) — trong luc dang thu tieng\n",
                (unsigned)(imgLen / 1024), dImg,
                dImg ? imgLen / 1.024 / dImg : 0.0);

  // --- Buoc 3: mo phan tieng, roi day THEO LUONG trong luc nguoi dung con noi.
  // Tran doi = tran thu am cong du phong. Nguoi dung giu nut toi da MAX_SECS.
  g_st = ST_WAIT_AUD;
  if (!waitFor(&g_aud, (unsigned long)MAX_SECS * 1000 + 20000)) return false;

  g_st = ST_SEND_AUD;

  String pAud = String("\r\n--") + BOUND + "\r\n"
                "Content-Disposition: form-data; name=\"audio\"; filename=\""
                + g_audName + "\"\r\n"
                "Content-Type: " + g_audType + "\r\n\r\n";
  String pEnd = String("\r\n--") + BOUND + "--\r\n";

  if (!writeChunk(pAud)) {
    Serial.println("Dut ket noi khi mo phan tieng");
    return false;
  }

  size_t        sent  = 0;
  unsigned long tAud0 = millis();
  unsigned long tIdle = millis();

  for (;;) {
    if (g_abort) return false;

    // Doc MOT lan roi lam viec voi ban sao: con so dang tang trong khi ta ghi,
    // hai lan doc khac nhau trong cung mot vong se lech nhau.
    size_t have = g_audFill;
    bool   fin  = g_audDone;      // doc SAU `have` — xem ghi chu ben duoi

    if (have > sent) {
      // Gom moi thu da co vao MOT mieng chunked. Mieng cang to thi 8 byte
      // tieu de chunk cang chia deu ra nhieu — va tren duong cham thi mot
      // vong nhu vay thuong da gom san vai khoi.
      if (!writeChunk(g_aud + sent, have - sent)) {
        Serial.println("Dut ket noi khi day tieng");
        return false;
      }
      sent  = have;
      tIdle = millis();
      continue;
    }

    // 🔴 `fin` phai doc SAU `have`. Doc truoc thi co the roi vao: thay
    // fin = false, roi task audio ghi not khoi cuoi va dat done, roi ta doc
    // have (cu) va thoat vong — mat khoi cuoi. Doc sau thi he qua nguoc lai:
    // fin = true bao dam `have` da la con so cuoi cung.
    if (fin && sent >= g_audFill) break;

    if (millis() - tIdle > (unsigned long)MAX_SECS * 1000 + 20000) {
      Serial.println("Khong nhan them tieng nua — bo cuoc");
      return false;
    }
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }

  // Mieng cuoi cung "0\r\n\r\n" bao het than. Thieu no thi server ngoi doi
  // toi khi timeout roi tra 400.
  if (!writeChunk(pEnd) || !writeAll((const uint8_t *)"0\r\n\r\n", 5)) {
    Serial.println("Dut ket noi khi dong goi");
    return false;
  }

  unsigned long dAud = millis() - tAud0;
  Serial.printf("Day xong tieng %u KB trong %lu ms (%.1f KB/s)\n",
                (unsigned)(sent / 1024), dAud,
                dAud ? sent / 1.024 / dAud : 0.0);

  // --- Buoc 4: doc header tra ve. Chang nay la thoi gian SERVER nghi.
  g_st = ST_READ_HDR;
  return readHeaders();
}

static void netTask(void *) {
  g_taskAlive = true;

  bool ok = netRun();
  if (!ok) { g_reusable = false; g_client.stop(); }
  g_st = ok ? ST_DONE : ST_FAIL;

  // Dat co TRUOC vTaskDelete. Sau vTaskDelete khong con dong nao chay nua —
  // de sau la co khong bao gio duoc tat, va apiBegin() se cho no vinh vien.
  g_taskAlive = false;
  vTaskDelete(nullptr);
}

// ============================================================================
void apiBegin() {
  if (g_st != ST_IDLE) return;              // dang co mot lan gui do dang

  // Task cua lan truoc co the con sot lai neu apiAbort() da het tran cho.
  // Doi no thoat han roi moi mo lan moi — hai task cung ghi mot socket TLS la
  // hong im lang. Cho co tran de mot su co khong khoa chet cai nut.
  if (g_taskAlive) {
    unsigned long t = millis();
    while (g_taskAlive && millis() - t < 3000) vTaskDelay(20 / portTICK_PERIOD_MS);
    if (g_taskAlive) {
      Serial.println("Task mang cu chua thoat — bo qua lan gui nay");
      return;
    }
    Serial.printf("Task mang cu vua thoat sau %lu ms\n", millis() - t);
  }

  g_img = nullptr; g_imgLen = 0;
  g_aud = nullptr; g_audFill = 0; g_audDone = false;
  g_abort = false;

  if (WiFi.status() != WL_CONNECTED) { g_st = ST_FAIL; return; }
  if (!g_haveIp)                     { g_st = ST_FAIL; return; }

  g_st = ST_CONNECT;

  // Stack 12 KB: bat tay TLS cua mbedtls mot minh da an ~8 KB, con lai cho
  // may bien String dung header.
  // Ghim core 0 — noi WiFi/TCP dang chay san, de core 1 danh tron cho I2S.
  if (xTaskCreatePinnedToCore(netTask, "netsend", 12288, nullptr, 1,
                              nullptr, 0) != pdPASS) {
    Serial.println("Khong tao duoc task mang");
    g_st = ST_FAIL;
  }
}

void apiPushImage(const uint8_t *jpg, size_t jpgLen) {
  if (g_st == ST_IDLE || g_st == ST_FAIL) return;
  // Do dai TRUOC con tro: task mang doi con tro khac null roi moi doc do dai,
  // nen do dai phai co san truoc do. Nguoc lai la co luc no doc duoc 0.
  g_imgLen = jpgLen;
  g_img    = jpg;
}

void apiAudioOpen(const uint8_t *buf, const char *filename,
                  const char *contentType) {
  if (g_st == ST_IDLE || g_st == ST_FAIL) return;
  g_audName = filename;
  g_audType = contentType;
  g_audFill = 0;
  g_audDone = false;
  // 🔴 Con tro dat SAU cung, cung mot ly do nhu apiPushImage(): task mang doi
  // con tro khac null roi moi doc ba truong tren. Dat truoc thi co luc no doc
  // phai nhan cua lan bam cu.
  g_aud     = buf;
}

void apiPushAudio(size_t totalBytes, bool done) {
  if (g_st == ST_IDLE || g_st == ST_FAIL) return;
  // 🔴 So byte dat TRUOC co `done`, va task mang doc theo thu tu nguoc lai.
  // Nho vay no khong bao gio thay "xong" di kem mot con so cu — tuc khong bao
  // gio bo sot khoi cuoi.
  if (totalBytes > g_audFill) g_audFill = totalBytes;
  if (done) g_audDone = true;
}

bool apiWaitReply(ApiReply &reply, unsigned long timeoutMs) {
  reply.isMp3  = false;
  reply.fmt    = AudioFmt{};
  reply.code   = 0;
  reply.tStart = g_tSend;

  unsigned long t = millis();
  while (g_st != ST_DONE && g_st != ST_FAIL && g_st != ST_IDLE) {
    if (millis() - t > timeoutMs) {
      Serial.printf("Qua %lu ms van chua co header — bo cuoc\n", timeoutMs);
      apiAbort();
      return false;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }

  if (g_st != ST_DONE) { g_st = ST_IDLE; return false; }

  reply.isMp3  = g_isMp3;
  reply.fmt    = g_fmt;
  reply.code   = g_code;
  reply.tStart = g_tSend;
  bodyInit(reply.body, &g_client, g_chunked, g_clen);
  return true;
}

void apiAbort() {
  if (g_st == ST_IDLE) return;

  // Task dang bat tay hoac dang ghi: khong the giet giua chung (mbedtls se ro
  // ri bo nho). Dung co bao roi doi no tu thoat — moi vong doi ben trong deu
  // co tran nen chac chan co diem dung.
  g_abort = true;

  unsigned long t = millis();
  while (g_st != ST_DONE && g_st != ST_FAIL && g_st != ST_IDLE &&
         millis() - t < (unsigned long)NET_CONNECT_MS + 3000)
    vTaskDelay(20 / portTICK_PERIOD_MS);

  g_reusable = false;
  g_client.stop();
  g_abort = false;
  g_st    = ST_IDLE;
}

void apiClose(ApiReply &reply) {
  // 🔴 Chi giu phien lai khi than da doc HET. Con sot byte thi chung se bi doc
  // thanh dong trang thai cua lan sau — hai luong lech nhau vinh vien, va
  // trieu chung la "tu lan bam thu hai tra loi sai bet", rat kho lan ra.
  // Duong nao khong chac cung phai dong: bat tay lai ton 3 giay, doc lech
  // luong thi ton ca thiet bi.
  if (reply.body.eof && g_client.connected()) {
    g_reusable = true;
    g_aliveAt  = millis();
    Serial.println("Giu phien TLS lai cho lan bam sau");
  } else {
    if (!reply.body.eof)
      Serial.println("Than chua doc het — dong phien de khong lech luong");
    g_reusable = false;
    g_client.stop();
  }
  g_st = ST_IDLE;
}
