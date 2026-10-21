#ifndef AUDIO_CUES_H
#define AUDIO_CUES_H

// ============================================================================
// Clip cue nam san trong flash + tieng bao loi cuc bo
// ============================================================================
// Khoi nay CHI tra cuu clip va sinh tieng bip loi. No khong biet mang, khong
// biet server, khong quyet dinh khi nao keu — audio_service goi no dung luc.
// Phat clip (qua duong nan tieng chung) la viec cua audio_player.
//
// Clip la bo cua SERVER (assets/cues/ ben repo server, chep sang cues/ o day),
// sinh thanh mang C bang tools/gen_cue_clips.py. Kinh khai CUE_VERSION trong
// `hello`; server thay so nay khop thi chi gui ma cue (`reply.cues`, khung
// `cue`, `hazard`) thay vi doc thanh tieng qua mang — clip phat ngay, khong
// cho mot byte nao tu server.
//
// 🔴 Doi bo clip ben server ma quen chay lai gen_cue_clips.py thi kinh van khai
// version cu. Server thay version THAP hon se doc cue thanh tieng (kinh cu van
// nghe du), nhung neu version CAO hon/khop ma clip lai thieu thi kinh im lang —
// nen cueFind() tra NULL phai duoc in ra, khong duoc nuot.
// ============================================================================
#include <Arduino.h>

struct CueClip {
  const char    *id;
  const uint8_t *data;      // khoi "data" IMA ADPCM, khoi 256 byte
  uint32_t       bytes;
  uint32_t       samples;   // so mau that (khoi cuoi co dem)
};

// Ban clip dang nam trong flash. Khai trong `hello`.
int cueVersion();

// Tra clip theo ma (vd "received", "hazard_2"). NULL neu khong co.
const CueClip *cueFind(const char *id);

// "Hong roi, bam lai" — hai tieng 400 Hz ngan. Day thang ra I2S, khong qua
// mang, khong can clip nao: day la tieng phai keu duoc ca khi mat ket noi.
void cueError();

#endif  // AUDIO_CUES_H
