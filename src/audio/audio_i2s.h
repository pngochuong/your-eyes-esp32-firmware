#ifndef AUDIO_I2S_H
#define AUDIO_I2S_H

// ============================================================================
// Duong I2S full-duplex: mic INMP441 (RX) + amp MAX98357A (TX)
// ============================================================================
// Chi lo phan cung: mo kenh, doi tan so, bom im lang. Khong biet gi ve noi
// dung dang thu hay dang phat.
//
// 🔴 TX va RX dung CHUNG mot khoi clock. Tat mot ben la tat luon dong ho cua
// ben kia — day la ly do khong the "tat loa khi thu" bang cach disable TX.
// ============================================================================
#include <Arduino.h>
#include <driver/i2s_std.h>

extern i2s_chan_handle_t i2sTx;
extern i2s_chan_handle_t i2sRx;

// Giu lai cau hinh khe cua TX de doi duoc luc dang chay (lenh 'm'). Can thiet
// vi khong the biet chac chan SD_MODE cua MAX98357A dang o che do nao neu
// khong nhin thay mach — nghe thu hai kieu la biet ngay.
extern i2s_std_config_t i2sTxCfg;
extern bool             i2sTxSlotBoth;

// Tan so I2S dang cai, de biet khi nao can doi.
extern uint32_t i2sCurRate;

// Mo hai kenh o SR va bat len. Tra ve false neu phan cung khong nhan.
bool i2sBegin();

// Doi tan so lay mau cua ca cum full-duplex.
bool i2sSetSampleRate(uint32_t hz);

// Bom `ms` mili giay im lang vao DMA. Dung sau khi phat de mang loa dung
// lai o vi tri 0 thay vi nhay giat, va de day not doan duoi ra khoi DMA.
void i2sWriteSilence(int ms);

#endif  // AUDIO_I2S_H
