#ifndef AUDIO_DSP_H
#define AUDIO_DSP_H

// ============================================================================
// Xu ly ban thu truoc khi gui di
// ============================================================================
// Tat ca cac ham o day lam viec TAI CHO tren recBuf[] cua audio_buffers.*.
// Khong doc mic, khong noi mang — chi bien doi so lieu.
// ============================================================================
#include <Arduino.h>

// Vi tri bat dau cua cua so 100 ms yen nhat ma quietestRms() vua tim thay.
extern size_t quietestStart;

// RMS cua cua so 100 ms yen nhat trong `cnt` mau dau cua recBuf[]. Goi hai lan
// (truoc va sau khi loc) de biet bao nhieu phan cua "nhieu" la tai nghe duoc,
// bao nhieu la mot chieu voi u tram — hai thu hoan toan khac nhau ma mot con
// so RMS tron khong phan biet noi.
double quietestRms(size_t cnt);

// Nang luong con lai sau khi loc thong cao ~3 kHz, do tren cua so bat dau tu
// `start`. Dung de biet nhieu nam o dai tan nao, vi moi dai mot nguyen nhan:
//   - don o dai THAP (76 Hz - 1 kHz): u nguon, ghep qua day cap nguon
//   - don o dai CAO (> 3 kHz): nhieu so, xung nhip camera, day tin hieu chay
//     canh cap FPC — thu nay khong den tu nguon ma den tu buc xa
//   - deu ca hai dai: nhieu nhiet cua chinh mic, khong sua duoc
double highBandRms(size_t start, size_t cnt);

// Ha am nhung doan KHONG co tieng noi, lay `noiseRms` lam nguong.
void noiseGate(size_t cnt, double noiseRms);

// Loc mot chieu, chan on roi khuech dai ban thu len muc server doc duoc.
void normalizeRecording(size_t bytes);

#endif  // AUDIO_DSP_H
