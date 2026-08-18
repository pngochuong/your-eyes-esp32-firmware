#include "audio_player.h"

#include <math.h>
#include <WiFi.h>

#include "audio_buffers.h"
#include "audio_i2s.h"
#include "../../app_config.h"

// ------------------------------------------------------------ chinh loa re
// Ba nut de van khi loa re. Chinh o day, khong phai trong vong phat ben duoi.
//
// PLAY_GAIN_PCT: bien do phat, tinh theo % goc.
//   Khong co tu loc nguon thi day la nut quan trong nhat. Bien do to =
//   dong dien dinh lon = rail 5 V sut = ca chip lan amp cung u u. Ha xuong
//   70% cat dinh dong gan mot nua ma tai nghe chi nho di chut.
//   Con re: ha tiep 60 → 50. Het re nhung nho qua: nang len 85.
//
// 🔴 Da ha 70 → 55 (2026-08-18): o 70 nguoi dung van bao "hoi re" khi phat cau
// tra loi tu server. Nguyen nhan khong phai cat tran so — do that mot lan
// phat: dinh 14889/32767, so mau cham tran = 0. Tuc la meo nam o phan ANALOG,
// tu dinh dong lam sut rail 5 V, dung nhu doan o tren. Cat tran so thi ha bao
// nhieu cung vo ich, con ha dinh dong thi an ngay.
#define PLAY_GAIN_PCT   55

// Vuot bien do len/xuong o dau va cuoi cau noi. Khong co doan nay thi
// mau dau tien nhay tu 0 len bien do that trong 1/16000 giay — mang loa
// giat mot cai, tai nghe thanh tieng "tach".
#define FADE_MS         8

// Bom im lang truoc va sau khi phat. DMA cua I2S van quay khi het du lieu;
// khong bom thi no lap lai mieng dem cu, ra tieng u hoac lap bap cuoi cau.
#define FLUSH_MS        40

// Tran file am tra ve. Nap tron vao PSRAM roi moi phat.
//
// Da thu phat theo luong (vua nhan vua phat) va BO. Ly do: server khong nha
// deu — co lan ca 198 KB ve trong 10 giay gay 4 lan hut, co lan chi ve 3.5 KB
// roi dung han 160 giay. Phat theo luong chi hon khi nguon day deu; khong deu
// thi no bien mot cau lien mach thanh mot chuoi tieng giat. Nap tron cho do
// tre cao hon nhung KHONG BAO GIO giat, va do la thu nguoi dung nghe thay.
static const size_t MAX_REPLY = 2 * 1024 * 1024;  // 2 MB
static const size_t MIN_REPLY = 512 * 1024;       // muc lui khi PSRAM khong du

// ------------------------------------------------------- phat theo luong
// Server tra am thanh dan chu khong doi xong ca file. Phat dan thi nghe duoc
// som hon nhieu, nhung doi lai: mang nghen mot nhip la loa hut tieng.
//
// Hai yeu cau nay keo nguoc chieu nhau:
//   dem NHIEU  -> khong hut, nhung tieng ra cham
//   dem IT     -> tieng ra nhanh, nhung mang nghen mot nhip la dut
// Nen KHONG chon mot con so co dinh, ma chia lam ba:
//
//   PREBUF_MS   — dem lan dau, quyet dinh DO TRE. De nho.
//   REFILL_*    — dem nap lai sau khi hut. TU LON DAN moi lan hut, nen mang
//                 tot thi khong ai phai tra gia, mang xau thi no tu chuyen
//                 sang che do an toan sau dung mot lan giat.
//   DANGER_MS   — muc bao dong. Con it hon the la vuot bien do XUONG truoc
//                 khi het sach, roi vuot LEN khi co lai.
//
// Cho cuoi cung moi la mau chot: thu lam nguoi ta NGHE THAY khoang dut khong
// phai la su im lang, ma la cai GAY DOT NGOT o hai dau no. Vuot muot hai dau
// thi tai nhan ra day la mot quang nghi, khong phai loi.
#define PREBUF_MS        400    // muc toi thieu de bat dau do

// 🔴 Phai DO DU LAU roi moi quyet dinh nguong. Da tung khong co con so nay:
// bo giai ma MP3 xa mot cum ngay khi co du lieu nen, cham nguong sau 39 ms,
// va uoc luong toc do o thoi diem do la vo nghia — no chon nguong nho nhat
// roi hut ngay sau do.
//
// WARMUP bo qua cum xa dau tien; do tu sau moc do toi MEASURE moi ra toc do
// on dinh that cua nguon.
#define WARMUP_MS        300
#define MEASURE_MS       900
#define REFILL_START_MS  800    // nap lai sau lan hut dau tien
#define REFILL_STEP_MS   400    // moi lan hut tiep lai cong them
#define REFILL_MAX_MS   2500    // tran
#define DANGER_MS         40    // duoi muc nay thi bat dau vuot xuong

// Moi lan day sang I2S bao nhieu byte. Nho thi vong lap quay lai doc socket
// thuong xuyen hon (it hut hon). 1024 byte = 32 ms.
#define IO_CHUNK        1024

// ---------------------------------------------------------------- nan tieng
// Trang thai nan tieng, giu giua cac mieng. Ban nap-tron nhin thay ca cau;
// ban nay khong, nen moi thu phai co trang thai.
static struct {
  float   hpX, hpY;
  size_t  done;
  size_t  fade;
  int32_t peak;
  size_t  clipped;
} scond;

static void scondReset(uint32_t rate) {
  scond.hpX = scond.hpY = 0.0f;
  scond.done = 0;
  scond.fade = (size_t)rate * FADE_MS / 1000;
  if (scond.fade == 0) scond.fade = 1;
  scond.peak = 0;
  scond.clipped = 0;
}

// `left` = so mau CHAC CHAN con lai sau mieng nay, hoac (size_t)-1 khi chua
// biet cau con dai bao nhieu. Chi vuot xuong khi da biet diem ket thuc — day
// la ly do vong lap ben duoi giu lai mot doan cuoi chua xu ly.
static void scondChunk(int16_t *p, size_t n, size_t left) {
  const float A = 0.999f;        // thong cao ~2.5 Hz: chi cat mot chieu

  for (size_t i = 0; i < n; i++) {
    float x = (float)p[i];
    float y = A * (scond.hpY + x - scond.hpX);
    scond.hpX = x;
    scond.hpY = y;

    int32_t a = (int32_t)(x < 0 ? -x : x);
    if (a > scond.peak) scond.peak = a;
    if (a >= 32000) scond.clipped++;

    int32_t v = (int32_t)(y * PLAY_GAIN_PCT / 100.0f);

    size_t pos = scond.done + i;
    if (pos < scond.fade) v = (int32_t)((int64_t)v * pos / scond.fade);

    if (left != (size_t)-1) {
      size_t toEnd = left + (n - 1 - i);
      if (toEnd < scond.fade) v = (int32_t)((int64_t)v * toEnd / scond.fade);
    }

    p[i] = v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v);
  }
  scond.done += n;
}

// Vuot bien do tuyen tinh tu `from` sang `to` trong mot mieng. Dung de dong
// va mo tieng quanh cho hut, thay cho viec cat phang.
static void applyRamp(int16_t *p, size_t n, float from, float to) {
  if (n == 0) return;
  for (size_t i = 0; i < n; i++) {
    float g = from + (to - from) * ((float)i / (float)n);
    int32_t v = (int32_t)(p[i] * g);
    p[i] = v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v);
  }
}

// ============================================================================
// Vua nhan vua phat
// ============================================================================
// Bo dem vong mot nguoi ghi mot nguoi doc, ca hai deu la task nay nen khong
// can khoa. stStart luon la boi so cua frameBytes vi chi bao gio pop tron
// frame — nho vay ep kieu sang int16_t* luon can bang.
bool playPcmStream(PcmSource &src, const AudioFmt &fmt, size_t totalBytes) {
  if (!stageBuf) { Serial.println("Chua co bo dem phat"); return false; }

  const uint32_t rate = fmt.rate;
  const size_t   frameBytes = (size_t)fmt.ch * 2;

  Serial.printf("Phat theo luong: %u Hz, %u kenh, 16-bit\n",
                (unsigned)rate, fmt.ch);

  if (rate != i2sCurRate) {
    Serial.printf("Doi I2S sang %u Hz\n", (unsigned)rate);
    if (!i2sSetSampleRate(rate)) return false;
  }
  scondReset(rate);

  const size_t bytesPerMs = (size_t)rate * frameBytes / 1000;

  size_t gate      = bytesPerMs * PREBUF_MS;        // muc can co de (tai) mo loa
  size_t refillMs  = REFILL_START_MS;               // lon dan sau moi lan hut
  const size_t danger = bytesPerMs * DANGER_MS;
  if (gate > stageCap / 2) gate = stageCap / 2;

  size_t stStart = 0, stUsed = 0;
  float  duck = 1.0f;              // 1 = dang phat binh thuong, 0 = da dong tieng

  // Toc do phat can duoc dam bao. Neu server day cham hon con so nay thi
  // KHONG co cach nao phat lien mach trong luc van dang nhan — bo dem se can,
  // dem to bao nhieu cung chi doi cho dut sang muon hon. Luc do lua chon duy
  // nhat con lai la doi nhan xong roi moi phat.
  const size_t bytesPerSec = (size_t)rate * frameBytes;
  bool decided = false, waitAll = false;

  // Moc de do toc do on dinh: chot lai luong da nhan sau WARMUP_MS, roi tinh
  // toc do tu do tro di. Bo qua cum xa dau tien cua bo giai ma.
  unsigned long warmT = 0, tDecide = 0;
  size_t        warmIn = 0;
  int           lastMode = -1, lastLogged = -1;

  i2s_channel_disable(i2sRx);
#if LOWER_WIFI_TX
  wifi_power_t txSaved = WiFi.getTxPower();
  WiFi.setTxPower(WIFI_POWER_11dBm);
#endif

  bool          playing = false, netEof = false, ioErr = false;
  unsigned long tStart = millis(), tStall = millis(), tBeat = millis(), tFirst = 0;
  size_t        written = 0, totalIn = 0, underruns = 0;

  for (;;) {
    // ---- hut tu mang
    if (!netEof && stUsed < stageCap) {
      size_t wpos = (stStart + stUsed) % stageCap;
      size_t room = stageCap - stUsed;
      if (room > stageCap - wpos) room = stageCap - wpos;   // toi mep vong

      int got = pcmRead(src, stageBuf + wpos, room);
      if (got > 0) {
        stUsed  += got;
        totalIn += got;
        tStall = millis();
      } else if (got < 0) {
        netEof = true;
      } else {
        int budget = tFirst ? BODY_GAP_MS : FIRST_AUDIO_MS;
        if (millis() - tStall > (unsigned long)budget) {
          Serial.printf("Im %d s %s — dung nhan\n", budget / 1000,
                        tFirst ? "giua cau" : "tu luc gui xong");
          netEof = true;
        } else if (millis() - tBeat >= 10000) {
          tBeat = millis();
          Serial.printf("  ... cho tieng: da nhan %u byte sau %lu s\n",
                        (unsigned)totalIn, (millis() - tStart) / 1000);
        }
      }
    }

    // ---- chot moc do sau khi cum xa dau tien di qua
    if (!warmT && millis() - tStart >= WARMUP_MS) {
      warmT  = millis();
      warmIn = totalIn;
    }

    // ---- danh gia lai nguong, LIEN TUC cho toi khi bat dau phat
    //
    // 🔴 Truoc day chot mot lan roi thoi. Sai: nguon day theo CUM roi nghi,
    // nen mot cua so 900 ms trung vao khoang nghi cho ra 0 KB/s, va tu do
    // ket luan sai ve ca doan. Do lien tuc tren toan bo thoi gian da troi
    // qua thi cac cum va cac khoang nghi tu can bang nhau.
    bool measured = warmT && (millis() - warmT) >= MEASURE_MS;
    if (!playing && measured && (millis() - tDecide >= 1000 || netEof)) {
      tDecide = millis();
      unsigned long el = millis() - warmT;
      double r = el ? (double)(totalIn - warmIn) * 1000.0 / (double)el : 0.0;
      double margin = (double)bytesPerSec > 0 ? r / (double)bytesPerSec : 0.0;

      int mode;                       // 0=400ms 1=1500ms 2=tinh chinh xac 3=doi het
      if (margin >= 1.30)             mode = 0;
      else if (margin >= 1.12)        mode = 1;
      else if (totalBytes > 0)        mode = 2;
      else                            mode = 3;

      if (mode != lastMode) {
        lastMode = mode;
        Serial.printf("Nguon %.1f KB/s / can %.1f KB/s = du %.0f%%\n",
                      r / 1024.0, bytesPerSec / 1024.0, margin * 100.0);
      }
      decided = true;

      if (mode == 0) {
        gate = bytesPerMs * 400;
        if (mode != lastLogged) { Serial.println("=> Du rong rai: dem 400 ms."); lastLogged = mode; }
      } else if (mode == 1) {
        gate = bytesPerMs * 1500;
        if (mode != lastLogged) { Serial.println("=> Du vua phai: dem 1500 ms."); lastLogged = mode; }
      } else if (mode == 2) {
        // Biet tong do dai thi tinh duoc CHINH XAC luc nao phat an toan,
        // khong phai doi het.
        //
        // Bo dem voi B byte, nhan them r B/s, phat het P B/s => muc dem tut
        // (P - r) B/s, can duoc B/(P-r) giay. Phat het ca cau mat T/P giay.
        // Khong dut khi  T/P <= B/(P-r)  <=>  B >= T * (1 - r/P).
        double frac = 1.0 - r / (double)bytesPerSec;
        size_t need = (size_t)((double)totalBytes * frac);
        size_t cap  = stageCap - (size_t)IO_CHUNK * 2;
        if (need > cap) need = cap;
        gate = need;
        if (mode != lastLogged) {
          lastLogged = mode;
          Serial.printf("=> Doi den %u/%u byte (%.0f%%) roi phat.\n",
                        (unsigned)need, (unsigned)totalBytes, frac * 100.0);
        }
      } else {
        // Nguon CHAM HON toc do phat va khong biet tong do dai.
        //
        // 🔴 Truoc day cho vao day mot con so dem lon (3000 ms) — SAI han ve
        // nguyen ly. Nguon chay cham hon coi tieu thu thi bo dem tut deu dan,
        // dem to bao nhieu cung chi doi cho dut sang muon hon chu khong bo
        // duoc. Cach duy nhat la doi nhan XONG. Doi lai do tre, nhung mot
        // khoang cho duy nhat de chiu hon tieng noi dut quang lien tuc.
        waitAll = true;
        if (mode != lastLogged) {
          lastLogged = mode;
          Serial.println("=> Nguon cham hon toc do phat: doi nhan xong roi phat.");
        }
      }
    }

    // ---- mo loa
    // Bo dem gan day thi phai phat du dang o che do doi: khong con cho chua.
    bool bufFull = stUsed >= stageCap - (size_t)IO_CHUNK * 2;
    if (!playing && (netEof || bufFull || (decided && !waitAll && stUsed >= gate))) {
      playing = true;
      if (!tFirst) {
        tFirst = millis();
        Serial.printf(">>> DANG PHAT (cho dem %lu ms, %u byte)...\n",
                      tFirst - tStart, (unsigned)stUsed);
      }
    }

    // ---- day mot mieng ra loa
    if (playing) {
      // Chua biet cau ket thuc o dau thi giu lai dung mot doan fade, de luc
      // phat hien het luong con co cai ma vuot xuong, khong cat phut.
      size_t avail = stUsed;
      if (!netEof) {
        size_t hold = scond.fade * frameBytes;
        avail = avail > hold ? avail - hold : 0;
      }

      size_t run = avail;
      if (run > stageCap - stStart) run = stageCap - stStart;
      if (run > IO_CHUNK) run = IO_CHUNK;
      run -= run % frameBytes;

      if (run > 0) {
        int16_t *p = (int16_t *)(stageBuf + stStart);
        size_t   outBytes = run;

        if (fmt.ch == 2) {          // I2S dang mono: tron hai kenh tai cho
          size_t frames = run / 4;
          for (size_t i = 0; i < frames; i++)
            p[i] = (int16_t)(((int32_t)p[2 * i] + p[2 * i + 1]) / 2);
          outBytes = frames * 2;
        }

        size_t leftAfter = netEof ? (stUsed - run) / frameBytes : (size_t)-1;
        scondChunk(p, outBytes / 2, leftAfter);

        // Vuot quanh cho hut. Day moi la thu quyet dinh tai co nghe ra khoang
        // dut hay khong — cat phang giua song am tao ra mot buoc nhay, va
        // buoc nhay do nghe thanh tieng "bup". Vuot xuong roi vuot len thi
        // cung khoang lang ay lai nghe nhu mot quang nghi binh thuong.
        bool willStarve = !netEof && (stUsed - run) < danger;

        if (willStarve && duck > 0.5f) {
          applyRamp(p, outBytes / 2, 1.0f, 0.0f);
          duck = 0.0f;
        } else if (!willStarve && duck < 0.5f) {
          applyRamp(p, outBytes / 2, 0.0f, 1.0f);
          duck = 1.0f;
        } else if (duck < 0.5f) {
          applyRamp(p, outBytes / 2, 0.0f, 0.0f);   // van dang dong
        }

        size_t put = 0, n;
        while (put < outBytes) {
          if (i2s_channel_write(i2sTx, (uint8_t *)p + put, outBytes - put, &n,
                                1000 / portTICK_PERIOD_MS) != ESP_OK) {
            Serial.println("LOI write ra loa");
            ioErr = true;
            break;
          }
          put += n;
        }
        if (ioErr) break;

        written += outBytes;
        stStart  = (stStart + run) % stageCap;
        stUsed  -= run;
      } else if (!netEof) {
        // Bo dem can day. Dong lai cho nap them thay vi phat nho giot — hut
        // lien tuc nghe con te hon mot khoang lang duy nhat.
        //
        // Va NOI RONG muc nap lai: mang da chung to la khong theo kip, nen
        // mo lai o dung muc cu thi chac chan hut tiep. Tu lon dan nghia la
        // mang tot khong phai tra gia gi, mang xau thi chi giat mot lan roi
        // no tu chuyen sang che do an toan.
        playing = false;
        underruns++;

        // Hut lan thu hai la du bang chung: nguon khong theo kip, khong phai
        // chi giat mot nhip. Nang dem tiep chi keo dai chuoi giat — chuyen han
        // sang doi nhan xong. Mot khoang lang duy nhat de chiu hon nhieu so
        // voi tieng noi dut quang lien tuc.
        if (underruns >= 2 && !waitAll) {
          waitAll = true;
          Serial.println("  hut lan thu hai -> chuyen sang doi nhan xong roi phat tiep");
        } else if (!waitAll && refillMs < REFILL_MAX_MS) {
          gate = bytesPerMs * refillMs;
          if (gate > stageCap / 2) gate = stageCap / 2;
          Serial.printf("  hut lan %u -> nang dem len %u ms\n",
                        (unsigned)underruns, (unsigned)refillMs);
          refillMs += REFILL_STEP_MS;
        }
      }
    }

    if (netEof && stUsed < frameBytes) break;
    if (!playing) vTaskDelay(2 / portTICK_PERIOD_MS);
  }

  i2sWriteSilence(FLUSH_MS);

  Serial.printf("Phat %u byte (~%lu ms tieng), so lan hut: %u\n",
                (unsigned)written, (unsigned long)(written / 2 * 1000UL / rate),
                (unsigned)underruns);
  Serial.printf("PCM: dinh=%d, mau cham tran=%u\n",
                (int)scond.peak, (unsigned)scond.clipped);
  if (underruns > 0)
    Serial.println("=> Loa bi hut. Nang PREBUF_MS len, hoac server day cham qua.");

#if LOWER_WIFI_TX
  WiFi.setTxPower(txSaved);
#endif
  if (i2s_channel_enable(i2sRx) != ESP_OK)
    Serial.println("Khong bat lai duoc mic sau khi phat");
  i2sSetSampleRate(SR);

  return !ioErr && written > 0;
}

// Sinh song sin sach roi day thang ra loa. Khong qua duong nan tieng: muon tin
// hieu vao amp la thu sach nhat co the sinh ra, de neu no van re thi ket luan
// duoc ngay la loi phan cung.
void playTone(uint32_t freq, float amp, int ms) {
  size_t n = (size_t)SR * ms / 1000;
  if (n > recMaxSamp) n = recMaxSamp;

  for (size_t i = 0; i < n; i++) {
    float v = sinf(2.0f * (float)PI * freq * (float)i / (float)SR) * amp * 32000.0f;
    recBuf[i] = (int16_t)v;
  }

  // Vuot 5 ms hai dau, neu khong tieng "tach" luc vao/ra se bi nham la re.
  size_t f = SR / 200;
  if (f > n / 2) f = n / 2;
  for (size_t i = 0; i < f; i++) {
    recBuf[i]         = (int16_t)((int32_t)recBuf[i] * i / f);
    recBuf[n - 1 - i] = (int16_t)((int32_t)recBuf[n - 1 - i] * i / f);
  }

  size_t bytes = n * sizeof(int16_t), put = 0, w;
  while (put < bytes) {
    if (i2s_channel_write(i2sTx, (uint8_t *)recBuf + put, bytes - put, &w,
                          1000 / portTICK_PERIOD_MS) != ESP_OK) break;
    put += w;
  }
}
