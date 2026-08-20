#ifndef PERF_PROBE_H
#define PERF_PROBE_H

// ============================================================================
// Do thoi gian tung chang va bo nho con lai
// ============================================================================
// Khoi nay chi GHI LAI va IN RA. No khong quyet dinh gi, khong doi hanh vi cua
// bat ky khoi nao — go het cac loi goi toi no thi chuong trinh chay y het.
//
// Nam o util/ chu KHONG phai diag/ vi hai thu khac muc dich: diag/ la nhung
// phep thu chay rieng khi go lenh Serial, con cai nay chay ngay tren duong bam
// nut that. Moi khoi deu duoc phep goi, giong mem_alloc.
//
// KHONG cap phat, KHONG dung String. perfMark() bi goi tu trong vong phat loa;
// cap phat o do la duong ngan nhat toi hut tieng.
// ============================================================================
#include <Arduino.h>

// Bat dau mot lan do moi. Xoa het moc cu.
void perfBegin(const char *tag);

// Ghi mot chang vua xong. `name` phai la chuoi HANG — ham chi giu con tro,
// khong sao chep, nen truyen bien tam vao la in ra rac.
void perfMark(const char *name);

// In bang: moi chang bao lau, chiem bao nhieu phan tram tong, va moc tich luy.
// Cot phan tram moi la cot dang nhin — no tra loi "nen di sua cho nao".
void perfReport();

// In bo nho con lai.
//
// 🔴 Tach RAM NOI khoi PSRAM. ESP.getFreeHeap() gop ca hai lam mot nen khong
// dung de ket luan duoc: PSRAM con 4 MB ma RAM noi can 20 KB thi tong so nhin
// van rat dep, trong khi lan cap phat tiep theo se that bai.
//
// Cot "khoi lien lon nhat" con quan trong hon cot "con trong": bo nho phan
// manh thi tong con nhieu nhung khong xin noi mot mieng lien tuc.
void memReport(const char *when);

#endif  // PERF_PROBE_H
