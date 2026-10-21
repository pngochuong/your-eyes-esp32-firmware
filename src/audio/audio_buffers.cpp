#include "audio_buffers.h"

#include "adpcm_codec.h"
#include "../util/mem_alloc.h"
#include "../../app_config.h"

// Suc chua hang phat (play_queue), tinh bang byte ADPCM. Hang giu tieng NEN,
// 8.1 KB/s, nen 512 KB = ~64 giay tieng — du cho mot cau tra loi dai cong vai
// cau result-* xep sau, ma chi ton 512 KB PSRAM.
#define STAGE_BYTES     (512 * 1024)

// Tran thu am tinh luc chay, vi con so phu thuoc co PSRAM hay khong.
// camera_device ha camera xuong SVGA/DRAM khi thieu PSRAM; audio phai lui
// theo, neu khong 320 KB buffer thu am se khong bao gio cap phat noi trong DRAM.
int16_t *recBuf     = NULL;
size_t   recMaxSamp = (size_t)SR * MAX_SECS;
uint8_t *sendBuf    = NULL;
size_t   sendCap    = 0;
uint8_t *stageBuf   = NULL;
size_t   stageCap   = 0;

bool audioBuffersAlloc() {
  // Khong PSRAM: camera da ha xuong SVGA/DRAM, audio cung phai lui
  // tran thu am xuong con DRAM_SECS thi moi mong cap phat duoc.
  if (!psramFound()) {
    recMaxSamp = (size_t)SR * DRAM_SECS;
    Serial.printf("Khong co PSRAM — ha tran thu am xuong %d giay\n", DRAM_SECS);
  }

  recBuf = (int16_t *)bigAlloc(recMaxSamp * sizeof(int16_t));
  if (!recBuf) {
    Serial.printf("HET RAM (%u byte) — giam MAX_SECS, fb_count hoac frame_size\n",
                  (unsigned)(recMaxSamp * sizeof(int16_t)));
    return false;
  }

  // ADPCM tho cua ban thu, task mang day len thanh khung 0x03 trong luc dang
  // thu. Nen 4:1 nen no chi bang mot phan tu recBuf — 10 giay: 320 KB -> 81 KB.
  //
  // Xin rieng chu khong nen DE CHONG len recBuf: nen tai cho PHA ban thu goc,
  // ma ban thu goc con dung cho lenh chan doan.
  sendCap  = adpcmEncodedBytes(recMaxSamp);
  sendBuf  = (uint8_t *)bigAlloc(sendCap);
  if (!sendBuf) {
    Serial.printf("Khong xin duoc %u byte bo dem gui\n", (unsigned)sendCap);
    audioBuffersFree();
    return false;
  }

  stageCap = STAGE_BYTES;
  stageBuf = (uint8_t *)bigAlloc(stageCap);
  if (!stageBuf) {
    Serial.printf("Khong xin duoc %u byte hang phat — ha STAGE_BYTES xuong\n",
                  (unsigned)stageCap);
    audioBuffersFree();
    return false;
  }
  return true;
}

void audioBuffersFree() {
  if (recBuf)   { free(recBuf);   recBuf = NULL; }
  if (sendBuf)  { free(sendBuf);  sendBuf = NULL; }
  if (stageBuf) { free(stageBuf); stageBuf = NULL; }
  sendCap  = 0;
  stageCap = 0;
}
