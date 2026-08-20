#ifndef AUDIO_CUES_H
#define AUDIO_CUES_H

// ============================================================================
// Hai tieng bao ngan cho nguoi dung khiem thi
// ============================================================================
// Khoi nay CHI day mau ra I2S. No khong biet mang, khong biet server, khong
// quyet dinh khi nao keu — audio_service goi no dung luc.
//
// Vi sao phai co: nguoi dung khong nhin man hinh, khong thay den. Sau khi nha
// nut ho phai doi ~40 giay cho server nghi. Khong co tieng bao thi "dang xu
// ly" va "mat mang, hong roi" nghe giong het nhau — ho se dung do 40 giay roi
// moi biet la phai bam lai.
//
// 🔴 Ca hai tieng deu phat o dung SR (16000 Hz) — bang tan so cua mic. Nho
// vay khong phai goi i2sSetSampleRate(): moi lan doi phai tat/bat CA HAI
// chieu cua cum full-duplex, ton ~90 ms, ma hai tieng nay nam ngay tren duong
// bam nut nen tiec tung chuc ms.
// ============================================================================

// "Server da nhan lenh" — warm_melodic.wav nhung san trong flash, ~0.94 giay.
// Goi NGAY SAU khi byte cuoi cua anh + ban thu da ra khoi board, truoc khi
// ngoi doi server nghi.
void cueSent();

// "Gui hong" — hai tieng 400 Hz ngan. Cao do thap va nhip gat, khac han tieng
// tren, de nguoi dung phan biet duoc ma khong can nghe ky.
void cueError();

#endif  // AUDIO_CUES_H
