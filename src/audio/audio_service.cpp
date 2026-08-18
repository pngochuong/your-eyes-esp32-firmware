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

#include "adpcm_codec.h"
#include "audio_buffers.h"
#include "audio_cues.h"
#include "audio_dsp.h"
#include "audio_format.h"
#include "audio_i2s.h"
#include "audio_player.h"
#include "audio_recorder.h"
#include "pcm_source.h"
#include "../camera/camera_capture.h"
#include "../diag/serial_console.h"
#include "../net/api_client.h"
#include "../util/perf_probe.h"
#include "../../app_config.h"

// ============================================================================
// Nen ban thu thanh ADPCM va day len NGAY trong luc dang thu
// ============================================================================
// 🔴 Ban thu KHONG con la file WAV. Header WAV phai di truoc du lieu, ma ba
// truong cua no (RIFF size, data size, so mau trong "fact") chi biet duoc khi
// da thu xong — tuc phai doi nha nut roi moi gui duoc byte dau tien. Do that
// tren hotspot 9 KB/s: 26 KB ADPCM cua mot cau 3 giay mat gan 3 giay day,
// cong thang vao khoang nguoi dung ngoi cho.
//
// Bo header di thi khong con gi phai biet truoc: cu du 505 mau la nen ra mot
// khoi 256 byte va giao ngay cho task mang. Luc nha nut chi con khoi cuoi.
// Tham so codec di trong Content-Type cua phan multipart, server tu dung lai
// header WAV neu no can.
//
// Dat o day chu khong o api_client: api_client lo duong MANG, no khong duoc
// biet am thanh ma hoa kieu gi. Doi codec thi chi mot khoi nay doi.
// ============================================================================

// Nhan dan len phan "audio" cua multipart. Dung san mot lan luc khoi dong vi
// no ghep tu SR va ADPCM_BLOCK_BYTES — hai hang so nam o hai file khac nhau,
// va viet tay lai o day la mo duong cho chung lech nhau.
static char g_audType[80];

static AdpcmEnc g_enc;
static size_t   g_encBytes = 0;   // so byte ADPCM da nen vao sendBuf
static size_t   g_pendSamp = 0;   // mau dau tien cua recBuf CHUA duoc nen

static void encodeBegin() {
  adpcmEncReset(g_enc);
  dspStreamBegin();
  g_encBytes = 0;
  g_pendSamp = 0;
}

// Nen het cac khoi DAY co the nen tu so mau dang co, roi bao cho task mang.
// `flush` = thu xong roi, nen not phan le thanh mot khoi cuoi.
static void encodeUpTo(size_t haveSamp, bool flush) {
  if (!sendBuf) return;

  while (g_pendSamp < haveSamp) {
    size_t n = haveSamp - g_pendSamp;
    if (n > (size_t)ADPCM_BLOCK_SAMPLES) n = ADPCM_BLOCK_SAMPLES;
    // Chua du mot khoi va cung chua thu xong -> de danh cho lan sau. Nen som
    // mot khoi cut la tu nguyen vut di ty so nen cua 505 mau.
    if (n < (size_t)ADPCM_BLOCK_SAMPLES && !flush) break;

    if (g_encBytes + ADPCM_BLOCK_BYTES > sendCap) {
      Serial.println("Bo dem gui day — dung nen");
      break;
    }
    g_encBytes += adpcmEncodeBlock(g_enc, recBuf + g_pendSamp, n,
                                   sendBuf + g_encBytes);
    g_pendSamp += n;
  }
  apiPushAudio(g_encBytes, flush);
}

// Ham goi lai cua recordWhileHeld()/recordFixed(): loc roi nen ngay mieng vua
// doc duoc. Chay TRONG task audio, giua hai lan doc mic — vai tram micro giay
// cho mot mieng 32 ms, khong lam cham viec thu.
static void onRecChunk(size_t from, size_t count) {
  dspStreamPrep(from, count);
  encodeUpTo(from + count, false);
}

// Nen not phan le sau khi nha nut, roi dong phan "audio" lai. Tra ve tong so
// byte da gui.
static size_t encodeFinish(size_t pcmBytes) {
  encodeUpTo(pcmBytes / sizeof(int16_t), true);
  Serial.printf("Nen ADPCM: %u -> %u byte (con %.0f%%), khoi cuoi %u byte\n",
                (unsigned)pcmBytes, (unsigned)g_encBytes,
                pcmBytes ? 100.0 * g_encBytes / pcmBytes : 0.0,
                (unsigned)(g_encBytes ? ADPCM_BLOCK_BYTES : 0));
  return g_encBytes;
}

// ============================================================================
// Gui len server roi phat cau tra loi
// ============================================================================
// api_client lo phan mang, audio_player lo phan loa. Ham nay chi chon duong:
// than tra ve la MP3, ADPCM, PCM tho, hay WAV PCM.
static bool waitAndPlay() {
  ApiReply reply;
  if (!apiWaitReply(reply, FIRST_AUDIO_MS)) return false;

  bool played;

  PcmSource src = {};
  src.body = &reply.body;

  // 🔴 Chon duong phat theo BYTE THAT cua than, khong theo HTTP header.
  //
  // Da do bang ffmpeg + Python: bo giai ma ADPCM dung — SNR 35 dB tren
  // bitstream cua ffmpeg, ca khoi 256 lan 1024, va khong xau di khi socket nha
  // vun. Nhung neu di NHAM sang nhanh "tho" trong khi than lai la file WAV thi
  // 60 byte header bi an nhu du lieu nen: moi khoi lech ke tu do, SNR tut
  // xuong -11 dB, tuc nhieu thuan. Tai nghe ra dung la "loa re rat re" — ma
  // khong mot dong log nao bao, vi khong ai doi chieu header voi than.
  //
  // parseAudioFormat() dat raw = true chi vi THAY header x-audio-format, no
  // khong the biet than dong goi kieu gi. Bon byte dau thi biet: "RIFF" la
  // file WAV, khong the la du lieu ADPCM tho.
  uint8_t magic[4] = { 0, 0, 0, 0 };
  bool isRiff = bodySniff(reply.body, magic, 4) && !memcmp(magic, "RIFF", 4);

  if (isRiff && reply.fmt.raw) {
    Serial.println("CANH BAO: header x-audio-format bao du lieu THO, nhung than "
                   "bat dau bang 'RIFF'.");
    Serial.println("  Theo than — doc nhu file WAV. Server nen bo header do, "
                   "hoac tra du lieu tho that su.");
  }

  if (reply.isMp3) {
    // MP3: giai ma truoc khi vao duong phat. Bang thong chi con 1/8 so voi
    // PCM tho, nen day moi la duong khong con bi khung.
    if (!mp3Init(src)) { apiClose(reply); return false; }

    // Giai KHUNG DAU truoc de biet tan so va so kenh — hai thu do nam trong
    // chinh khung MP3, khong co header rieng nhu WAV.
    // Gan tung truong chu khong dung danh sach khoi tao: AudioFmt da them
    // truong `adpcm` o vi tri 2, nen `{ false, 0, 0, 16 }` am tham thanh
    // ch = 16, bits = 0. Lan nay khong hai vi ca hai bi ghi de ngay ben duoi,
    // nhung mot cai bay im lang thi khong dang giu lai.
    AudioFmt mf = {};
    mf.bits = 16;
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
  } else if (!isRiff && reply.fmt.raw && reply.fmt.adpcm) {
    // ADPCM tho, dinh dang bao qua x-audio-format. Khong biet tong do dai.
    if (!adpcmInit(src, reply.fmt)) { apiClose(reply); return false; }
    src.isAdpcm = true;
    Serial.printf("ADPCM tho: %u Hz, khoi %u byte -> %u mau\n",
                  (unsigned)reply.fmt.rate, reply.fmt.blockAlign,
                  reply.fmt.samplesPerBlock);
    played = playPcmStream(src, reply.fmt, 0);
    adpcmFree(src);
  } else if (!isRiff && reply.fmt.raw) {
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
      Serial.println("Than tra ve khong phai WAV PCM 16-bit / IMA ADPCM hop le");
      // In bon byte dau. Khong co no thi "than hong" va "board doc nham kieu
      // dong goi" nhin giong het nhau — ma hai cai sua o hai dau khac han.
      Serial.printf("  4 byte dau than: %02X %02X %02X %02X  '%c%c%c%c'\n",
                    magic[0], magic[1], magic[2], magic[3],
                    isprint(magic[0]) ? magic[0] : '.',
                    isprint(magic[1]) ? magic[1] : '.',
                    isprint(magic[2]) ? magic[2] : '.',
                    isprint(magic[3]) ? magic[3] : '.');
      apiClose(reply);
      return false;
    }

    // 🔴 playPcmStream() do "nguon day duoc bao nhieu byte/giay" tren so byte
    // pcmRead() TRA RA, tuc byte da giai nen. Nen tong do dai truyen vao cung
    // phai la so byte SAU giai nen — dua thang do dai khoi "data" cua file
    // ADPCM vao la lech 4 lan, no tuong cau ngan bang 1/4 va phat qua som.
    size_t totalPcm = dataLen;
    if (wf.adpcm && dataLen && wf.blockAlign && wf.samplesPerBlock) {
      size_t blocks = (dataLen + wf.blockAlign - 1) / wf.blockAlign;
      totalPcm = blocks * wf.samplesPerBlock * sizeof(int16_t);
    }

    Serial.printf("WAV %s: %u Hz, %u kenh, PCM %s byte\n",
                  wf.adpcm ? "IMA ADPCM 4-bit" : "16-bit",
                  (unsigned)wf.rate, wf.ch,
                  totalPcm ? String((unsigned)totalPcm).c_str() : "khong bao truoc");

    if (wf.adpcm) {
      if (!adpcmInit(src, wf)) { apiClose(reply); return false; }
      src.isAdpcm = true;
    }
    played = playPcmStream(src, wf, totalPcm);
    if (wf.adpcm) adpcmFree(src);
  }
  apiClose(reply);
  perfMark("phat het cau");

  // 🔴 Moc nay gio la luc BAT DAU day anh, khong con la luc gui xong. Anh di
  // trong khi nguoi dung con dang noi, nen "gui xong" khong con la mot thoi
  // diem duy nhat de lay lam goc.
  Serial.printf("Tron mot lan: %lu ms ke tu luc bat dau day anh\n",
                millis() - reply.tStart);
  return played;
}

// ============================================================================
// Vong doi mot lan nhan nut
// ============================================================================
// Giu nguyen trinh tu cua audioTask, chi thay dieu kien thu am
// ============================================================================
void audioSimulatePress(unsigned long holdMs) {
  perfBegin("GIA LAP MOT LAN BAM NUT");
  Serial.printf("\n===== GIA LAP BAM NUT (%lu ms) =====\n", holdMs);

  // Giong het duong bam nut that: mo TLS truoc tien, chay song song.
  apiBegin();

  bool havePhoto = photoCapture();
  perfMark("chup anh");

  // Giao anh ngay — no se duoc day len trong luc dang thu tieng.
  if (havePhoto) apiPushImage(photoData(), photoSize());

  // Mo phan tieng roi thu — nen va day dan trong luc thu, giong het duong
  // bam nut that. Duong gia lap phai la CUNG mot duong, khong thi con so do
  // duoc o day khong noi gi ve duong that.
  encodeBegin();
  apiAudioOpen(sendBuf, "record.adpcm", g_audType);

  size_t pcmBytes = recordFixed(holdMs, onRecChunk);
  perfMark("thu am + nen + day song song");

  if (!havePhoto) {
    Serial.println("Khong co anh — bo qua lan nay");
    apiAbort();
    return;
  }
  if (pcmBytes == 0) {
    Serial.println("Khong thu duoc gi — bo qua lan nay");
    apiAbort();
    photoRelease();
    return;
  }

  size_t audBytes = encodeFinish(pcmBytes);
  perfMark("nen + day khoi cuoi");

  bool ok = false;
  if (audBytes == 0) {
    Serial.println("Khong nen duoc ban thu — bo qua lan nay");
    apiAbort();
  } else if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Chua co WiFi — khong gui duoc");
    apiAbort();
  } else {
    cueSent();
    perfMark("keu tieng da nhan");
    ok = waitAndPlay();
  }
  dspStreamReport(pcmBytes / sizeof(int16_t));
  if (!ok) cueError();

  photoRelease();
  perfReport();
  memReport("sau lan gia lap");
}

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
    perfBegin("MOT LAN BAM NUT");
    Serial.println("\n===== BAM NUT =====");

    // 0. Mo TLS NGAY BAY GIO, chay song song o core 0.
    //
    // 🔴 Dat truoc ca viec chup anh, va la dong dau tien cua mot lan bam. Do
    // that tren hotspot 4G: bat tay TCP+TLS toi Cloudflare mat 7-8 giay. Neu
    // doi toi luc nha nut moi mo thi nguoi dung ngoi cho tron 8 giay do. Con
    // o day thi no chay lut trong khoang chup anh + thu tieng — luc nha nut
    // duong da san, chi con viec do byte ra.
    //
    // Ham tra ve tuc thi, khong lam cham viec chup mot mili giay nao.
    apiBegin();

    // 1. Chup TRUOC khi thu: anh phai la khoanh khac bam, khong phai luc nha.
    bool havePhoto = photoCapture();
    perfMark("chup anh");

    // 2. Giao anh cho task mang NGAY, dung doi thu xong.
    //
    // 🔴 Day la chang an tien nhat cua ca luong. Duong day len do duoc chi
    // 5.9-9 KB/s; buc anh 720p khoang 60 KB, tuc bay giay. Nhung nguoi dung
    // dang giu nut va dang noi — bay giay do la thoi gian chet, du de day het
    // anh. Doi nha nut roi moi gui thi bay giay do cong THANG vao khoang
    // nguoi dung ngoi cho.
    //
    // photoData() phai con song toi khi apiWaitReply() tra ve: task mang doc
    // thang tu PSRAM, khong sao chep. Vi vay photoRelease() nam o cuoi vong.
    if (havePhoto) apiPushImage(photoData(), photoSize());

    // 2b. Mo phan tieng cua multipart NGAY BAY GIO, truoc khi co byte nao.
    //
    // 🔴 Day la chang thay doi ca cam giac dung may. Truoc kia ban thu chi bat
    // dau di sau khi nha nut, va 26 KB ADPCM tren hotspot 9 KB/s la gan 3 giay
    // ngoi im. Gio moi 505 mau (32 ms) sinh ra mot khoi 256 byte va di ngay,
    // nen luc nha nut chi con dung MOT khoi phai day — duoi 0.2 giay tren moi
    // duong truyen.
    encodeBegin();
    apiAudioOpen(sendBuf, "record.adpcm", g_audType);

    // 3. Thu cho toi khi nha nut. onRecChunk() loc + nen + giao cho task mang
    // ngay giua hai lan doc mic.
    size_t pcmBytes = recordWhileHeld(onRecChunk);
    perfMark("thu am + nen + day song song");

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
    // Chang nay PHAI bang ~0. Khac 0 nghia la recordWhileHeld() da tra ve som
    // hon luc nha tay — tuc la cham tran MAX_SECS, cau hoi bi cat cut.
    perfMark("cho nha nut");

    // Bo qua lan bam thi phai bo luon lan gui dang do, neu khong socket nam
    // do om ~45 KB heap cho toi lan bam sau — ma lan bam sau lai xin mo mot
    // cai moi, nen cho nay tung la duong ro ri cham.
    if (millis() - tPress < MIN_MS || pcmBytes == 0) {
      Serial.println("Nhan qua ngan — bo qua");
      apiAbort();
      photoRelease();
      continue;
    }
    if (!havePhoto) {
      Serial.println("Khong co anh — bo qua lan nay");
      apiAbort();
      continue;
    }

    // Chi con phan le chua du mot khoi. Chang nay PHAI gan bang 0 — day la
    // con so tra loi cau hoi "nha nut xong bao lau thi request di het".
    size_t audBytes = encodeFinish(pcmBytes);
    perfMark("nen + day khoi cuoi");

    // 4. Keu tieng bao da nhan. Luc nay byte cuoi da nam trong tay task mang
    // roi (encodeFinish o tren), nen cueSent() chiem core 1 mat ~0.94 giay
    // khong lam cham duong truyen mot mili giay nao — hai viec chay o hai core.
    bool ok = false;
    if (audBytes == 0) {
      Serial.println("Khong nen duoc ban thu — bo qua lan nay");
      apiAbort();
    } else if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Chua co WiFi — khong gui duoc");
      apiAbort();
    } else {
      cueSent();
      perfMark("keu tieng da nhan");

      // 4. Cho header roi vua nhan vua phat — khong doi tron file.
      ok = waitAndPlay();
    }

    // In cac con so cua ban thu SAU khi da gui xong: chung chi de nguoi doc
    // log biet mic thu duoc gi, khong con anh huong toi byte di len server.
    dspStreamReport(pcmBytes / sizeof(int16_t));

    // Moi duong hong deu phai keu len. Nguoi dung khong nhin man hinh: neu im
    // lang thi "dang cho server" va "hong tu dau, bam lai di" nghe giong het
    // nhau, va ho se dung do rat lau truoc khi doan ra.
    if (!ok) cueError();

    photoRelease();
    perfReport();
    memReport("sau mot lan bam nut");
  }
}

// ============================================================================
// Khoi tao — goi sau startCameraServer()
// ============================================================================
bool startAudio() {
  pinMode(BTN, INPUT_PULLUP);

  // Ghep nhan Content-Type tu chinh hai hang so ma bo nen dang dung, de no
  // khong the lech voi byte that su di len day.
  snprintf(g_audType, sizeof(g_audType),
           "audio/x-adpcm-ima; rate=%u; channels=1; block=%u",
           (unsigned)SR, (unsigned)ADPCM_BLOCK_BYTES);

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

  // 🔴 Hai moc nay (truoc/sau khi cap phat audio) la so lieu goc de tra loi
  // "co nhet them duoc WakeNet khong". Cot quyet dinh la KHOI LIEN LON NHAT
  // cua RAM NOI — AFE cua esp-sr xin RAM noi, khong xin PSRAM.
  memReport("truoc khi cap phat audio");

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

  memReport("sau khi cap phat audio — day la muc nen thuc te");

  Serial.printf("Audio san sang (toi da %u giay/lan).\n",
                (unsigned)(recMaxSamp / SR));
  Serial.printf("Giu nut GPIO%d de chup + thu.\n", BTN);
  Serial.println("Go 'c' de chup thu, 'r' de xem bo nho con lai.");
  return true;
}
