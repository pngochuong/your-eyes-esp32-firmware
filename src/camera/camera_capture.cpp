#include "camera_capture.h"

#include "esp_camera.h"
#include "img_converters.h"
#include "../util/mem_alloc.h"
#include "../../app_config.h"

// ============================================================================
// Chup nhieu khung, giu khung net nhat
// ============================================================================
// Chup mot khung duy nhat ngay luc bam nut la bat dung khoanh khac tay rung
// manh nhat — chinh cai an nut lam rung may. Nen chup vai khung roi chon.
//
// Do do net bang KICH THUOC JPEG. Cung mot canh va cung muc nen, khung net
// co nhieu chi tiet tan so cao nen file lon; khung nhoe mat chi tiet nen nen
// nho lai. Day la phep do duy nhat lam duoc ma khong phai giai nen ca anh
// UXGA (ton 4.6 MB RAM va vai giay).
//
// Sao JPEG ra PSRAM roi TRA frame buffer ngay. Giu fb trong suot qua trinh
// thu + upload (vai giay) se lam nghen stream vi camera chi co 2 fb.
#define CAPTURE_TRIES  3        // khung moi loat
#define CAPTURE_GAP_MS 60       // cho camera kip sinh khung moi

// Nguong do net coi la "dat". Cham nguong nay la dung ngay, khong thu tiep.
// 🔴 Con so nay PHAI hieu chinh theo canh that. Bam thu vai lan o cho du sang
// voi anh ro, doc tri so "do net" trong log, roi dat nguong khoang 80% cua no.
#define SHARP_GOOD     12.0f

static uint8_t *jpg    = NULL;   // ban sao anh JPEG (PSRAM, cap phat theo lan)
static size_t   jpgLen = 0;

const uint8_t *photoData() { return jpg; }
size_t         photoSize() { return jpgLen; }

void photoRelease() {
  if (jpg) { free(jpg); jpg = NULL; }
  jpgLen = 0;
}

// ------------------------------------------------------------ den chieu sang
void flashLedBegin() {
#if FLASH_LED_PIN >= 0
  pinMode(FLASH_LED_PIN, OUTPUT);
  flashLed(false);
  Serial.printf("Den chieu sang o GPIO%d\n", FLASH_LED_PIN);
#else
  Serial.println("CHUA co den chieu sang — anh trong toi se nhoe hoac nhieu hat.");
  Serial.println("Noi LED vao mot chan trong roi dat FLASH_LED_PIN trong app_config.h.");
#endif
}

void flashLed(bool on) {
#if FLASH_LED_PIN >= 0
  digitalWrite(FLASH_LED_PIN, on ? FLASH_LED_ON : !FLASH_LED_ON);
#else
  (void)on;
#endif
}

// ---------------------------------------------------------------- do do net
// Kich thuoc JPEG chi so sanh duoc TRONG CUNG mot loat chup: no tang theo do
// net NHUNG CUNG tang theo nhieu hat. Lam cong chan tuyet doi thi sai — anh
// toi day hat co the "to" hon anh sang net. Nen phai do that.
//
// Giai nen o 1/4 do phan giai (400x300 cho UXGA, ~240 KB, ~120 ms) roi tinh
// nang luong dao ham ngang, chia cho do sang trung binh. Chia de so khong doi
// theo do sang — cung mot canh chup toi hay sang deu ra tri so tuong duong.
// Ha ti le xuong 1/4 con giup trung binh hoa bot nhieu hat tung diem anh.
//
// Tra ve -1 neu khong do duoc.
float photoMeasureSharpness(const uint8_t *buf, size_t len, int w, int h) {
  int sw = w / 4, sh = h / 4;
  if (sw < 16 || sh < 16) return -1.0f;

  uint8_t *rgb = (uint8_t *)bigAlloc((size_t)sw * sh * 2);
  if (!rgb) { Serial.println("Het RAM de do do net"); return -1.0f; }

  if (!jpg2rgb565(buf, len, rgb, JPG_SCALE_4X)) {
    Serial.println("Giai nen that bai khi do do net");
    free(rgb);
    return -1.0f;
  }

  // RGB565: lay kenh luc (6 bit) lam thay cho do sang — nhieu bit nhat trong
  // ba kenh va gan luma nhat.
  uint64_t grad = 0, sum = 0;
  const uint16_t *p = (const uint16_t *)rgb;

  for (int y = 0; y < sh; y++) {
    const uint16_t *row = p + (size_t)y * sw;
    int prev = (row[0] >> 5) & 0x3F;
    sum += prev;
    for (int x = 1; x < sw; x++) {
      int g = (row[x] >> 5) & 0x3F;
      int d = g - prev;
      grad += (d < 0 ? -d : d);
      sum  += g;
      prev = g;
    }
  }
  free(rgb);

  double px   = (double)sw * sh;
  double mean = (double)sum / px;
  if (mean < 1.0) return 0.0f;
  return (float)((double)grad / px / mean * 100.0);
}

// Chup mot loat CAPTURE_TRIES khung, tra ve ban sao PSRAM cua khung to nhat.
// Trong cung mot loat, do sang va gain khong doi nen muc nhieu hat nhu nhau —
// luc do chenh lech kich thuoc JPEG phan anh dung do net. (So sanh GIUA cac
// loat co thiet lap khac nhau thi KHONG dung, do la viec cua measureSharpness.)
static uint8_t *burstBest(size_t *outLen, uint16_t *outW, uint16_t *outH,
                          unsigned *outSpread) {
  uint8_t *bestBuf = NULL;
  size_t   best = 0, worst = (size_t)-1;

  for (int i = 1; i <= CAPTURE_TRIES; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.printf("    khung %d: khong lay duoc\n", i);
      vTaskDelay(CAPTURE_GAP_MS / portTICK_PERIOD_MS);
      continue;
    }
    if (fb->format != PIXFORMAT_JPEG) {
      Serial.println("Camera khong o che do JPEG");
      esp_camera_fb_return(fb);
      if (bestBuf) free(bestBuf);
      return NULL;
    }

    if (fb->len < worst) worst = fb->len;

    if (fb->len > best) {
      uint8_t *tmp = (uint8_t *)bigAlloc(fb->len);
      if (tmp) {
        memcpy(tmp, fb->buf, fb->len);
        if (bestBuf) free(bestBuf);
        bestBuf = tmp;
        best    = fb->len;
        *outW   = fb->width;
        *outH   = fb->height;
      } else {
        Serial.printf("Het RAM khi sao anh (%u byte)\n", (unsigned)fb->len);
      }
    }

    esp_camera_fb_return(fb);
    if (i < CAPTURE_TRIES) vTaskDelay(CAPTURE_GAP_MS / portTICK_PERIOD_MS);
  }

  if (!bestBuf) return NULL;
  *outLen = best;
  *outSpread = (worst != (size_t)-1 && worst > 0)
               ? (unsigned)((best - worst) * 100 / worst) : 0;
  return bestBuf;
}

// Duong chup dung luc bam nut: MOT loat, chon khung to nhat, xong.
//
// Da tung co vong tu can chinh o day (chup lai voi ae_level -1, -2 roi do do
// net moi lan). Da BO. Ly do do bang so:
//   - Do net do duoc 1.3 / 1.4 o ca ba lan, chenh lech giua cac khung 0-5%.
//     Tuc la anh mo KHONG phai vi phoi sang, ma vi ong kinh lech tieu cu —
//     thu lai voi phoi ngan hon khong cuu duoc gi.
//   - Doi lai la 4.5 giay moi lan bam, an thang vao doan dau ban thu am va
//     keo dai toan bo vong hoi dap.
// Do net van do duoc bang lenh 'c' qua Serial khi can chan doan; chi la khong
// nam tren duong bam nut nua.
//
// Loat 3 khung ~250 ms van giu: re, va thuc su chon duoc khung do rung tay.
bool photoCapture() {
  photoRelease();

  flashLed(true);
#if FLASH_LED_PIN >= 0
  vTaskDelay(FLASH_SETTLE_MS / portTICK_PERIOD_MS);
#endif

  size_t   len = 0;
  uint16_t w = 0, h = 0;
  unsigned spread = 0;
  uint8_t *buf = burstBest(&len, &w, &h, &spread);

  flashLed(false);          // tat truoc khi thu am

  if (!buf) {
    Serial.println("Khong chup duoc khung nao");
    return false;
  }

  jpg    = buf;
  jpgLen = len;
  Serial.printf("Chup xong: %ux%u, %u byte (chenh lech loat %u%%)\n",
                (unsigned)w, (unsigned)h, (unsigned)len, spread);
  return true;
}
