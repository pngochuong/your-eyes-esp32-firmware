#include "play_queue.h"

// Moc va clip khan deu nho va thua thot — vai cai mot cau. 24 moc chua duoc
// ca chuc cau result-* xep hang ma van chi ~2 KB RAM.
static const uint8_t EV_CAP = 24;
static const uint8_t URG_CAP = 8;

static uint8_t *s_ring = nullptr;
static size_t   s_cap  = 0;

// 🔴 Vi tri la so byte TICH LUY, khong phai chi so trong vong. Chi lay modulo
// luc cham bo nho. Nho vay "con bao nhieu" chi la mot phep tru, va so sanh moc
// voi vi tri doc khong bao gio nham khi vong da quay qua mep.
// uint32_t tran sau 4 GB — o 8 KB/s la 6 ngay tieng lien tuc; phep tru khong
// dau van dung qua diem tran.
static volatile uint32_t s_wpos = 0;
static volatile uint32_t s_rpos = 0;

static PqEvent s_ev[EV_CAP];
static uint8_t s_evHead = 0, s_evLen = 0;

static char    s_urg[URG_CAP][24];
static uint8_t s_urgHead = 0, s_urgLen = 0;

// Mot khoa cho tat ca: moi doan giu khoa chi vai chuc lenh, khong ghi I/O.
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

void pqBegin(uint8_t *ring, size_t cap) {
  s_ring = ring;
  s_cap  = cap;
  s_wpos = 0;
  s_rpos = 0;
  s_evLen = s_urgLen = 0;
}

bool pqReady() { return s_ring != nullptr; }

size_t pqPushAudio(const uint8_t *p, size_t n) {
  if (!s_ring || n == 0) return 0;

  portENTER_CRITICAL(&s_mux);
  uint32_t w = s_wpos, r = s_rpos;
  portEXIT_CRITICAL(&s_mux);

  size_t room = s_cap - (size_t)(w - r);
  if (n > room) {
    Serial.printf("Hang phat day — bo %u byte tieng\n", (unsigned)(n - room));
    n = room;
  }

  // Chep NGOAI khoa: task audio chua the doc vung nay vi s_wpos chua tang.
  // pqFlush() chay xen vao chi day s_rpos len, tuc chi rong them cho trong.
  size_t off = w % s_cap, first = s_cap - off;
  if (first > n) first = n;
  memcpy(s_ring + off, p, first);
  if (n > first) memcpy(s_ring, p + first, n - first);

  portENTER_CRITICAL(&s_mux);
  s_wpos = w + (uint32_t)n;
  portEXIT_CRITICAL(&s_mux);
  return n;
}

bool pqPushEvent(PqEvent e) {
  if (!s_ring) return false;
  bool ok = false;
  portENTER_CRITICAL(&s_mux);
  if (s_evLen < EV_CAP) {
    e.pos = s_wpos;
    s_ev[(s_evHead + s_evLen) % EV_CAP] = e;
    s_evLen++;
    ok = true;
  }
  portEXIT_CRITICAL(&s_mux);
  if (!ok) Serial.println("Hang moc phat day — bo moc");
  return ok;
}

void pqUrgent(const char *cueId) {
  if (!cueId || !*cueId) return;
  portENTER_CRITICAL(&s_mux);
  if (s_urgLen < URG_CAP) {
    char *d = s_urg[(s_urgHead + s_urgLen) % URG_CAP];
    strncpy(d, cueId, sizeof(s_urg[0]) - 1);
    d[sizeof(s_urg[0]) - 1] = 0;
    s_urgLen++;
  }
  portEXIT_CRITICAL(&s_mux);
}

size_t pqAvail() {
  portENTER_CRITICAL(&s_mux);
  uint32_t w = s_wpos, r = s_rpos;
  // Khong doc vuot moc ke tiep: tieng sau moc phai cho moc duoc xu ly truoc.
  if (s_evLen) {
    uint32_t p = s_ev[s_evHead].pos;
    if ((int32_t)(p - r) >= 0 && (int32_t)(p - w) < 0) w = p;
  }
  portEXIT_CRITICAL(&s_mux);
  return (size_t)(w - r);
}

size_t pqRead(uint8_t *dst, size_t n) {
  size_t have = pqAvail();
  if (n > have) n = have;
  if (n == 0) return 0;

  uint32_t r = s_rpos;
  size_t off = r % s_cap, first = s_cap - off;
  if (first > n) first = n;
  memcpy(dst, s_ring + off, first);
  if (n > first) memcpy(dst + first, s_ring, n - first);

  portENTER_CRITICAL(&s_mux);
  // pqFlush() cung chay tren task NAY nen khong the xen giua; chi task mang
  // doi s_wpos, ma s_rpos thi chi task nay ghi.
  s_rpos = r + (uint32_t)n;
  portEXIT_CRITICAL(&s_mux);
  return n;
}

bool pqDueEvent(PqEvent &e) {
  bool due = false;
  portENTER_CRITICAL(&s_mux);
  if (s_evLen && (int32_t)(s_ev[s_evHead].pos - s_rpos) <= 0) {
    e = s_ev[s_evHead];
    due = true;
  }
  portEXIT_CRITICAL(&s_mux);
  return due;
}

void pqPopEvent() {
  portENTER_CRITICAL(&s_mux);
  if (s_evLen) { s_evHead = (s_evHead + 1) % EV_CAP; s_evLen--; }
  portEXIT_CRITICAL(&s_mux);
}

bool pqHasEvent() {
  portENTER_CRITICAL(&s_mux);
  bool has = s_evLen > 0;
  portEXIT_CRITICAL(&s_mux);
  return has;
}

bool pqTakeUrgent(char *id, size_t cap) {
  bool got = false;
  portENTER_CRITICAL(&s_mux);
  if (s_urgLen) {
    strncpy(id, s_urg[s_urgHead], cap - 1);
    id[cap - 1] = 0;
    s_urgHead = (s_urgHead + 1) % URG_CAP;
    s_urgLen--;
    got = true;
  }
  portEXIT_CRITICAL(&s_mux);
  return got;
}

void pqFlush() {
  portENTER_CRITICAL(&s_mux);
  s_rpos  = s_wpos;
  s_evLen = 0;
  s_urgLen = 0;
  portEXIT_CRITICAL(&s_mux);
}

bool pqEmpty() {
  portENTER_CRITICAL(&s_mux);
  bool empty = (s_wpos == s_rpos) && s_evLen == 0 && s_urgLen == 0;
  portEXIT_CRITICAL(&s_mux);
  return empty;
}
