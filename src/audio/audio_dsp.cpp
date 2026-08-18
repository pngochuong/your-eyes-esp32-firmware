#include "audio_dsp.h"

#include <math.h>
#include "audio_buffers.h"
#include "../util/mem_alloc.h"
#include "../../app_config.h"

// Chi so cua so 100 ms yen nhat, de con phan tich rieng doan do.
size_t quietestStart = 0;

// Nhieu nam o dai tan nao? Do tren dung cua so yen nhat da tim duoc, khong
// dung ca ban thu, va KHONG sua recBuf[] (ban thu con phai gui di).
double highBandRms(size_t start, size_t cnt) {
  size_t win = SR / 10;
  if (start + win > cnt) return -1.0;

  // Thong cao mot cuc ~3 kHz: a = exp(-2*pi*fc/fs)
  const float A = 0.308f;
  float hpX = 0.0f, hpY = 0.0f;
  double sum = 0;

  for (size_t i = start; i < start + win; i++) {
    float x = (float)recBuf[i];
    float y = A * (hpY + x - hpX);
    hpX = x;
    hpY = y;
    sum += (double)y * y;
  }
  return sqrt(sum / win);
}

double quietestRms(size_t cnt) {
  size_t win = SR / 10;                       // 100 ms
  if (cnt <= win) return -1.0;

  double quietest = -1.0;
  for (size_t s = 0; s + win <= cnt; s += win) {
    double sum = 0;
    for (size_t i = s; i < s + win; i++) sum += (double)recBuf[i] * recBuf[i];
    double rms = sqrt(sum / win);
    if (quietest < 0 || rms < quietest) { quietest = rms; quietestStart = s; }
  }
  return quietest;
}

// ------------------------------------------------------------ cong chan on
// Ha am nhung doan KHONG co tieng noi. Khong bo duoc tieng on phong khi dang
// noi (cung dai tan, khong tach duoc), nhung lam sach cac khoang lang giua
// cac tu — do la thu ASR de nham thanh am nhat.
//
// GATE_FLOOR khong phai 0: tat han se tao ra cac manh im tuyet doi xen ke,
// nghe nhu bi cat va mot so bo ASR coi do la het cau.
#define GATE_THRESH_X  3.0f    // nguong = bao nhieu lan nen nhieu
#define GATE_FLOOR     0.25f   // muc san, -12 dB

void noiseGate(size_t cnt, double noiseRms) {
  if (noiseRms <= 0 || cnt == 0) return;

  int16_t *env = (int16_t *)bigAlloc(cnt * sizeof(int16_t));
  if (!env) { Serial.println("Khong du RAM cho cong chan on — bo qua"); return; }

  const float aAtt = 0.969f;    // bam len trong ~2 ms
  const float aRel = 0.9996f;   // nha xuong trong ~150 ms

  // Lot xuoi.
  float e = 0.0f;
  for (size_t i = 0; i < cnt; i++) {
    float a = fabsf((float)recBuf[i]);
    e = (a > e) ? (aAtt * e + (1.0f - aAtt) * a)
                : (aRel * e + (1.0f - aRel) * a);
    env[i] = (int16_t)(e > 32767.0f ? 32767.0f : e);
  }

  // Lot NGUOC, lay max voi lot xuoi. Day la mau chot: bao hinh nho vay mo ra
  // TRUOC khi tu bat dau, nen cong khong bao gio cat mat phu am dau. Chi lam
  // duoc vi da co tron ban thu trong tay — phat theo luong thi khong.
  e = 0.0f;
  for (size_t i = cnt; i-- > 0; ) {
    float a = fabsf((float)recBuf[i]);
    e = (a > e) ? (aAtt * e + (1.0f - aAtt) * a)
                : (aRel * e + (1.0f - aRel) * a);
    if (e > (float)env[i]) env[i] = (int16_t)(e > 32767.0f ? 32767.0f : e);
  }

  // He so bien thien LIEN TUC theo bao hinh, khong bat/tat dut khoat — nhay
  // giua hai muc se tao tieng lach tach o moi lan chuyen.
  const float thresh = (float)(noiseRms * GATE_THRESH_X);
  size_t lowered = 0;

  for (size_t i = 0; i < cnt; i++) {
    float r = (float)env[i] / thresh;
    if (r > 1.0f) r = 1.0f;
    float g = GATE_FLOOR + (1.0f - GATE_FLOOR) * r;
    if (g < 0.99f) lowered++;

    int32_t v = (int32_t)(recBuf[i] * g);
    recBuf[i] = v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v);
  }

  free(env);
  Serial.printf("Cong chan on: ha am %u%% thoi luong\n",
                (unsigned)(lowered * 100 / cnt));
}

// INMP441 o che do 16-bit ra rat nho. Loc mot chieu roi khuech dai truoc khi
// gui, de server nhan duoc tin hieu dung muc chu khong phai doan nhieu nen.
void normalizeRecording(size_t bytes) {
  size_t cnt = bytes / sizeof(int16_t);
  if (cnt == 0) return;
  int16_t peak = 0;
  for (size_t i = 0; i < cnt; i++) if (abs(recBuf[i]) > peak) peak = abs(recBuf[i]);

  // Do nen nhieu: RMS cua cua so 100 ms YEN NHAT trong ca doan. Nguoi dung
  // luon co mot khoang lang o dau hoac cuoi, nen cua so do la nhieu thuan.
  double rmsRaw = quietestRms(cnt);

  // Do do lech mot chieu. INMP441 co DC offset that su o dau ra, va do lech
  // do lam RMS phong to len du tai KHONG nghe thay gi — do lech mot chieu la
  // im lang doi voi tai nguoi. Phai tach ra truoc khi ket luan "re".
  int64_t sum = 0;
  for (size_t i = 0; i < cnt; i++) sum += recBuf[i];
  int32_t dc = (int32_t)(sum / (int64_t)cnt);

  // Loc thong cao mot cuc ~76 Hz: cat mot chieu va tieng u tram. Phan con lai
  // moi la nhieu nghe duoc, va cung la thu server can de nhan dang giong noi.
  {
    const float A = 0.97f;
    float hpX = 0.0f, hpY = 0.0f;
    for (size_t i = 0; i < cnt; i++) {
      float x = (float)recBuf[i];
      float y = A * (hpY + x - hpX);
      hpX = x;
      hpY = y;
      recBuf[i] = y > 32767.0f ? 32767 : (y < -32768.0f ? -32768 : (int16_t)y);
    }
  }

  double rmsAc = quietestRms(cnt);

  // Ha am cac khoang lang. Phai lam SAU khi do rmsAc (cong lay chinh con so
  // do lam nguong) va TRUOC khi khuech dai (khong thi lai keo ca on len).
  noiseGate(cnt, rmsAc);

  // Dinh phai do LAI sau khi loc va chan on — hai buoc do deu doi bien do.
  peak = 0;
  for (size_t i = 0; i < cnt; i++) if (abs(recBuf[i]) > peak) peak = abs(recBuf[i]);

  int gain = peak > 0 ? 8000 / peak : 1;
  if (gain < 1)  gain = 1;
  if (gain > 64) gain = 64;
  for (size_t i = 0; i < cnt; i++) {
    int32_t v = (int32_t)recBuf[i] * gain;
    recBuf[i] = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
  }

  Serial.printf("Dinh sau khi loc = %d, he so khuech dai = %d\n", peak, gain);
  Serial.printf("Lech mot chieu = %d\n", (int)dc);

  if (rmsRaw >= 0 && rmsAc >= 0) {
    double snr = rmsAc > 0.5 ? 20.0 * log10((double)peak / rmsAc) : 99.0;
    Serial.printf("Nen nhieu: truoc loc %.1f -> sau loc %.1f, dinh/nen = %.1f dB\n",
                  rmsRaw, rmsAc, snr);

    // Mot dong, khong giang giai. Ai can chan doan sau thi go lenh 'n' — no
    // in day du phan bo dai tan va lam phep thu bat/tat camera.
    if (snr < 25.0)
      Serial.println("=> Tin hieu yeu so voi nen on. Dua mic lai gan mieng hon.");
  }
}

// ============================================================================
// Ban theo LUONG — xem giai thich trong audio_dsp.h
// ============================================================================
// Trang thai cua bo loc thong cao va cua he so khuech dai, mang xuyen qua cac
// lan goi dspStreamPrep().
static float   g_hpX = 0.0f, g_hpY = 0.0f;
static int32_t g_runPeak = 0;      // dinh CUA CA DOAN DA THAY, chi tang
static int     g_gain    = 1;      // suy ra tu g_runPeak, nen chi giam
static size_t  g_seen    = 0;      // so mau da di qua, de bo qua doan mic on dinh

// 🔴 Bo qua 100 ms dau khi do dinh. INMP441 nha ra vai mau BAO HOA ngay sau khi
// bat kenh I2S. Chung khong phai tin hieu, nhung dinh chay thi chi tang: mot
// mau -32768 lot vao la he so bi ghim o 1 suot ca cau, va ban thu di len server
// nho nguyen nhu chua qua khuech dai. Da do duoc dung nhu vay tren mot ban thu
// khong co ai noi gi: "Dinh truoc khuech dai = 32768, he so = 1".
//
// Van LOC va van khuech dai doan nay nhu binh thuong — chi khong cho no du phan
// vao con so dinh.
#define DSP_SETTLE_SAMP  (SR / 10)

// He so nham dua dinh ve khoang 8000/32767 (-12 dBFS) — giong
// normalizeRecording(). Chua tran de mot tieng go bat ngo khong lam ca cau
// sau do bi ep xuong qua nho.
static int gainFor(int32_t peak) {
  int g = peak > 0 ? (int)(8000 / peak) : 1;
  if (g < 1)  g = 1;
  if (g > 64) g = 64;
  return g;
}

void dspStreamBegin() {
  g_hpX = g_hpY = 0.0f;
  g_runPeak = 0;
  g_gain    = 1;
  g_seen    = 0;
}

void dspStreamPrep(size_t from, size_t count) {
  if (count == 0) return;
  const float A = 0.97f;           // mot cuc ~76 Hz, giong normalizeRecording()

  // Luot 1: loc thong cao tai cho, va do dinh cua chinh mieng nay.
  int32_t blockPeak = 0;
  for (size_t i = from; i < from + count; i++) {
    float x = (float)recBuf[i];
    float y = A * (g_hpY + x - g_hpX);
    g_hpX = x;
    g_hpY = y;
    int16_t v = y > 32767.0f ? 32767 : (y < -32768.0f ? -32768 : (int16_t)y);
    recBuf[i] = v;
    int32_t a = v < 0 ? -(int32_t)v : (int32_t)v;
    if (a > blockPeak && g_seen + (i - from) >= (size_t)DSP_SETTLE_SAMP)
      blockPeak = a;
  }
  g_seen += count;

  // 🔴 Cap nhat dinh chay TRUOC khi khuech dai mieng nay, khong phai sau. Neu
  // cap nhat sau thi mot mieng to bat ngo se duoc nhan bang he so cu (tinh cho
  // muc nho hon) va bi kep tran. Tinh truoc thi he so luon vua voi mau to nhat
  // co trong chinh mieng dang xu ly — khong bao gio kep.
  if (blockPeak > g_runPeak) {
    g_runPeak = blockPeak;
    g_gain    = gainFor(g_runPeak);
  }

  if (g_gain > 1) {
    for (size_t i = from; i < from + count; i++) {
      int32_t v = (int32_t)recBuf[i] * g_gain;
      recBuf[i] = v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v);
    }
  }
}

void dspStreamReport(size_t cnt) {
  if (cnt == 0) return;
  // Nen nhieu do SAU khi da loc va khuech dai — day la byte that su di len
  // server, khong phai so lieu cua tin hieu goc.
  double rms = quietestRms(cnt);
  int32_t peakOut = 0;
  for (size_t i = 0; i < cnt; i++) {
    int32_t a = recBuf[i] < 0 ? -(int32_t)recBuf[i] : (int32_t)recBuf[i];
    if (a > peakOut) peakOut = a;
  }
  Serial.printf("Dinh truoc khuech dai = %d, he so = %d, dinh sau = %d\n",
                (int)g_runPeak, g_gain, (int)peakOut);
  if (rms > 0.5) {
    double snr = 20.0 * log10((double)peakOut / rms);
    Serial.printf("Nen nhieu sau loc %.1f, dinh/nen = %.1f dB\n", rms, snr);
    if (snr < 25.0)
      Serial.println("=> Tin hieu yeu so voi nen on. Dua mic lai gan mieng hon.");
  }
}
