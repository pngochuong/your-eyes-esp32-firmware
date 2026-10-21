#include "audio_player.h"

#include <math.h>
#include <WiFi.h>

#include "adpcm_codec.h"
#include "audio_buffers.h"
#include "audio_cues.h"
#include "audio_i2s.h"
#include "play_queue.h"
#include "../../app_config.h"

// ------------------------------------------------------------ chinh loa re
// PLAY_GAIN_PCT: bien do phat, tinh theo % goc.
//
// 🔴 Da ha 70 → 55 → 45 (2026-08-18): o 70 nguoi dung van bao "hoi re". Do that
// mot lan phat: dinh 14889/32767, so mau cham tran = 0. Tuc la meo nam o phan
// ANALOG, tu dinh dong lam sut rail 5 V. Cat tran so thi ha bao nhieu cung vo
// ich, con ha dinh dong thi an ngay.
#define PLAY_GAIN_PCT   45

// PLAY_HPF_HZ: cat moi thu duoi tan so nay TRUOC khi day ra loa.
//
// 🔴 Day moi la nut an tieng re, khong phai PLAY_GAIN_PCT. Loa mini 3 W 8 ohm
// khong tai tao noi duoi khoang 300 Hz — mang loa chi phap phong het bien do
// ma khong ra am, va chinh cai phap phong do la tieng "re".
//
// Muon to hon ma khong re: HA gain 9 dB cua MAX98357A truoc (noi chan GAIN len
// Vdd = 6 dB), roi nang PLAY_GAIN_PCT len.
#define PLAY_HPF_HZ     180

// Vuot bien do o dau va cuoi moi doan tieng. Khong co thi mang loa giat mot
// cai, tai nghe thanh tieng "tach".
#define FADE_MS         8

// Bom im lang sau khi phat, de day not doan duoi ra khoi DMA va dua mang loa
// ve 0 thay vi lap lai mieng dem cu.
#define FLUSH_MS        40

// Preroll mac dinh khi server khong noi (cau cua live khong co `reply`).
// Spec: `reply.audio.preroll_s` = 0.2 s.
#define PREROLL_DEFAULT_MS  200

// Cho du preroll toi da bay lau. Qua han thi phat voi nhung gi dang co — mot
// cau ngan hon preroll thi se khong bao gio du.
#define PREROLL_MAX_WAIT_MS 1500

// 🔴 Bao lau khong co khoi moi thi coi la het doan va vuot xuong.
// DMA sau 8 x 480 khung = 240 ms, va i2s_channel_write() chan khi DMA day — tuc
// ngay sau moi lan ghi DMA con >= 200 ms tieng. Cho 120 ms van con thua ~80 ms
// truoc khi loa that su can, nen mot khoi den tre vai chuc ms KHONG lam dut
// tieng; qua 120 ms moi chiu la het cau.
#define STARVE_MS       120

// Hang rong bao lau thi tra loa, bat lai mic. Cau ke tiep cua server (result-*,
// cau noi cua chi duong) thuong toi sat sau; tat-bat mic lien tuc la vo ich.
#define IDLE_EXIT_MS    400

// Reply co audio: cho khoi 0x02 / reply_end toi da bay lau sau lan cuoi co viec.
// Qua han thi coi nhu server bo do, tra loa — khong om mic vinh vien.
#define REPLY_AUDIO_WAIT_MS 8000

// So byte/giay cua ADPCM 16 kHz khoi 256: 256 * 16000 / 505.
static const uint32_t ADPCM_BPS = (uint32_t)ADPCM_BLOCK_BYTES * SR / ADPCM_BLOCK_SAMPLES;

// ---------------------------------------------------------------- nan tieng
static struct {
  float   hpX, hpY, hpA;
  int32_t peak;
} scond;

static void scondReset() {
  scond.hpX = scond.hpY = 0.0f;
  scond.hpA = expf(-2.0f * (float)M_PI * PLAY_HPF_HZ / (float)SR);
  scond.peak = 0;
}

// Loc + ha muc + vuot hai dau TAI CHO, roi day ra I2S.
static bool writeOut(int16_t *p, size_t n, bool fadeIn, bool fadeOut) {
  const float  A    = scond.hpA;
  const size_t fade = (size_t)SR * FADE_MS / 1000;

  for (size_t i = 0; i < n; i++) {
    float x = (float)p[i];
    float y = A * (scond.hpY + x - scond.hpX);
    scond.hpX = x;
    scond.hpY = y;

    int32_t a = (int32_t)(x < 0 ? -x : x);
    if (a > scond.peak) scond.peak = a;

    int32_t v = (int32_t)(y * PLAY_GAIN_PCT / 100.0f);
    if (fadeIn && i < fade)            v = (int32_t)((int64_t)v * i / fade);
    if (fadeOut && n - 1 - i < fade)   v = (int32_t)((int64_t)v * (n - 1 - i) / fade);
    p[i] = v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v);
  }

  size_t bytes = n * sizeof(int16_t), put = 0, w;
  while (put < bytes) {
    if (i2s_channel_write(i2sTx, (uint8_t *)p + put, bytes - put, &w,
                          1000 / portTICK_PERIOD_MS) != ESP_OK) {
      Serial.println("LOI write ra loa");
      return false;
    }
    put += w;
  }
  return true;
}

// ---------------------------------------------------------- giu mot khoi lai
// 🔴 Luon GIU LAI khoi vua giai, chi ghi khoi TRUOC no. Ly do: chi biet mot
// khoi la khoi CUOI cua doan khi khoi sau khong toi — luc do no phai duoc vuot
// xuong, ma da ghi ra DMA roi thi khong vuot duoc nua. Cau cua chi duong live
// khong co `reply_end`, nen "het cau" chi biet bang cach doi.
static int16_t s_pend[ADPCM_BLOCK_SAMPLES];
static size_t  s_pendN   = 0;
static bool    s_segOpen = false;   // da ghi it nhat mot khoi cua doan nay

static void emit(const int16_t *pcm, size_t n) {
  if (s_pendN) {
    writeOut(s_pend, s_pendN, !s_segOpen, false);
    s_segOpen = true;
  }
  memcpy(s_pend, pcm, n * sizeof(int16_t));
  s_pendN = n;
}

static void endSegment() {
  if (s_pendN) writeOut(s_pend, s_pendN, !s_segOpen, true);
  s_pendN   = 0;
  s_segOpen = false;
}

// ---------------------------------------------------------------- trang thai
static PlayerPlayedFn s_onPlayed = nullptr;
void playerSetPlayedHandler(PlayerPlayedFn fn) { s_onPlayed = fn; }

static bool          s_active   = false;
static wifi_power_t  s_txSaved;

// Luot dang phat. Mot `reply` mo luot, `reply_end` dong luot.
static char          s_turn[48]    = "";
static bool          s_cueOnly     = false;   // reply.audio == null: bao played sau cue
static unsigned long s_replyAt     = 0;       // luc moc REPLY duoc xu ly
static unsigned long s_firstAudio  = 0;

// Preroll cho doan tieng sap mo.
static bool          s_preArmed    = true;
static uint32_t      s_preBytes    = 0;
static unsigned long s_preSince    = 0;

bool playerActive() { return s_active; }

static void armPreroll(uint32_t ms) {
  uint32_t b = ADPCM_BPS * ms / 1000;
  b = (b + ADPCM_BLOCK_BYTES - 1) / ADPCM_BLOCK_BYTES * ADPCM_BLOCK_BYTES;
  s_preArmed = true;
  s_preBytes = b;
  s_preSince = millis();
}

static void markAudio() {
  if (s_turn[0] && !s_firstAudio) s_firstAudio = millis();
}

static void reportPlayed(unsigned long firstByteMs) {
  if (!s_turn[0]) return;
  if (s_firstAudio && s_onPlayed) s_onPlayed(s_turn, firstByteMs, s_firstAudio);
  s_turn[0] = 0;
  s_cueOnly = false;
}

static void openOut() {
  if (s_active) return;
  s_active = true;
  // Tat mic trong luc phat, giong duong cu: khong ai doc RX thi DMA RX tran.
  i2s_channel_disable(i2sRx);
  i2sSetSampleRate(SR);
#if LOWER_WIFI_TX
  s_txSaved = WiFi.getTxPower();
  WiFi.setTxPower(WIFI_POWER_11dBm);
#endif
  scondReset();
  s_pendN = 0;
  s_segOpen = false;
}

static void closeOut() {
  if (!s_active) return;
  endSegment();
  i2sWriteSilence(FLUSH_MS);
#if LOWER_WIFI_TX
  WiFi.setTxPower(s_txSaved);
#endif
  if (i2s_channel_enable(i2sRx) != ESP_OK)
    Serial.println("Khong bat lai duoc mic sau khi phat");
  s_active = false;
  armPreroll(PREROLL_DEFAULT_MS);
}

// Phat tron mot clip tu flash. Tra ve false neu bi stop() cat ngang.
static bool playClip(const char *id, PlayerStopFn stop) {
  const CueClip *c = cueFind(id);
  if (!c) return true;
  endSegment();

  int16_t  pcm[ADPCM_BLOCK_SAMPLES];
  uint32_t left = c->samples;
  for (uint32_t off = 0; off < c->bytes && left; off += ADPCM_BLOCK_BYTES) {
    if (stop && stop()) { s_pendN = 0; s_segOpen = false; return false; }
    size_t blk = c->bytes - off < ADPCM_BLOCK_BYTES ? c->bytes - off : ADPCM_BLOCK_BYTES;
    size_t n   = adpcmDecodeBlock(c->data + off, blk, pcm, ADPCM_BLOCK_SAMPLES);
    if (n > left) n = left;           // cat phan dem cua khoi cuoi
    left -= n;
    markAudio();
    emit(pcm, n);
  }
  endSegment();
  return true;
}

// ============================================================================
bool playerPump(PlayerStopFn stop) {
  if (!pqReady()) return false;
  if (!s_active && pqEmpty()) return false;

  openOut();

  unsigned long tLast = millis();      // lan cuoi co viec gi de lam
  uint8_t       blk[ADPCM_BLOCK_BYTES];
  int16_t       pcm[ADPCM_BLOCK_SAMPLES];
  char          urg[24];

  for (;;) {
    if (stop && stop()) {
      // Bo khoi dang giu, khong vuot: nguoi dung vua bam la muon IM ngay.
      s_pendN = 0;
      s_segOpen = false;
      s_turn[0] = 0;
      closeOut();
      return true;
    }

    // ---- 1. clip khan: cat ngang moi thu
    if (pqTakeUrgent(urg, sizeof(urg))) {
      if (!playClip(urg, stop)) continue;     // vong sau se bat stop()
      armPreroll(PREROLL_DEFAULT_MS);
      tLast = millis();
      continue;
    }

    // ---- 2. moc da toi
    PqEvent e;
    bool due = pqDueEvent(e);

    // Reply khong co audio: bao played ngay khi cue cua no da phat xong, tuc
    // khi moc ke tiep KHONG con la cue nua. Khong doi reply_end — server co
    // the khong gui reply_end cho reply chi co cue.
    if (s_cueOnly && !(due && e.type == PQ_CUE)) reportPlayed(s_replyAt);

    if (due) {
      pqPopEvent();
      tLast = millis();
      if (e.type == PQ_REPLY) {
        endSegment();
        if (s_turn[0]) reportPlayed(0);       // reply truoc bi cat, khong co reply_end
        strncpy(s_turn, e.turn, sizeof(s_turn) - 1);
        s_turn[sizeof(s_turn) - 1] = 0;
        s_firstAudio = 0;
        // Moc REPLY mang luc khung `reply` TOI kinh — voi reply chi co cue,
        // do la "byte audio dau" gan dung nhat ma kinh co.
        s_replyAt    = e.firstByteMs ? e.firstByteMs : millis();
        s_cueOnly    = !e.hasAudio;
        armPreroll(e.prerollMs ? e.prerollMs : PREROLL_DEFAULT_MS);
      } else if (e.type == PQ_CUE) {
        playClip(e.cue, stop);
      } else if (e.type == PQ_REPLY_END) {
        endSegment();
        if (strcmp(e.turn, s_turn) == 0) reportPlayed(e.firstByteMs);
        armPreroll(PREROLL_DEFAULT_MS);
      }
      continue;
    }

    // ---- 3. tieng
    size_t avail = pqAvail();
    // Co moc xep sau thi phan tieng truoc no da ve DU — khong phai doi
    // preroll, va khoi cut cuoi (neu co) cung la khoi that, giai luon.
    bool bounded = pqHasEvent();

    if (avail > 0 && (avail >= ADPCM_BLOCK_BYTES || bounded)) {
      if (s_preArmed && !s_segOpen && !s_pendN) {
        if (avail < s_preBytes && !bounded &&
            millis() - s_preSince < PREROLL_MAX_WAIT_MS) {
          vTaskDelay(2 / portTICK_PERIOD_MS);
          continue;
        }
        s_preArmed = false;
      }
      size_t got = pqRead(blk, ADPCM_BLOCK_BYTES);
      size_t n   = adpcmDecodeBlock(blk, got, pcm, ADPCM_BLOCK_SAMPLES);
      if (n) {
        markAudio();
        emit(pcm, n);
      }
      tLast = millis();
      continue;
    }

    // ---- khong co gi doc duoc luc nay
    if (s_pendN && millis() - tLast > STARVE_MS) {
      endSegment();                         // het doan: vuot xuong khoi cuoi
      armPreroll(PREROLL_DEFAULT_MS);       // doan sau lai dem tu dau
    }
    // 🔴 Reply CO audio ma chua toi reply_end thi KHONG tra loa. Do that tren
    // board (2026-10-20): khoi 0x02 dau toi ~0.4-0.5 s SAU khung `reply`, va
    // server sinh tieng theo TUNG CAU nen giua hai cau con im lau hon nua. Tra
    // loa o IDLE_EXIT_MS la tat-bat mic va mo loa lai nhieu lan cho MOT cau tra
    // loi — log in "Phat xong (dinh PCM 0)" ngay sau moi reply.
    bool replyOpen = s_turn[0] && !s_cueOnly &&
                     millis() - tLast < REPLY_AUDIO_WAIT_MS;
    if (!s_pendN && pqEmpty() && !replyOpen && millis() - tLast > IDLE_EXIT_MS) {
      if (s_cueOnly) reportPlayed(s_replyAt);
      closeOut();
      Serial.printf("Phat xong (dinh PCM %d)\n", (int)scond.peak);
      return false;
    }
    vTaskDelay(2 / portTICK_PERIOD_MS);
  }
}

// ============================================================================
// Cuoc goi: loa va mic chay CUNG luc
// ============================================================================
// Dem chong giat: du bay nhieu khoi moi bat dau phat, va lai dem tu dau moi
// lan dut. Server da dem 60 ms (CALL_JITTER_MS) o phia app; phan nay lo chang
// server -> Cloudflare -> Wi-Fi.
#define CALL_PREROLL_BLOCKS  3      // ~95 ms
// 🔴 Tieng don lai qua tran thi BO bot, khong phat het. Dong ho cua app va
// cua kinh lech nhau mot chut, cong voi moi lan mang nghen xa ra mot cuc —
// phat het thi do tre cuoc goi chi tang, sau vai phut hai ben noi de len nhau.
#define CALL_MAX_BLOCKS      16     // ~505 ms
#define CALL_KEEP_BLOCKS     4

static bool s_callPre    = true;    // dang dem lai preroll
static bool s_callSilent = true;    // khoi vua ghi la im lang -> khoi sau vuot len

void playerCallBegin() {
  if (s_active) closeOut();
  i2sSetSampleRate(SR);
#if LOWER_WIFI_TX
  s_txSaved = WiFi.getTxPower();
  WiFi.setTxPower(WIFI_POWER_11dBm);
#endif
  scondReset();
  s_callPre    = true;
  s_callSilent = true;
  // 🔴 Moi DAY DMA truoc (CLAUDE.md muc 5: DMA TX lo lung o muc can la re).
  // Sau do moi vong ghi dung mot khoi bang dung mot khoi mic doc ra, nen DMA
  // dung yen o muc day — doi lai loa tre ~240 ms co dinh.
  i2sWriteSilence(300);
}

void playerCallStep() {
  const size_t BB = ADPCM_BLOCK_BYTES;
  uint8_t blk[ADPCM_BLOCK_BYTES];
  int16_t pcm[ADPCM_BLOCK_SAMPLES];

  // Moc (reply sau cuoc goi) KHONG lay o day: pqAvail() dung truoc moc, nen
  // tieng cuoc goi phat toi moc roi thoi, phan sau moc de playerPump() lo.
  size_t avail = pqAvail();
  if (s_callPre && avail >= CALL_PREROLL_BLOCKS * BB) s_callPre = false;

  if (!s_callPre && avail > CALL_MAX_BLOCKS * BB) {
    size_t drop = 0;
    while (pqAvail() > CALL_KEEP_BLOCKS * BB) drop += pqRead(blk, BB);
    Serial.printf("Goi: tieng den don %u B — bo bot de khong tre them\n", (unsigned)drop);
  }

  if (!s_callPre && pqAvail() >= BB) {
    size_t got = pqRead(blk, BB);
    size_t n   = adpcmDecodeBlock(blk, got, pcm, ADPCM_BLOCK_SAMPLES);
    if (n) {
      writeOut(pcm, n, s_callSilent, false);
      s_callSilent = false;
      return;
    }
  }

  // Het tieng: phat MOT khoi im lang dung bang mot khoi mic, de nhip vong
  // khong doi, roi dem lai preroll.
  if (!s_callPre && !s_callSilent) s_callPre = true;
  memset(pcm, 0, sizeof(pcm));
  writeOut(pcm, ADPCM_BLOCK_SAMPLES, false, false);
  s_callSilent = true;
}

void playerCallEnd() {
  // Bo tieng cuoc goi con sot. pqRead() dung truoc moc ke tiep, nen `reply
  // result-*` toi sau cuoc goi ("Me da cup may...") van con nguyen.
  uint8_t blk[ADPCM_BLOCK_BYTES];
  while (pqRead(blk, sizeof(blk))) {}
  i2sWriteSilence(FLUSH_MS);
#if LOWER_WIFI_TX
  WiFi.setTxPower(s_txSaved);
#endif
  armPreroll(PREROLL_DEFAULT_MS);
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
