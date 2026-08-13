#include "audio_buffers.h"

#include "../util/mem_alloc.h"
#include "../../app_config.h"

// Suc chua bo dem trung gian. Phai du lon de chua TRON mot cau tra loi, vi
// khi server day cham hon toc do phat thi cach duy nhat khong bi dut la nhan
// het roi moi phat. 40 giay o 16 kHz mono = 1.28 MB PSRAM — con 7 MB, thoai mai.
#define STAGE_MS        40000

// Tran thu am tinh luc chay, vi con so phu thuoc co PSRAM hay khong.
// camera_device ha camera xuong SVGA/DRAM khi thieu PSRAM; audio phai lui
// theo, neu khong 320 KB buffer thu am se khong bao gio cap phat noi trong DRAM.
int16_t *recBuf     = NULL;
size_t   recMaxSamp = (size_t)SR * MAX_SECS;
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

  // Bo dem trung gian cho phat theo luong. Tinh o tan so mic; server tra tan
  // so cao hon thi so giay dem ngan lai tuong ung, van du dung.
  stageCap = (size_t)SR * STAGE_MS / 1000 * sizeof(int16_t);
  stageCap &= ~(size_t)3;                 // giu boi so 4 cho khung stereo
  stageBuf = (uint8_t *)bigAlloc(stageCap);
  if (!stageBuf) {
    Serial.printf("Khong xin duoc %u byte bo dem phat — ha STAGE_MS xuong\n",
                  (unsigned)stageCap);
    audioBuffersFree();
    return false;
  }
  return true;
}

void audioBuffersFree() {
  if (recBuf)   { free(recBuf);   recBuf = NULL; }
  if (stageBuf) { free(stageBuf); stageBuf = NULL; }
  stageCap = 0;
}
