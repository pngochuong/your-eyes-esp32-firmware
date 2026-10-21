#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

// ============================================================================
// Phat ra loa: luong ADPCM + clip cue tu flash, theo dung thu tu trong hang
// ============================================================================
// Doc tu play_queue va phat. Khong biet byte toi tu dau, khong biet socket —
// task mang day vao hang, task audio goi playerPump() de xa ra loa.
//
// Thu tu xu ly moi vong:
//   1. clip KHAN (hazard, cue cua live)  -> cat ngang cau dang phat, keu ngay
//   2. moc da toi vi tri doc             -> doi luot / phat cue / bao played
//   3. tieng 0x02                        -> giai ma tung khoi 256 byte roi phat
//
// Khong biet gi ve mang hay nut bam: nguoi goi dua ham `stop` vao, tra true la
// dung ngay (nguoi dung bam nut — FIRMWARE_ENDPOINTS muc 1, buoc 7).
// ============================================================================
#include <Arduino.h>

typedef bool (*PlayerStopFn)();

// Goi khi da phat het audio cua mot `reply` (toi `reply_end`, hoac het cue voi
// reply khong co audio). `firstAudioMs` = luc mau dau tien cua reply do ra loa.
// Chay TRONG task audio.
typedef void (*PlayerPlayedFn)(const char *turn, unsigned long firstByteMs,
                               unsigned long firstAudioMs);

void playerSetPlayedHandler(PlayerPlayedFn fn);

// Phat moi thu dang co trong hang. Tra ve NGAY khi hang rong. Khi da mo loa
// thi o lai cho toi khi hang rong them mot quang ngan (cau ke tiep thuong toi
// sat sau), hoac `stop()` tra true.
// Tra ve true neu dung vi stop().
bool playerPump(PlayerStopFn stop);

// Dang mo loa (mic dang tat). Task audio khong duoc thu am khi true.
bool playerActive();

// ---- cuoc goi (FIRMWARE_ENDPOINTS muc 16): song cong, mic VAN BAT
// Khac playerPump(): khong tat mic, khong cue, khong `played`. Kenh 0x02 luc
// `active` chi con tieng dau kia.
//
// Begin: mo loa, moi day DMA. Step: phat DUNG MOT khoi (505 mau) — tieng neu
// co, im lang neu chua — goi moi vong ngay sau khi doc mot khoi mic, nen loa
// va mic giu cung nhip. End: bo tieng cuoc goi con sot (giu nguyen moc
// `reply` sau no), tra loa.
void playerCallBegin();
void playerCallStep();
void playerCallEnd();

// Sinh song sin sach roi day thang ra loa. Dung recBuf[] lam cho dung song.
// Chi dung khi chan doan.
void playTone(uint32_t freq, float amp, int ms);

#endif  // AUDIO_PLAYER_H
