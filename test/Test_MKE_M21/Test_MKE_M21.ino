// ============================================================================
// Test_MKE_M21 — unit test module 4G MakerEDU MKE-M21 (SIMCom SIM7680C)
// ============================================================================
// Sketch DOC LAP. Khong dung camera, khong dung I2S, khong dung mic/loa/ampli
// cua VisionCare. Muc dich duy nhat: chung minh module 4G song, dang ky duoc
// mang, gui duoc SMS va goi duoc dien thoai — TRUOC khi ghep vao he thong.
//
// KHOI NAY LO: bat tay UART, do song, dang ky mang + IMS/VoLTE, SMS, cuoc goi.
// KHOI NAY KHONG LO: chat luong am thanh cuoc goi (di duong analog MIC±/SP±
//                    cua module, khong qua I2S), data 4G/HTTP, tich hop.
//
// Bo lenh AT: ho A76XX cua SIMCom.
//
// Nap: Arduino IDE — Board "ESP32S3 Dev Module". Cac thiet lap PSRAM/Partition
// khong quan trong o sketch nay (chuong trinh rat nho), cu de nguyen cau hinh
// cua VisionCare cho khoi phai doi qua doi lai.
//
// Serial Monitor: 115200 baud, line ending = "Both NL & CR" hoac "New Line".
// ============================================================================
#include <Arduino.h>

// ---------------------------------------------------------------- Cau hinh
// Chan chon tu danh sach GPIO CON TRONG cua board VisionCare.
// Camera an 4-13,15-18 · I2S an 21,41,42,47 · nut an 1.
// Con trong an toan: 2, 14, 38, 39, 40, 48. Tranh 0/3/45/46 (strap) va
// 19/20 (USB-JTAG) — cam vao day co the lam board khong nap duoc nua.
#define M21_TX_PIN   2     // ESP32 phat  -> chan RX cua module
#define M21_RX_PIN   14    // ESP32 nhan  <- chan TX cua module

// 🔴 9600 chu khong phai 115200. Nha san xuat chot baud mac dinh 9600 va
// khuyen khong nang len 115200. Canh bao do nham vao SoftwareSerial tren AVR;
// ESP32-S3 dung UART phan cung nen nang duoc — nhung KHONG nang o buoc test,
// vi test la de biet module co song khong, khong phai de toi uu toc do.
#define M21_BAUD     9600

// Module MKE-M21 KHONG co chan PWRKEY — no tu bat khi co nguon. Co chan RST
// (reset). De -1 la khong dung. Chi noi day khi module treo cung, khong con
// tra loi lenh AT nao ca.
#define M21_RST_PIN  -1     // vi du: 38
#define M21_RST_ON   LOW    // muc tich cuc cua RST

HardwareSerial M21(1);      // dung UART1, UART0 danh cho Serial Monitor

static String   atLast;     // phan hoi cua lenh vua chay
static uint32_t atBaud = M21_BAUD;   // baud dang thuc su chay duoc

// ============================================================================
// Lop AT toi thieu
// ============================================================================

// Do het byte con ket dong trong bo dem vao. Goi truoc moi lenh de phan hoi
// cua lenh truoc (hoac URC nhu "RING") khong bi tinh nham vao lenh nay.
static void atFlush() {
  uint32_t t0 = millis();
  while (millis() - t0 < 50) {
    while (M21.available()) { M21.read(); t0 = millis(); }
  }
}

// Doc phan hoi theo TUNG DONG cho toi khi gap 'want' / "ERROR" / het gio.
// Khong gui gi ca — dung khi can doc tiep phan con lai cua mot lenh da gui.
//
// 🔴 Doc theo dong, va VUT dong echo, la bat buoc. Ban truoc doc theo tung
// KY TU va so khop tren ca cuc du lieu: khi module bat echo, cau lenh "AT+CSQ"
// vong ve nguyen van va chuoi can tim "+CSQ" khop ngay vao chinh cau echo do
// -> ham thoat som, bao "khong doc duoc" trong khi module tra loi hoan toan
// binh thuong. Da mat mot buoi vi cai nay. Dung gop lai thanh so khop tho.
static bool atWait(const char* cmd, const char* want, uint32_t toMs, bool show) {
  String line;
  String cmdStr(cmd);
  cmdStr.trim();
  uint32_t t0 = millis();

  while (millis() - t0 < toMs) {
    while (M21.available()) {
      char c = M21.read();
      if (c == '\r') continue;
      if (c != '\n') {
        line += c;
        if (line.length() > 300) line.remove(0, 150);   // chong tran khi gap rac
        continue;
      }
      line.trim();
      if (!line.length()) continue;                     // dong trong
      if (cmdStr.length() && line == cmdStr) { line = ""; continue; }   // <- echo

      atLast += line; atLast += '\n';
      bool hit = (line.indexOf(want) >= 0);
      bool err = (line.indexOf("ERROR") >= 0);
      line = "";
      if (hit || err) {
        if (show) { Serial.print(F("<< ")); Serial.print(atLast); }
        return hit;
      }
    }
    delay(2);
  }
  if (show) {
    Serial.print(F("<< (het gio sau ")); Serial.print(toMs); Serial.print(F(" ms) "));
    Serial.println(atLast.length() ? atLast : String(F("[khong nhan duoc gi]")));
  }
  return false;
}

// Gui mot lenh roi doc phan hoi. Toan bo phan hoi (da bo echo) nam o atLast.
static bool atCmd(const char* cmd, const char* want = "OK", uint32_t toMs = 3000, bool show = true) {
  atFlush();
  atLast = "";
  if (show) { Serial.print(F(">> ")); Serial.println(cmd); }
  M21.print(cmd);
  M21.print("\r\n");
  return atWait(cmd, want, toMs, show);
}

// Tat echo + bat bao loi bang chu. Phai goi o DAU MOI BAI TEST, khong chi
// mot lan trong T1: hai thiet lap nay khong luu vao bo nho, tat bat nguon
// module mot cai la echo bat lai. Chay 'i' xong roi 's' thi khong sao, nhung
// tat nguon de lap SIM roi chay thang 's' thi echo van con.
static void atPrep() {
  atCmd("ATE0", "OK", 1500, false);
  atCmd("AT+CMEE=2", "OK", 1500, false);
}

// Do nhung gi module tu dong bao len (RING, +CMT, +CLIP, NO CARRIER...) ra
// Serial Monitor. Goi lien tuc trong loop() de khong bo lo su kien.
static void atPump() {
  static String line;
  while (M21.available()) {
    char c = M21.read();
    if (c == '\n') {
      line.trim();
      if (line.length()) { Serial.print(F("[URC] ")); Serial.println(line); }
      line = "";
    } else if (c != '\r') {
      line += c;
      if (line.length() > 400) line = "";   // chong tran neu gap rac
    }
  }
}

static const uint32_t BAUD_CANDS[] = { M21_BAUD, 9600, 115200, 19200, 38400,
                                       57600, 4800, 230400, 460800 };
static const uint8_t  BAUD_N = sizeof(BAUD_CANDS) / sizeof(BAUD_CANDS[0]);

static void openAt(uint32_t baud) {
  M21.end();
  delay(30);
  M21.begin(baud, SERIAL_8N1, M21_RX_PIN, M21_TX_PIN);
  delay(150);
}

// Chuoi thoat che do du lieu, gui o baud dang mo.
//
// 🔴 "+++" phai gui TRAN: khong CR, khong LF, va phai im lang >1 giay ca
// truoc lan sau. Go "+++" kem Enter qua che do cau noi 'b' se KHONG thoat
// duoc, vi CRLF di kem pha vo dieu kien im lang.
static void escapeDataMode() {
  atFlush();
  delay(1200);
  M21.print("+++");     // khong xuong dong
  delay(1200);
  atFlush();
  atCmd("ATH", "OK", 3000, false);   // cup not cuoc goi du lieu neu con
}

static uint32_t scanBaudOnce() {
  for (uint8_t i = 0; i < BAUD_N; i++) {
    if (i && BAUD_CANDS[i] == BAUD_CANDS[0]) continue;   // khoi thu lai muc dau
    Serial.print(F("  thu baud ")); Serial.print(BAUD_CANDS[i]); Serial.print(F(" ... "));
    openAt(BAUD_CANDS[i]);
    for (uint8_t k = 0; k < 3; k++) {
      if (atCmd("AT", "OK", 1000, false)) {
        Serial.println(F("OK"));
        atBaud = BAUD_CANDS[i];
        return BAUD_CANDS[i];
      }
    }
    Serial.println(F("khong"));
  }
  return 0;
}

// Doi module tra loi "AT" o nhieu muc baud. Tra ve baud tim duoc, 0 neu thua.
//
// 🔴 KHONG goi AT+IPR de ep baud ve mot con so co dinh. AT+IPR ghi vao bo nho
// module va giu qua ca lan tat nguon — mot lan go nham la lan sau khong ai
// biet vi sao module cam. Tim duoc baud nao thi dung baud do.
//
// 🔴 Quet TRUOT HET moi muc baud KHONG co nghia la dut day. Sau khi chay
// sketch PPP, module con nam trong CHE DO DU LIEU: no coi moi byte gui xuong
// la du lieu chu khong phai lenh, nen im lang o ca chin muc baud — nhin y
// het hong phan cung. Vi vay quet truot mot luot thi phai thu thoat che do
// du lieu roi quet lai, truoc khi bao loi. Bat nguoi dung tu nho bam phim
// thoat la thiet ke toi: luc do ho dang tin la day hong.
static uint32_t atAutoBaud() {
  uint32_t b = scanBaudOnce();
  if (b) return b;

  Serial.println(F("  Khong muc baud nao tra loi."));
  Serial.println(F("  Module co the dang o CHE DO DU LIEU (sau khi chay sketch PPP)."));
  Serial.println(F("  Dang thu chuoi thoat +++ ..."));
  for (uint8_t i = 0; i < 3 && i < BAUD_N; i++) {
    openAt(BAUD_CANDS[i]);
    escapeDataMode();
  }

  b = scanBaudOnce();
  if (b) {
    Serial.println(F("  --> Dung vay: da thoat che do du lieu, module tra loi lai."));
    return b;
  }

  // Khong tim thay: quay ve muc mac dinh de cac lenh sau con co cua
  openAt(M21_BAUD);
  return 0;
}

// Doc mot dong tu Serial Monitor. Chan cho toi khi co Enter hoac het gio.
// 🔴 Chan nhu the nay CHI chap nhan duoc trong sketch test doc lap. Trong
// firmware chinh, vong cho ky tu Serial da tung khoa luon task audio va lam
// nut bam mat tac dung — nhin ngoai giong het thiet bi hong.
static String askLine(const char* prompt, uint32_t toMs = 30000) {
  Serial.print(prompt);
  while (Serial.available()) Serial.read();
  String s;
  uint32_t t0 = millis();
  while (millis() - t0 < toMs) {
    atPump();                       // van hien URC trong luc dang go
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        if (s.length()) { Serial.println(s); return s; }
      } else {
        s += c;
        t0 = millis();
      }
    }
    delay(5);
  }
  Serial.println(F("(het gio, bo qua)"));
  return "";
}

// Trich so nguyen ngay sau tien to, vi du atFindInt("+CSQ: 21,99", "+CSQ:").
static int atFindInt(const String& src, const char* tag, int notFound = -1) {
  int i = src.indexOf(tag);
  if (i < 0) return notFound;
  i += strlen(tag);
  while (i < (int)src.length() && src[i] == ' ') i++;
  int v = 0; bool any = false;
  while (i < (int)src.length() && isdigit((unsigned char)src[i])) { v = v * 10 + (src[i] - '0'); i++; any = true; }
  return any ? v : notFound;
}

// ============================================================================
// Cac bai test
// ============================================================================

// --- T1: module co song khong, no la con gi -------------------------------
static bool testIdentify() {
  Serial.println(F("\n===== T1 — BAT TAY UART & NHAN DANG ====="));
  uint32_t baud = atAutoBaud();
  if (!baud) {
    Serial.println(F("KHONG BAT TAY DUOC. Theo thu tu kha nang:"));
    Serial.println(F("  1. TX/RX nguoc — TX cua ben nay phai vao RX cua ben kia"));
    Serial.println(F("  2. Chua chung GND giua ESP32 va module"));
    Serial.println(F("  3. Nguon sai — khoi SIM chi an 3.7~4.0V. Cap 5V THANG vao"));
    Serial.println(F("     khoi SIM la hong module. Phai qua khoi cap nguon di kem."));
    Serial.println(F("  4. Den NET tren module khong sang/khong nhap nhay -> module chua chay"));
    return false;
  }
  Serial.print(F("Bat tay OK o baud ")); Serial.println(baud);

  atPrep();                      // tat echo + bao loi bang chu
  atCmd("ATI", "OK", 3000);      // ten model
  atCmd("AT+CGMR", "OK", 3000);  // phien ban firmware
  atCmd("AT+GSN", "OK", 3000);   // IMEI
  Serial.println(F("--> Doi chieu dong ATI voi ten in tren con chip. Ho A76XX"));
  Serial.println(F("    (SIM7680C / A7680C) dung chung bo lenh, ten khac chut"));
  Serial.println(F("    khong sao; khac han thi phai tra lai datasheet dung ten."));
  return true;
}

// --- T2: SIM ---------------------------------------------------------------
static bool testSim() {
  Serial.println(F("\n===== T2 — THE SIM ====="));
  atPrep();
  bool ok = atCmd("AT+CPIN?", "READY", 8000);
  if (!ok) {
    Serial.println(F("SIM CHUA SAN SANG:"));
    Serial.println(F("  - 'SIM not inserted': lap sai chieu, hoac dung SIM khong phai Nano"));
    Serial.println(F("  - 'SIM PIN': SIM con khoa PIN. Cho vao dien thoai tat PIN roi lap lai"));
    Serial.println(F("  - khong tra loi gi: nguon tut khi module doc SIM"));
    return false;
  }
  atCmd("AT+CICCID", "OK", 3000);   // so seri SIM
  if (atLast.indexOf("ERROR") >= 0) atCmd("AT+CCID", "OK", 3000);
  return true;
}

// --- T3: song va dang ky mang ---------------------------------------------
// Doc stat cua +CEREG / +CREG: truong thu HAI trong dong "+CxREG: <n>,<stat>".
// Tra -1 neu khong doc duoc.
static int regStat(const char* cmd, const char* tag) {
  atCmd(cmd, tag, 3000, false);
  int p = atLast.indexOf(tag);
  if (p < 0) return -1;
  p = atLast.indexOf(',', p);
  if (p < 0) return -1;
  p++;
  while (p < (int)atLast.length() && atLast[p] == ' ') p++;
  if (p >= (int)atLast.length() || !isdigit((unsigned char)atLast[p])) return -1;
  return atLast[p] - '0';
}

static bool testNetwork() {
  Serial.println(F("\n===== T3 — SONG & DANG KY MANG ====="));
  atPrep();

  // --- Radio co dang BAT khong -------------------------------------------
  // 🔴 Phai hoi truoc moi thu khac. CFUN=0 (toi thieu) hoac CFUN=4 (may bay)
  // thi module KHONG quet mang chut nao: CSQ tra 99 va CEREG tra stat=0. Nhin
  // giong het "song yeu" nhung khong phai — song yeu la stat=2 (dang tim).
  // Doi anten hay doi cho ngoi deu vo ich khi radio dang tat.
  atCmd("AT+CFUN?", "+CFUN:", 5000);
  int cfun = atFindInt(atLast, "+CFUN:");
  if (cfun != 1) {
    Serial.print(F("  CFUN = ")); Serial.print(cfun);
    Serial.println(F(" -> radio dang TAT. Dang bat len (AT+CFUN=1)..."));
    atCmd("AT+CFUN=1", "OK", 15000);
    delay(5000);                       // radio can vai giay moi bat dau quet
  }

  // --- Muc song ------------------------------------------------------------
  atCmd("AT+CSQ", "+CSQ:", 3000);
  int rssi = atFindInt(atLast, "+CSQ:");
  if (rssi < 0) {
    Serial.println(F("  Khong doc duoc dong +CSQ (module khong tra loi dung dinh dang)."));
  } else if (rssi == 99) {
    Serial.println(F("  RSSI = 99 — module KHONG do duoc muc song."));
    Serial.println(F("    Thuong la: anten chua cam / chua van chat vao chan ATN,"));
    Serial.println(F("    hoac radio vua bat chua kip quet (doi 10s roi bam 's' lai)."));
  } else {
    Serial.print(F("  RSSI = ")); Serial.print(rssi);
    Serial.print(F("  (~")); Serial.print(-113 + 2 * rssi); Serial.println(F(" dBm)"));
    if (rssi < 8)       Serial.println(F("  --> YEU. Anten da van chat chua? Dua ra gan cua so."));
    else if (rssi < 15) Serial.println(F("  --> Tam duoc, nen cai thien truoc khi test cuoc goi."));
    else                Serial.println(F("  --> Tot."));
  }

  // --- Cho dang ky ---------------------------------------------------------
  // CEREG = dang ky LTE (cai duy nhat quan trong o module Cat-1 nay).
  // CREG  = dang ky thoai doi cu; in kem de doi chieu khi CEREG kho hieu.
  bool reg = false;
  int  st  = -1;
  Serial.println(F("  Cho dang ky mang (toi 60 giay)..."));
  for (int i = 0; i < 30 && !reg; i++) {
    st = regStat("AT+CEREG?", "+CEREG:");
    if (st == 1 || st == 5) { reg = true; break; }
    if (i % 5 == 0) {
      Serial.print(F("    +CEREG stat=")); Serial.print(st);
      Serial.print(F("   +CREG stat=")); Serial.println(regStat("AT+CREG?", "+CREG:"));
    }
    delay(2000);
  }

  if (!reg) {
    Serial.println(F("  CHUA DANG KY DUOC MANG. Y nghia ma stat:"));
    Serial.println(F("    0 = KHONG dang ky va KHONG tim  <- radio tat, anten hong, SIM chua nhan"));
    Serial.println(F("    2 = dang tim                    <- song yeu / dang cho, cho them"));
    Serial.println(F("    3 = BI TU CHOI                  <- SIM het han/chua kich hoat/khoa mang"));
    Serial.println(F("    4 = khong ro"));
    if (st == 0) {
      // stat=0 la trang thai "nam im", khong phai "co gang ma khong duoc".
      // Ep quet lai bang tay truoc khi ket luan phan cung.
      Serial.println(F("  stat=0: thu ep quet lai bang tay..."));
      atCmd("AT+CFUN=1", "OK", 15000);
      atCmd("AT+CNMP=2", "OK", 5000);    // 2 = tu chon he mang (LTE/GSM)
      atCmd("AT+COPS=0", "OK", 60000);   // 0 = tu chon nha mang. Cham, kien nhan
      Serial.println(F("  Xong. Doi 20 giay roi bam 's' de do lai."));
      Serial.println(F("  Van stat=0 -> kiem tra ANTEN truoc tien (chan ATN),"));
      Serial.println(F("  sau do den SIM da kich hoat 4G chua."));
    }
    atCmd("AT+COPS=?", "OK", 120000);    // quet danh sach nha mang xung quanh
    Serial.println(F("  Dong tren la cac nha mang module NHIN THAY. Rong khong"));
    Serial.println(F("  = van de o anten hoac song. Co ten ma van khong vao duoc"));
    Serial.println(F("  = van de o SIM."));
    return false;
  }

  Serial.println(F("  Da dang ky mang."));
  atCmd("AT+COPS?", "OK", 5000);      // ten nha mang + he mang
  atCmd("AT+CPSI?", "OK", 5000);      // chi tiet: band LTE, muc thu
  return true;
}

// --- T3b: IMS / VoLTE ------------------------------------------------------
// 🔴 Buoc nay khong co o cac module 2G doi truoc, va la ly do pho bien nhat
// khien "mang OK, SMS OK, nhung goi la rot". Module KHONG co duong lui ve 2G:
// Viet Nam da tat song 2G, nen cuoc goi bat buoc di qua VoLTE. SIM khong bat
// dich vu VoLTE (HD Voice) thi khong bao gio goi duoc, dung bao nhieu code.
static void testVolte() {
  Serial.println(F("\n===== T3b — IMS / VoLTE ====="));
  atPrep();
  Serial.println(F("  (khong dat khong co nghia la hong — mot so ban firmware"));
  Serial.println(F("   khong ho tro hai lenh nay; luc do dua vao ket qua T6.)"));
  atCmd("AT+CIREG?",  "OK", 5000);   // 3GPP: dang ky IMS. reg_info 1 = co thoai
  atCmd("AT+CAVIMS?", "OK", 5000);   // SIMCom: thoai qua IMS co san khong
  Serial.println(F("  Muon con so 1 o hai dong tren. Neu la 0:"));
  Serial.println(F("    -> goi tong dai nha mang bat VoLTE / HD Voice cho SIM nay,"));
  Serial.println(F("       hoac lap SIM vao dien thoai co VoLTE de kich hoat lan dau."));
}

// --- T4: gui SMS -----------------------------------------------------------
static void testSmsSend() {
  Serial.println(F("\n===== T4 — GUI SMS ====="));
  atPrep();
  String num = askLine("So dien thoai nhan (dang +84...): ");
  if (!num.length()) return;
  String msg = askLine("Noi dung (KHONG DAU, chi ASCII): ");
  if (!msg.length()) msg = "VisionCare 4G test";

  atCmd("AT+CMGF=1");                 // che do text
  atCmd("AT+CSCS=\"GSM\"");           // bang ma ASCII

  String cmd = "AT+CMGS=\"" + num + "\"";
  atFlush();
  Serial.print(F(">> ")); Serial.println(cmd);
  M21.print(cmd); M21.print("\r\n");

  // Phai doi dau nhac '>' truoc khi go noi dung, khong duoc gui thang.
  bool prompt = false;
  uint32_t t0 = millis();
  while (millis() - t0 < 5000 && !prompt) {
    while (M21.available()) if (M21.read() == '>') { prompt = true; break; }
    delay(2);
  }
  if (!prompt) {
    Serial.println(F("  Khong thay dau nhac '>'. So dien thoai sai dinh dang?"));
    M21.write(27);                    // ESC huy lenh dang do dang
    return;
  }
  M21.print(msg);
  M21.write(26);                      // Ctrl+Z = ket thuc va gui

  Serial.println(F("  Dang gui, cho toi 60 giay..."));
  atLast = "";
  if (atWait("", "+CMGS", 60000, true)) Serial.println(F("  DA GUI. Kiem tra may nhan."));
  else Serial.println(F("  GUI THAT BAI. SIM con tien khong? Song co du khong?"));
}

// --- T5: nhan SMS ----------------------------------------------------------
static void testSmsReceive() {
  Serial.println(F("\n===== T5 — NHAN SMS ====="));
  atPrep();
  atCmd("AT+CMGF=1");
  // CNMI=2,2 = day thang noi dung tin nhan ra UART duoi dang +CMT, khong luu
  // vao bo nho SIM. De =1 thi chi bao co tin moi, phai tu doc bang AT+CMGR.
  atCmd("AT+CNMI=2,2,0,0,0");
  Serial.println(F("  Da bat. Nhan tin vao SIM nay tu dien thoai khac,"));
  Serial.println(F("  dong '+CMT:' se hien o [URC] ben duoi trong vai giay."));
}

// --- T6: goi di ------------------------------------------------------------
static void testCallOut() {
  Serial.println(F("\n===== T6 — GOI DI ====="));
  atPrep();
  Serial.println(F("  Buoc nang nhat ve dien: luc bat song phat, module rut dong"));
  Serial.println(F("  dot ngot (dinh ~1A). ESP32 reset o day = NGUON YEU, khong"));
  Serial.println(F("  phai loi code."));
  String num = askLine("So dien thoai goi (dang +84...): ");
  if (!num.length()) return;

  atCmd("AT+CLIP=1");                 // hien so trong cac su kien cuoc goi
  String dial = "ATD" + num + ";";    // 🔴 dau ';' = cuoc goi THOAI. Thieu no
  Serial.print(F(">> ")); Serial.println(dial);   // la cuoc goi DATA, may kia
  atFlush();                                      // khong bao gio do chuong.
  M21.print(dial); M21.print("\r\n");

  Serial.println(F("  Theo doi 30 giay. Go 'h' bat cu luc nao de ngat may."));
  uint32_t t0 = millis(), tPoll = 0;
  while (millis() - t0 < 30000) {
    atPump();
    if (Serial.available() && Serial.read() == 'h') break;
    // AT+CLCC: 0=dang noi · 2=dang quay so · 3=may kia dang do chuong
    if (millis() - tPoll > 5000) { tPoll = millis(); atCmd("AT+CLCC", "OK", 1500); }
    delay(10);
  }
  atCmd("ATH", "OK", 5000);
  Serial.println(F("  Da ngat may."));
  Serial.println(F("  ✔ May kia do chuong = phan bao hieu chay dung, dat yeu cau T6."));
  Serial.println(F("  Chua nghe duoc tieng la BINH THUONG neu chua han mic/loa vao"));
  Serial.println(F("  chan MIC+/- va SP+/- cua module. Xem muc 6 cua tai lieu."));
  Serial.println(F("  'NO CARRIER' ngay lap tuc, trong khi T3b bao IMS = 0"));
  Serial.println(F("  --> SIM chua bat VoLTE, khong phai loi phan cung."));
}

// --- T7: nghe cuoc goi den -------------------------------------------------
static void testCallIn() {
  Serial.println(F("\n===== T7 — NGHE CUOC GOI DEN ====="));
  atPrep();
  atCmd("AT+CLIP=1");
  Serial.println(F("  Goi vao so SIM nay. Se thay 'RING' va '+CLIP:' o [URC]."));
  Serial.println(F("  Go 'a' de nhan may, 'h' de tu choi."));
}

// --- T8: nguon -------------------------------------------------------------
static void testPower() {
  Serial.println(F("\n===== T8 — DIEN AP NGUON MODULE ====="));
  atPrep();
  // Khoi SIM an 3.7~4.0V. Tut xuong duoi ~3.4V la module reset giua chung.
  // Chay lenh nay NGAY TRONG luc dang goi (T6) moi thay duoc muc sut that.
  atCmd("AT+CBC", "OK", 3000);
  Serial.println(F("  Doc so cuoi dong +CBC (mV hoac V tuy ban firmware)."));
  Serial.println(F("  Dai lam viec: 3.7~4.0V. Duoi 3.5V la vung nguy hiem."));
  Serial.println(F("  Muon do luc tai nang: chay T6, trong 30 giay theo doi go 'v'."));
}

// --- T9: am luong / do nhay mic --------------------------------------------
// Chi co y nghia SAU khi da han mic + loa vao MIC+/- va SP+/-.
static void testAudioLevels() {
  Serial.println(F("\n===== T9 — AM LUONG LOA & DO NHAY MIC ====="));
  atPrep();
  Serial.println(F("  Chi co tac dung khi da han mic/loa vao chan MIC+/- va SP+/-."));
  // 🔴 KHONG dung AT+CLVL / AT+CMIC. Do la lenh cua doi module cu (SIM800...);
  // firmware A7680C-LANS V11.0.01 tra ERROR cho ca hai. Bon lenh duoi day la
  // ten that, da do bang AT+<lenh>=? tren chinh board nay.
  atCmd("AT+COUTGAIN?", "OK", 3000);   // muc ra loa      (0-7)
  atCmd("AT+CMICGAIN?", "OK", 3000);   // do khuech dai mic (0-7)
  atCmd("AT+CRSL?",     "OK", 3000);   // am luong chuong  (0-100)
  atCmd("AT+CSDVC?",    "OK", 3000);   // duong ra: 1=handset, 3=loa ngoai
  Serial.println(F("  Doi gia tri bang che do cau noi 'b':"));
  Serial.println(F("    AT+COUTGAIN=5   muc ra loa      (0-7)"));
  Serial.println(F("    AT+CMICGAIN=5   khuech dai mic  (0-7)"));
  Serial.println(F("    AT+CRSL=80      am luong chuong (0-100)"));
  Serial.println(F("    AT+CSDVC=3      chuyen sang loa ngoai"));
  Serial.println(F("  Chinh TRONG LUC dang goi thi nghe duoc thay doi ngay."));
  Serial.println(F("  Ghi chu: AT+CPCMREG tra ERROR -> firmware nay KHONG mo"));
  Serial.println(F("  giao tiep PCM so. Am thanh cuoc goi chi di duong analog."));
}

// --- T10: duong DATA 4G ----------------------------------------------------
// 🔴 Module KHONG phat WiFi. No khong phai router, khong co ai "bat duoc song"
// cua no ca. Duong internet duy nhat no cho la: ESP32 go lenh AT qua UART,
// module di ra mang thay, roi tra du lieu ve cung tren hai soi day do. Nut co
// chai vi vay la BAUD UART chu khong phai toc do 4G — bai test nay do dung
// con so that de biet co du cho pipeline VisionCare hay khong.
static void testData() {
  Serial.println(F("\n===== T10 — DUONG DATA 4G ====="));
  atPrep();

  atCmd("AT+CGATT?", "+CGATT:", 10000);
  if (atFindInt(atLast, "+CGATT:") != 1) {
    Serial.println(F("  Chua gan vao mien goi tin, dang gan..."));
    atCmd("AT+CGATT=1", "OK", 30000);
  }
  atCmd("AT+CGDCONT?", "OK", 5000);      // APN dang dung

  // NETOPEN bat ngan xep TCP/IP ben trong module.
  atCmd("AT+NETOPEN", "OK", 30000);
  atWait("", "+NETOPEN:", 20000, true);
  atCmd("AT+IPADDR", "OK", 10000);       // co IP la ra duoc internet

  // Tai mot trang nho, do thoi gian de biet thong luong THUC TE qua UART.
  atCmd("AT+HTTPINIT", "OK", 10000);
  atCmd("AT+HTTPPARA=\"URL\",\"http://example.com/\"", "OK", 10000);
  uint32_t t0 = millis();
  atCmd("AT+HTTPACTION=0", "OK", 10000);
  bool got = atWait("", "+HTTPACTION:", 60000, true);
  uint32_t dt = millis() - t0;

  if (got) {
    Serial.print(F("  Server tra loi sau ")); Serial.print(dt); Serial.println(F(" ms"));
    atCmd("AT+HTTPHEAD", "OK", 15000);

    // Do thong luong THUC TE cua duong UART.
    // 🔴 Phai doi den dong ket thuc "+HTTPREAD: 0", KHONG phai chu "OK".
    // AT+HTTPREAD tra "OK" ngay lap tuc roi mmoi do du lieu ra sau:
    //     OK  /  +HTTPREAD: <n>  /  <n byte du lieu>  /  +HTTPREAD: 0
    // Dung "OK" lam moc thi do trung khoang thoi gian truoc khi truyen, ra
    // con so cao hon ca gioi han vat ly cua baud — da in ra 1326 B/s trong
    // khi 9600 baud toi da chi 960 B/s. So nhanh hon muc kha di = do sai.
    const uint32_t N = 512;
    uint32_t t1 = millis();
    bool done = atCmd("AT+HTTPREAD=0,512", "+HTTPREAD: 0", 30000);
    uint32_t d2 = millis() - t1;
    if (done && d2) {
      Serial.print(F("  Doc ")); Serial.print(N);
      Serial.print(F(" byte mat ")); Serial.print(d2);
      Serial.print(F(" ms -> ~")); Serial.print(N * 1000UL / d2);
      Serial.println(F(" byte/giay"));
    }
    Serial.print(F("  Tran ly thuyet cua UART o ")); Serial.print(atBaud);
    Serial.print(F(" baud (8N1) = ")); Serial.print(atBaud / 10);
    Serial.println(F(" byte/giay"));
    Serial.println(F("  Doi chieu nhu cau: WAV 48kHz 16-bit mono = 96000 byte/giay,"));
    Serial.println(F("  WAV 16kHz = 32000, MP3 64kbps = 8000 byte/giay."));
  } else {
    Serial.println(F("  Khong co +HTTPACTION -> chua ra duoc internet."));
    Serial.println(F("  Thuong la APN. Dat tay bang che do cau noi 'b':"));
    Serial.println(F("    AT+CGDCONT=1,\"IP\",\"v-internet\"   (Vinaphone)"));
    Serial.println(F("    AT+CGDCONT=1,\"IP\",\"m-wap\"        (Viettel)"));
    Serial.println(F("    AT+CGDCONT=1,\"IP\",\"m3-world\"     (MobiFone)"));
  }
  atCmd("AT+HTTPTERM", "OK", 10000);
  atCmd("AT+NETCLOSE", "OK", 20000);
}

// --- Thoat che do data ------------------------------------------------------
// Sau ATD*99# (hoac sau khi chay sketch PPP) module vao che do truyen trong
// suot: moi byte gui xuong deu bi coi la du lieu, khong con lenh AT nao chay
// nua. Loi ra la chuoi escape.
//
// Bai T1 da tu goi buoc nay khi quet truot het baud, nen thuong khong phai
// bam phim nay bang tay. Giu lai de dung khi module kep giua chung.
static void modemEscape() {
  Serial.println(F("\n===== THOAT CHE DO DATA (+++) ====="));
  escapeDataMode();
  if (atCmd("AT", "OK", 3000)) {
    Serial.println(F("  Da ve che do lenh."));
  } else {
    Serial.println(F("  Chua ve duoc. Thu lai lan nua, hoac tat bat nguon module"));
    Serial.println(F("  (cach chac an nhat, module doc lai tu dau)."));
  }
}

// --- Dat APN va ep PDP ve IPv4 ---------------------------------------------
// Mang cap san context kieu "IPV4V6". Khi ESP32 dung lwIP qua PPP, ben kia
// chi thuong luong IPv4 (IPCP) — context lai hai stack de lam IPCP that bai
// va PPP len duoc LCP nhung khong bao gio co IP. Ep ve "IP" (IPv4 thuan).
static void setApnIPv4() {
  Serial.println(F("\n===== DAT APN + EP PDP VE IPv4 ====="));
  atPrep();
  atCmd("AT+CGDCONT?", "OK", 5000);
  String apn = askLine("APN (Enter = m-wap cua MobiFone): ", 20000);
  if (!apn.length()) apn = "m-wap";
  String cmd = "AT+CGDCONT=1,\"IP\",\"" + apn + "\"";
  atCmd(cmd.c_str(), "OK", 10000);
  atCmd("AT+CGDCONT?", "OK", 5000);
  Serial.println(F("  Xong. Nap sketch Test_MKE_M21_PPP roi chay lai."));
}

// --- T0: cau noi trong suot ------------------------------------------------
static void bridgeMode() {
  Serial.println(F("\n===== T0 — CAU NOI TRONG SUOT ====="));
  Serial.println(F("  Go thang lenh AT, Enter de gui. Go '!!!' roi Enter de thoat."));
  String line;
  while (true) {
    while (M21.available()) Serial.write(M21.read());
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        if (line == "!!!") { Serial.println(F("\n  Thoat cau noi.")); return; }
        M21.print(line); M21.print("\r\n");
        Serial.print(F(">> ")); Serial.println(line);
        line = "";
      } else line += c;
    }
    delay(2);
  }
}

// ============================================================================
static void printMenu() {
  Serial.println(F("\n--------------------------------------------------"));
  Serial.println(F(" 1  chay het T1 -> T2 -> T3 -> T3b"));
  Serial.println(F(" i  T1  nhan dang module"));
  Serial.println(F(" s  T2+T3  SIM, song, dang ky mang"));
  Serial.println(F(" e  T3b VoLTE/IMS  (khong co la khong goi duoc)"));
  Serial.println(F(" m  T4  gui SMS"));
  Serial.println(F(" r  T5  bat nhan SMS truc tiep"));
  Serial.println(F(" c  T6  goi di"));
  Serial.println(F(" n  T7  cho cuoc goi den"));
  Serial.println(F(" a  nhan may   ·  h  ngat may"));
  Serial.println(F(" v  T8  do dien ap nguon module"));
  Serial.println(F(" l  T9  am luong loa / do nhay mic"));
  Serial.println(F(" d  T10 duong data 4G + do thong luong"));
  Serial.println(F(" x  thoat che do data (+++) khi module kep cung"));
  Serial.println(F(" p  dat APN + ep PDP ve IPv4 (chuan bi cho PPP)"));
  Serial.println(F(" b  T0  cau noi trong suot (go AT tay)"));
  Serial.println(F(" ?  hien lai menu nay"));
  Serial.println(F("--------------------------------------------------"));
}

void setup() {
  Serial.begin(115200);
  delay(600);
  Serial.println(F("\n\n===== Test_MKE_M21 — unit test module 4G ====="));
  Serial.print(F("UART1: TX=GPIO")); Serial.print(M21_TX_PIN);
  Serial.print(F(" -> RX module   |   RX=GPIO")); Serial.print(M21_RX_PIN);
  Serial.println(F(" <- TX module"));
  Serial.println(F("NGUON: khoi SIM chi an 3.7~4.0V — phai qua khoi cap nguon di kem."));

  if (M21_RST_PIN >= 0) {
    pinMode(M21_RST_PIN, OUTPUT);
    digitalWrite(M21_RST_PIN, !M21_RST_ON);
    delay(100);
    Serial.println(F("Nhan RST 200ms..."));
    digitalWrite(M21_RST_PIN, M21_RST_ON);
    delay(200);
    digitalWrite(M21_RST_PIN, !M21_RST_ON);
  }

  M21.begin(M21_BAUD, SERIAL_8N1, M21_RX_PIN, M21_TX_PIN);

  // Module can ~10-15 giay tu luc co nguon toi khi nhan lenh AT. Vua nap xong
  // ma go lenh ngay se tuong module hong.
  Serial.println(F("Cho module khoi dong 15 giay..."));
  for (int i = 15; i > 0; i--) { atPump(); Serial.print(i); Serial.print(' '); delay(1000); }
  Serial.println();

  printMenu();
  Serial.println(F("Go mot phim roi Enter."));
}

void loop() {
  atPump();     // luon hien URC: RING, +CMT, NO CARRIER, +CLIP...

  if (!Serial.available()) { delay(5); return; }
  char k = Serial.read();
  while (Serial.available() && (Serial.peek() == '\n' || Serial.peek() == '\r')) Serial.read();

  switch (k) {
    case '1': if (testIdentify() && testSim() && testNetwork()) testVolte(); break;
    case 'i': testIdentify();   break;
    case 's': if (testSim()) testNetwork(); break;
    case 'e': testVolte();      break;
    case 'm': testSmsSend();    break;
    case 'r': testSmsReceive(); break;
    case 'c': testCallOut();    break;
    case 'n': testCallIn();     break;
    case 'a': atCmd("ATA", "OK", 5000); break;
    case 'h': atCmd("ATH", "OK", 5000); break;
    case 'v': testPower();      break;
    case 'l': testAudioLevels(); break;
    case 'd': testData();       break;
    case 'x': modemEscape();    break;
    case 'p': setApnIPv4();     break;
    case 'b': bridgeMode();     break;
    case '?': printMenu();      break;
    case '\n': case '\r': case ' ': break;
    default:
      Serial.print(F("Khong biet phim '")); Serial.print(k); Serial.println(F("'. Go ? de xem menu."));
  }
}
