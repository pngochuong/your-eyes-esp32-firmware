#ifndef SERIAL_CONSOLE_H
#define SERIAL_CONSOLE_H

// ============================================================================
// Lenh chan doan go qua Serial
// ============================================================================
//   c — chup thu, do do net (khong gui di dau)
//   m — so sanh hai cach ghi khe I2S
//   t — thu loa bang song sin va do meo tieng
//   w — kiem tra duong mang: WiFi -> DNS -> TCP/TLS
//   n — do nen nhieu khi camera bat va khi camera tat
//
// 🔴 Tung co them che do chinh net ong kinh ('f') chay vong lap o day.
// Da BO: vong do chi thoat khi co ky tu Serial moi, nen no CHAN LUON
// task audio — bam nut khong con tac dung, ma nhin ben ngoai thi giong het
// nhu thiet bi hong. Moi thu chay trong task do bat buoc phai co diem
// ket thuc, khong duoc cho vo han.
// ============================================================================

// Doc mot ky tu neu co va chay lenh tuong ung. Goi tu vong lap cua task audio.
void consolePoll();

#endif  // SERIAL_CONSOLE_H
