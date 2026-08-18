#include "perf_probe.h"

#include <esp_heap_caps.h>

// 16 moc du rong cho mot lan bam nut (hien dung 9). Mang TINH, khong malloc:
// xem ghi chu o header.
static const int PERF_MAX = 16;

struct PerfEntry {
  const char   *name;
  unsigned long at;
};

static PerfEntry     perfList[PERF_MAX];
static int           perfCount = 0;
static unsigned long perfT0    = 0;
static const char   *perfTag   = "";

void perfBegin(const char *tag) {
  perfTag   = tag ? tag : "";
  perfT0    = millis();
  perfCount = 0;
}

void perfMark(const char *name) {
  // Tran thi BO chang moi, khong ghi de chang cu. Mat moc cuoi de nhan ra hon
  // nhieu so voi mot bang bi xao tron am tham.
  if (perfCount >= PERF_MAX) return;
  perfList[perfCount].name = name;
  perfList[perfCount].at   = millis();
  perfCount++;
}

void perfReport() {
  if (perfCount == 0) return;

  unsigned long total = perfList[perfCount - 1].at - perfT0;

  Serial.printf("\n--- THOI GIAN: %s ---\n", perfTag);
  unsigned long prev = perfT0;
  for (int i = 0; i < perfCount; i++) {
    unsigned long d = perfList[i].at - prev;
    Serial.printf("  %-20s %7lu ms  %3lu%%   (tich luy %lu ms)\n",
                  perfList[i].name, d,
                  total ? d * 100 / total : 0,
                  perfList[i].at - perfT0);
    prev = perfList[i].at;
  }
  Serial.printf("  %-20s %7lu ms\n", "TONG", total);
}

void memReport(const char *when) {
  // 🔴 MALLOC_CAP_INTERNAL mot minh la SAI so lieu: no tinh ca vung chi truy
  // cap duoc theo 32-bit (IRAM du), ma malloc() thong thuong khong lay tu do.
  // Phai kem MALLOC_CAP_8BIT moi ra dung phan DRAM ma du lieu dung duoc.
  const uint32_t capInt = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

  size_t iFree = heap_caps_get_free_size(capInt);
  size_t iBig  = heap_caps_get_largest_free_block(capInt);
  size_t iMin  = heap_caps_get_minimum_free_size(capInt);

  size_t pFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  size_t pBig  = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  size_t pMin  = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);

  Serial.printf("\n--- BO NHO: %s ---\n", when ? when : "");
  Serial.println("                    con trong    khoi lien lon nhat   thap nhat tung cham");
  Serial.printf("  RAM noi (DRAM)  %9u B  %14u B  %14u B\n",
                (unsigned)iFree, (unsigned)iBig, (unsigned)iMin);
  if (pFree || pBig)
    Serial.printf("  PSRAM           %9u B  %14u B  %14u B\n",
                  (unsigned)pFree, (unsigned)pBig, (unsigned)pMin);
  else
    Serial.println("  PSRAM             KHONG CO — kiem tra tuy chon PSRAM=opi");
}
