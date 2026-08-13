// ============================================================================
// 🔴 Cac file nay la .cpp, KHONG phai .ino — co ly do. Arduino tu sinh
// prototype cho moi ham trong tab .ino roi chen len DAU file gop, tuc la
// truoc dong #include <WiFiClientSecure.h>. Luc do kieu WiFiClientSecure chua
// ton tai -> "redeclared as different kind of entity". File .cpp khong bi
// Arduino dong vao, nen khong dinh. Doi lai phai tu #include <Arduino.h> vi
// .cpp khong duoc chen san.
// ============================================================================
#include "audio_service.h"

#include <WiFi.h>

#include "audio_buffers.h"
#include "audio_dsp.h"
#include "audio_format.h"
#include "audio_i2s.h"
#include "audio_player.h"
#include "audio_recorder.h"
#include "pcm_source.h"
#include "../camera/camera_capture.h"
#include "../diag/serial_console.h"
#include "../net/api_client.h"
#include "../../app_config.h"

// ============================================================================
// Gui len server roi phat cau tra loi
// ============================================================================
// api_client lo phan mang, audio_player lo phan loa. Ham nay chi chon duong:
// than tra ve la MP3, PCM tho, hay WAV.
static bool sendAndPlay(size_t pcmBytes) {
  ApiReply reply;
  if (!apiSendCapture(photoData(), photoSize(), recBuf, pcmBytes, reply))
    return false;

  bool played;

  PcmSource src = {};
  src.body = &reply.body;

  if (reply.isMp3) {
    // MP3: giai ma truoc khi vao duong phat. Bang thong chi con 1/8 so voi
    // PCM tho, nen day moi la duong khong con bi khung.
    if (!mp3Init(src)) { apiClose(reply); return false; }

    // Giai KHUNG DAU truoc de biet tan so va so kenh — hai thu do nam trong
    // chinh khung MP3, khong co header rieng nhu WAV.
    AudioFmt mf = { false, 0, 0, 16 };
    unsigned long tw = millis();
    while (millis() - tw < (unsigned long)FIRST_AUDIO_MS) {
      int n = mp3DecodeFrame(src);
      if (n < 0) break;
      if (n > 0) {
        MP3FrameInfo fi;
        MP3GetLastFrameInfo(src.dec, &fi);
        mf.rate = (uint32_t)fi.samprate;
        mf.ch   = (uint16_t)fi.nChans;
        src.outLen = (size_t)n;      // giu lai khung nay, khong vut di
        src.outPos = 0;
        Serial.printf("MP3: %d Hz, %d kenh, %d kbps\n",
                      fi.samprate, fi.nChans, fi.bitrate / 1000);
        break;
      }
      vTaskDelay(5 / portTICK_PERIOD_MS);
    }

    if (mf.rate == 0 || (mf.ch != 1 && mf.ch != 2)) {
      Serial.println("Khong giai duoc khung MP3 dau tien");
      mp3Free(src);
      apiClose(reply);
      return false;
    }

    src.isMp3 = true;
    played = playPcmStream(src, mf, 0);
    mp3Free(src);
  } else if (reply.fmt.raw) {
    // Co x-audio-format => PCM tho, khong header. Nhung cung khong biet tong
    // do dai, nen khi nguon cham chi con cach doi nhan het.
    played = playPcmStream(src, reply.fmt, 0);
  } else {
    // WAV: doc header ngay tren luong. Vai chuc byte dau, khong ton thoi gian,
    // doi lai biet duoc TONG DO DAI — nho do tinh duoc thoi diem som nhat
    // van phat lien mach duoc, thay vi doi het ca file.
    AudioFmt wf;
    size_t   dataLen = 0;
    if (!readWavHeader(reply.body, wf, &dataLen)) {
      Serial.println("Than tra ve khong phai WAV PCM 16-bit hop le");
      apiClose(reply);
      return false;
    }
    Serial.printf("WAV: %u Hz, %u kenh, 16-bit, PCM %s byte\n",
                  (unsigned)wf.rate, wf.ch,
                  dataLen ? String((unsigned)dataLen).c_str() : "khong bao truoc");
    played = playPcmStream(src, wf, dataLen);
  }
  apiClose(reply);

  Serial.printf("Tron mot lan: %lu ms ke tu luc gui xong\n",
                millis() - reply.tStart);
  return played;
}

// ============================================================================
// Vong doi mot lan nhan nut
// ============================================================================
static void audioTask(void *arg) {
  for (;;) {
    // Lenh chan doan qua Serial. Xem serial_console.h.
    consolePoll();

    if (digitalRead(BTN) != BTN_ACTIVE) {
      vTaskDelay(20 / portTICK_PERIOD_MS);      // vTaskDelay, KHONG delay()
      continue;
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);        // debounce
    if (digitalRead(BTN) != BTN_ACTIVE) continue;

    unsigned long tPress = millis();
    Serial.println("\n===== BAM NUT =====");

    // 1. Chup TRUOC khi thu: anh phai la khoanh khac bam, khong phai luc nha.
    bool havePhoto = photoCapture();

    // 2. Thu cho toi khi nha nut.
    size_t pcmBytes = recordWhileHeld();

    // Doi nut nha han (truong hop cham tran MAX_SECS).
    //
    // 🔴 Vong nay TUNG khong co gioi han. Khi chan nut chap xuong GND that,
    // digitalRead luon tra BTN_ACTIVE nen no cho vinh vien: thiet bi cham
    // tran 10 giay roi cam tit, khong log gi them, nhin ben ngoai giong hong
    // hoan toan. Mot su co phan cung 1 dong day khong duoc phep bien thanh
    // treo im lang — phai keu len.
    {
      const unsigned long STUCK_MS = 15000;
      unsigned long tRel = millis();
      while (digitalRead(BTN) == BTN_ACTIVE) {
        if (millis() - tRel > STUCK_MS) {
          Serial.printf("LOI PHAN CUNG: GPIO%d o muc thap lien tuc %lu s.\n",
                        BTN, STUCK_MS / 1000);
          Serial.println("Nut bi ket, chap, hoac day tin hieu cham GND. "
                         "Kiem tra day nut — phan mem khong chay tiep duoc.");
          break;
        }
        vTaskDelay(20 / portTICK_PERIOD_MS);
      }
    }

    if (millis() - tPress < MIN_MS || pcmBytes == 0) {
      Serial.println("Nhan qua ngan — bo qua");
      photoRelease();
      continue;
    }
    if (!havePhoto) {
      Serial.println("Khong co anh — bo qua lan nay");
      continue;
    }

    normalizeRecording(pcmBytes);

    // 3+4. Gui len server, roi vua nhan vua phat — khong doi tron file.
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Chua co WiFi — khong gui duoc");
    } else {
      sendAndPlay(pcmBytes);
    }

    photoRelease();
    Serial.printf("Xong. heap %u, PSRAM %u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  }
}

// ============================================================================
// Khoi tao — goi sau startCameraServer()
// ============================================================================
bool startAudio() {
  pinMode(BTN, INPUT_PULLUP);

  // Nut khong duoc phep dang bam luc khoi dong. Neu doc ra dang bam thi gan
  // nhu chac chan la chap day, va neu khong bao o day thi trieu chung se la
  // "may tu chup tu thu roi treo" — rat kho lan ra nguyen nhan.
  delay(5);                     // cho dien tro keo len on dinh
  if (digitalRead(BTN) == BTN_ACTIVE) {
    Serial.printf("CANH BAO: GPIO%d dang o muc thap NGAY LUC KHOI DONG.\n", BTN);
    Serial.println("Nut dang bi ket hoac day tin hieu cham GND. May se tu");
    Serial.println("kich hoat lien tuc cho toi khi sua day.");
  }

  flashLedBegin();

  Serial.printf("Truoc khi cap phat audio: heap %u, PSRAM %u\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());

  if (!audioBuffersAlloc()) return false;

  if (!i2sBegin()) {
    audioBuffersFree();         // tra lai vai tram KB cho camera
    return false;
  }

  // Stack 12 KB: bat tay TLS cua mbedtls mot minh da an ~8 KB.
  // Ghim core 1: core 0 dang lo WiFi/TCP. Priority 1 = duoi httpd.
  if (xTaskCreatePinnedToCore(audioTask, "audio", 12288, NULL, 1, NULL, 1) != pdPASS) {
    Serial.println("Khong tao duoc task audio");
    audioBuffersFree();
    return false;
  }

  Serial.printf("Audio san sang (toi da %u giay/lan): heap %u, PSRAM %u.\n",
                (unsigned)(recMaxSamp / SR),
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  Serial.printf("Giu nut GPIO%d de chup + thu.\n", BTN);
  Serial.println("Gu 'c' vao Serial de chup thu va do do net.");
  return true;
}
