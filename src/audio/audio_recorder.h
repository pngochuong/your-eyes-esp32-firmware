#ifndef AUDIO_RECORDER_H
#define AUDIO_RECORDER_H

// ============================================================================
// Thu am tu mic vao recBuf[]
// ============================================================================
// Chi doc mic. Loc, chan on va khuech dai la viec cua audio_dsp.*.
// ============================================================================
#include <Arduino.h>

// Goi lai moi khi co them mau moi trong recBuf[]. `from`/`count` tinh bang
// MAU, khong phai byte, va cac lan goi lien tuc khong chong nhau.
//
// Ly do ton tai: duong bam nut nen ADPCM va day len mang NGAY trong luc nguoi
// dung con dang noi. Doi thu xong het roi moi bat dau nen la tu chuoc lay ca
// vai giay day byte sau khi nha nut — tren hotspot 9 KB/s do la 3 giay ngoi im.
// Ham goi lai chay TRONG task audio, khong phai task rieng: no chi lam vai
// tram micro giay moi mieng 32 ms.
typedef void (*RecChunkFn)(size_t from, size_t count);

// Thu cho toi khi nha nut hoac cham tran. Tra ve so byte PCM thu duoc.
// `onChunk` co the la nullptr khi nguoi goi muon nhan tron ban thu.
size_t recordWhileHeld(RecChunkFn onChunk);

// Thu dung mot khoang thoi gian dinh truoc, khong ngo toi nut. Tra ve so byte
// PCM thu duoc.
//
// Ly do ton tai: recordWhileHeld() dung nut lam dieu kien dung, nen khong the
// goi tu xa. Khi do dac qua Serial thi khong co ai bam nut — ma chang thu am
// van phai chay that de cac chang sau (loc, nen, gui) nhan dung so byte nhu
// mot lan bam nut binh thuong.
size_t recordFixed(unsigned long ms, RecChunkFn onChunk);

// Thu 1.5 giay im lang roi tra ve nen nhieu NGHE DUOC (da loc thong cao 76 Hz).
// Dung cho phep thu A/B khi chan doan, khong gui di dau. Ham nay CO sua
// recBuf[], nen dung goi khi dang giu mot ban thu can dung.
double measureNoiseOnly();

#endif  // AUDIO_RECORDER_H
