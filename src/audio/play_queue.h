#ifndef PLAY_QUEUE_H
#define PLAY_QUEUE_H

// ============================================================================
// Hang doi phat: mot luong ADPCM + cac moc gan vao vi tri trong luong
// ============================================================================
// Server giu MOT kenh audio 0x02 co thu tu cho ca phien (FIRMWARE_ENDPOINTS
// muc 4.2): cau nay gui tron roi moi toi cau sau. Nen ben kinh chi can MOT
// luong byte phat tuan tu. Thu duy nhat phai giu them la CHO NAO trong luong
// thi lam gi — phat cue cua `reply`, bao `played` o `reply_end` — goi la moc.
//
//   task mang  (nguoi ghi DUY NHAT)  pqPushAudio(), pqPushEvent(), pqUrgent()
//   task audio (nguoi doc DUY NHAT)  pqRead(), pqDueEvent(), pqTakeUrgent(),
//                                    pqFlush()
//
// Moc mang vi tri = so byte da ghi vao luong luc moc toi. `reply` luon toi
// TRUOC khoi 0x02 cua no, nen cue cua no nam dung truoc tieng cua no — giu
// duoc thu tu ma khong can doan.
//
// Khoi nay KHONG giai ma, KHONG cham I2S, KHONG biet JSON. Chi giu byte va moc.
//
// 🔴 "Khan" (pqUrgent) la hang RIENG, khong co vi tri: hazard phai keu NGAY ke
// ca giua hai khoi 0x02 (muc 1, buoc 14), khong duoc xep sau cau dang doc.
// ============================================================================
#include <Arduino.h>

enum PqEvType : uint8_t {
  PQ_REPLY,       // bat dau mot `reply`: doi luot, tinh lai moc first_audio
  PQ_CUE,         // phat clip cue tu flash tai day
  PQ_REPLY_END,   // het audio cua `reply` do: bao `played`
};

struct PqEvent {
  uint8_t       type;
  uint32_t      pos;            // dien boi pqPushEvent()
  bool          hasAudio;       // PQ_REPLY: reply.audio khac null
  uint16_t      prerollMs;      // PQ_REPLY: reply.audio.preroll_s
  unsigned long firstByteMs;    // PQ_REPLY_END: luc byte 0x02 dau cua reply toi
  char          turn[48];       // PQ_REPLY / PQ_REPLY_END
  char          cue[24];        // PQ_CUE
};

// Giao vung nho cho luong ADPCM. Goi MOT lan luc khoi dong.
void pqBegin(uint8_t *ring, size_t cap);
bool pqReady();

// ---- phia task mang
// Tra ve so byte nhan duoc. Day thi bo phan du — in mot dong, khong chan.
size_t pqPushAudio(const uint8_t *p, size_t n);
bool   pqPushEvent(PqEvent e);
void   pqUrgent(const char *cueId);

// ---- phia task audio
// So byte doc duoc ngay, KHONG vuot qua moc ke tiep. Moc chua toi thi tieng
// sau moc chua duoc doc — nho vay cue phat dung cho.
size_t pqAvail();
size_t pqRead(uint8_t *dst, size_t n);

// Moc da toi vi tri doc (pos <= dang doc). Lay ra bang pqPopEvent().
bool pqDueEvent(PqEvent &e);
void pqPopEvent();

// Co moc nao xep sau vi tri doc khong (du chua toi).
bool pqHasEvent();

// Lay mot clip khan dang cho. false = khong co.
bool pqTakeUrgent(char *id, size_t cap);

// Bo moi thu dang cho: tieng, moc, clip khan. Goi khi nguoi dung bam nut —
// server cung bo cau dang doc do o phia no (muc 4.2).
void pqFlush();

// Khong con gi de phat.
bool pqEmpty();

#endif  // PLAY_QUEUE_H
