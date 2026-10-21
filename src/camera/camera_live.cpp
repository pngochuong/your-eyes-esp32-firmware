#include "camera_live.h"

#include "camera_device.h"

// Tran cua server (FIRMWARE_ENDPOINTS muc 11). Vuot thi server bo khung, nen
// bo ngay o day cho do ton duong len.
static const size_t LIVE_MAX_BYTES = 300 * 1024;
static const int    LIVE_MAX_FPS   = 15;

// Muc nen cho `capture` don: server khong noi, ma anh nay de OCR doc chu nho.
static const int    CAPTURE_QUALITY = 12;

static LiveSendFn        s_send = nullptr;
static TaskHandle_t      s_task = nullptr;

// Lenh moi nhat. Task "audio"/"wssess" ghi, task "live" doc — moi truong mot
// nguoi ghi, va task live doc lai MOI vong nen khong can khoa.
static volatile LiveKind    s_kind    = LIVE_NONE;
static volatile framesize_t s_size    = FRAMESIZE_VGA;
static volatile int         s_fps     = 5;
static volatile int         s_quality = 12;
static volatile bool        s_capReq  = false;
static volatile framesize_t s_capSize = FRAMESIZE_UXGA;
static volatile bool        s_paused  = false;

LiveKind liveKind() { return s_kind; }

static const char *kindName(LiveKind k) {
  return k == LIVE_OCR ? "OCR" : k == LIVE_NAV ? "chi duong" : "khong";
}

// Chup mot khung o `fs` va gui. Giu khoa camera chi trong luc CHUP — luc gui
// (co the vai giay tren duong cham) thi tra khoa de anh luot hoi khong bi chan.
// Dem de lenh 'v' biet live con chay that khong — server khong bao gi noi lai.
static volatile uint32_t s_sent = 0, s_dropped = 0;

static void grabAndSend(framesize_t fs, int quality) {
  cameraLock();
  cameraSetMode(fs, quality);        // doi do phan giai thi no tu bo khung hong
  camera_fb_t *fb = esp_camera_fb_get();
  cameraUnlock();
  if (!fb) return;

  if (fb->format == PIXFORMAT_JPEG && fb->len <= LIVE_MAX_BYTES && s_send) {
    if (s_send(fb->buf, fb->len)) s_sent = s_sent + 1;
    else                          s_dropped = s_dropped + 1;
  } else if (fb->len > LIVE_MAX_BYTES) {
    Serial.printf("Live: khung %u KB vuot tran 300 KB — bo\n", (unsigned)(fb->len / 1024));
  }
  esp_camera_fb_return(fb);
}

void livePrintStatus() {
  Serial.printf("Live    : %s, %s %d fps nen %d — da gui %u khung 0x04, bo %u\n",
                kindName(s_kind), cameraFrameSizeName(s_size), s_fps, s_quality,
                (unsigned)s_sent, (unsigned)s_dropped);
}

static void liveTask(void *) {
  unsigned long tNext = 0;
  for (;;) {
    if (s_capReq) {
      s_capReq = false;
      Serial.printf("Live: chup mot anh %s\n", cameraFrameSizeName(s_capSize));
      grabAndSend(s_capSize, CAPTURE_QUALITY);
      // Live con chay thi vong sau grabAndSend() tu dat lai theo s_size. Khong
      // con thi tra camera ve HD, de UXGA khong o lai cho web stream.
      if (s_kind == LIVE_NONE) {
        cameraLock();
        cameraUseDefault();
        cameraUnlock();
      }
      continue;
    }

    if (s_kind == LIVE_NONE || s_paused) {
      // Ngu cho toi khi co lenh. Thuc dinh ky de khong phu thuoc vao viec
      // ai do quen danh thuc.
      ulTaskNotifyTake(pdTRUE, 500 / portTICK_PERIOD_MS);
      continue;
    }

    int fps = s_fps;
    if (fps < 1) fps = 1;
    if (fps > LIVE_MAX_FPS) fps = LIVE_MAX_FPS;
    unsigned long period = 1000UL / fps;

    long wait = (long)(tNext - millis());
    if (wait > 0) {
      ulTaskNotifyTake(pdTRUE, (wait + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS);
      continue;
    }
    tNext = millis() + period;
    grabAndSend(s_size, s_quality);
  }
}

bool liveBegin(LiveSendFn send) {
  s_send = send;
  if (s_task) return true;
  // Stack 4 KB: chi goi esp_camera_fb_get() va ham gui; khong TLS, khong String.
  if (xTaskCreatePinnedToCore(liveTask, "live", 4096, nullptr, 1, &s_task, 0) != pdPASS) {
    Serial.println("Khong tao duoc task live");
    s_task = nullptr;
    return false;
  }
  return true;
}

static void wake() { if (s_task) xTaskNotifyGive(s_task); }

void liveCam(LiveKind kind, framesize_t fs, int fps, int quality) {
  s_size    = fs;
  s_fps     = fps;
  s_quality = quality;
  if (s_kind != kind)
    Serial.printf("Live %s: %s, %d fps, nen %d\n", kindName(kind),
                  cameraFrameSizeName(fs), fps, quality);
  s_kind = kind;
  wake();
}

void liveCapture(framesize_t fs) {
  s_capSize = fs;
  s_capReq  = true;
  wake();
}

void liveStop() {
  if (s_kind != LIVE_NONE) Serial.printf("Live %s: dung\n", kindName(s_kind));
  s_kind   = LIVE_NONE;
  s_capReq = false;
  // Tra camera ve HD cho web stream va luot hoi. Khoa vi task live co the
  // dang chup do dang.
  cameraLock();
  cameraUseDefault();
  cameraUnlock();
}

void livePause(bool paused) {
  if (s_paused == paused) return;
  s_paused = paused;
  if (s_kind != LIVE_NONE)
    Serial.printf("Live %s: %s\n", kindName(s_kind), paused ? "tam dung" : "chay lai");
  wake();
}

void liveOnPress() {
  // 🔴 CHI OCR. Nav song qua luot moi (muc 6): nguoi dung hoi gio giua duong
  // van phai duoc canh bao xe toi. Dung nham nav o day la tat canh bao vat can
  // cho toi khi server gui lai `cam` — tuc khong bao gio, vi server tuong nav
  // van chay.
  if (s_kind == LIVE_OCR) liveStop();
}
