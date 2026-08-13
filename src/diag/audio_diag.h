#ifndef AUDIO_DIAG_H
#define AUDIO_DIAG_H

// ============================================================================
// Phep thu chan doan duong am thanh
// ============================================================================
// Khong nam tren duong bam nut. Moi ham deu co diem ket thuc ro rang — cai gi
// chay trong task audio ma cho vo han thi nhin ben ngoai giong het thiet bi
// hong.
// ============================================================================
#include <Arduino.h>

// Nghe thu HAI kieu ghi khe I2S, ngay sat nhau, de tai so sanh truc tiep.
void diagSlotCompare();

// Phat song sin SACH do chinh ESP32 sinh ra, tang dan bien do, roi do meo
// tieng bang chinh mic. Muc dich: tach loi phan cung khoi loi phan mem.
void diagSpeakerTone();

// Do nen nhieu khi camera BAT va khi camera TAT. Cap FPC 75mm mang 8 duong
// du lieu dap o 16 MHz — no la mot cai ang-ten, va la nghi pham so mot.
void diagNoiseVsCamera();

#endif  // AUDIO_DIAG_H
