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
// 🔴 Can TRON ban thu trong tay: cong chan on lot nguoc mot luot va he so
// khuech dai lay tu dinh cua ca doan. Duong bam nut that KHONG dung ham nay
// nua (no nen va gui ngay trong luc dang thu, chua co "ca doan"); ham con lai
// de phuc vu lenh chan doan va de doi chieu chat luong hai duong.
void normalizeRecording(size_t bytes);

// ============================================================================
// Ban theo LUONG cua normalizeRecording()
// ============================================================================
// Duong bam nut nen + gui tung khoi 256 byte ngay trong luc nguoi dung con
// dang noi, nen khong the doi co "ca ban thu" roi moi xu ly. Hai viec bo duoc
// va mot viec giu lai:
//
//   giu   loc thong cao 76 Hz — mot cuc, chi mang theo hai bien trang thai,
//         chay theo luong CHINH XAC bang chay mot lan. Day la buoc dang gia
//         nhat: do lech mot chieu cua INMP441 an mat tam dong cua ADPCM.
//   bo    cong chan on — no lot NGUOC de mo cong truoc khi tu bat dau, viec do
//         doi hoi biet tuong lai. Lam mot chieu thi cat mat phu am dau.
//   doi   khuech dai — dinh cua ca doan chua biet, nen dung dinh CHAY: he so
//         chi giam, khong bao gio tang. Khong bao gio kep tieng, doi lai muc
//         am troi xuong dan neu nguoi dung noi to dan.
//
// ADPCM tu no da la mot bo AGC (chi so buoc bam theo bien do tin hieu), nen
// mat hai buoc tren khong lam hong ty so nen — chu yeu la mat mot chut loi cho
// bo nhan dang giong noi ben server.
// ============================================================================

// Dat lai trang thai truoc mot ban thu moi.
void dspStreamBegin();

// Xu ly TAI CHO `count` mau cua recBuf[] bat dau tu `from`. Cac lan goi phai
// lien tuc va khong chong nhau.
void dspStreamPrep(size_t from, size_t count);

// In lai nhung con so ma normalizeRecording() van in, sau khi thu xong.
void dspStreamReport(size_t cnt);

#endif  // AUDIO_DSP_H
