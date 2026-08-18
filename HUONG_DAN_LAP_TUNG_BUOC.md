# Quy trình lắp và test từng linh kiện (bring-up tăng dần)

Mỗi giai đoạn **chỉ thêm đúng một linh kiện**, test xong mới sang cái tiếp theo. Khi hỏng, bạn biết chắc thủ phạm là món vừa cắm.

Bổ sung cho [HUONG_DAN_LAP_RAP.md](HUONG_DAN_LAP_RAP.md) (tra cứu pinout/cảnh báo) và [HUONG_DAN_TEST_CAM.md](HUONG_DAN_TEST_CAM.md) (chi tiết camera).

> 📌 **Đọc trước khi tra code.** Tài liệu này kể lại quá trình dựng máy theo từng giai đoạn, nên các đoạn code bên dưới là bản **ở thời điểm đó**, không phải bản cuối. Sau GĐ 6, toàn bộ mã đã được tách thành module trong `src/` — xem [Giai đoạn 7](#giai-đoạn-7--tách-module) ở cuối để biết hàm nào giờ nằm ở file nào. Sketch chính giờ tên `your-eyes-esp32-firmware.ino` (từng là `VisionCare.ino`, trước nữa là `CameraWebServer.ino`).

---

## Thứ tự tối ưu và lý do

Sắp theo **rủi ro tăng dần** và **dòng tiêu thụ tăng dần**:

| GĐ | Linh kiện | Dòng | Rủi ro | Vì sao ở vị trí này |
|----|-----------|------|--------|---------------------|
| 0 | Camera | ~200 mA | — | ✅ Xong. Chiếm nhiều chân nhất, phải chốt trước để biết chân nào còn trống |
| 1 | Nút MKE-M02 | ~10 mA | Thấp | 3 dây, không ảnh hưởng nguồn. Xác minh GPIO trống dùng được thật |
| 2 | INMP441 | ~1.4 mA | Trung bình | Chỉ 3.3 V, dòng không đáng kể. Kiểm chứng khối I2S trước khi có dòng lớn |
| 3 | MAX98357A + loa | tới ~1 A | **Cao** | Dòng lớn, ngõ ra BTL dễ hỏng. Để cuối để không phá hỏng các bước đã chạy được |
| 4 | Tích hợp | — | Cao | Ghép mic + ampli chung đường clock |
| 5 | Gộp camera | — | Trung bình | Audio chạy task riêng, camera giữ nguyên hành vi |
| 6 | Pipeline server | — | Trung bình | Chỉ là phần mềm — chốt phần cứng xong mới làm |

Nguyên tắc: **không bao giờ cắm hai linh kiện mới rồi mới nạp code.**

---

## ⚠️ Quyết định kiến trúc: I2S phải chạy full-duplex

Đọc phần này trước khi lắp, vì nó quyết định cách viết code ở GĐ 4.

Board chỉ còn **6 GPIO an toàn**: `1, 2, 21, 41, 42, 47`.

Có hai cách nối mic và ampli:

**Cách A — hai khối I2S riêng, mỗi khối có clock riêng**
Cần 6 chân (2×BCLK, 2×WS, 1 DIN, 1 DOUT) + 1 chân nút = **7 chân**. → **Không đủ.**

**Cách B — một khối I2S full-duplex, dùng chung BCLK/WS** ✅
Cần 4 chân + 1 nút = **5 chân**, còn dư GPIO2.

Vì vậy bắt buộc dùng **cách B**: mic và ampli dùng chung `I2S_NUM_0`, chung BCLK và WS.

> 🔴 **Tuyệt đối không** cấu hình mic ở `I2S_NUM_0` và ampli ở `I2S_NUM_1` mà vẫn nối chung dây BCLK/WS. Hai khối đều ở chế độ master sẽ **cùng phát tín hiệu ra một sợi dây** → tranh chấp mức logic, sai dữ liệu, lâu dài hỏng chân GPIO.

Hệ quả thực tế: ở GĐ 2 và GĐ 3 bạn test riêng lẻ (một chiều) thì dùng API Arduino đơn giản được. Nhưng đến GĐ 4 ghép lại, nhiều khả năng phải chuyển sang API `i2s_std` của ESP-IDF vì lớp bọc Arduino không phải lúc nào cũng lộ ra chế độ full-duplex. Đây là chỗ khó nhất của cả dự án — vì thế mới để cuối.

---

## Giai đoạn 0 — Camera ✅

Đã hoàn thành. Kết quả: `Camera Ready! Use 'http://192.168.1.114'`

Trước khi sang GĐ 1, **lưu lại sketch camera đang chạy được** thành một bản riêng (ví dụ `CameraWebServer_OK.ino`). Mỗi giai đoạn sau sẽ test bằng sketch nhỏ riêng biệt, cuối cùng mới gộp.

---

## Giai đoạn 1 — Nút nhấn MKE-M02

Món dễ nhất, mục đích thật sự là **xác minh các GPIO còn trống thực sự dùng được**, không bị board chiếm ngầm.

### 🔴 Đo điện áp SIG TRƯỚC khi nối vào ESP32

MKE-M02 chưa có datasheet điện đầy đủ. Nhà sản xuất ghi nguồn 5 V — nếu SIG cũng lên 5 V thì **nối thẳng vào GPIO sẽ làm hỏng chân ESP32-S3** (chỉ chịu 3.3 V).

1. Cấp `VCC` và `GND` cho module, **chưa nối SIG vào đâu cả**.
2. Đồng hồ đo: que đen vào GND, que đỏ vào chân `SIG`.
3. Nhấn nút và nhả, ghi lại mức cao nhất đo được.

| Kết quả đo | Xử lý |
|---|---|
| ≤ 3.3 V | ✅ Nối thẳng SIG vào GPIO1 |
| ~5 V | ❌ Chuyển VCC sang **3.3 V** rồi đo lại. Nếu vẫn 5 V thì cần cầu chia áp (10 kΩ + 20 kΩ) hoặc level shifter |

### Đấu nối

| MKE-M02 | Board |
|---|---|
| VCC | 3V3 (ưu tiên) hoặc 5V nếu đã đo an toàn |
| GND | GND |
| SIG | **GPIO1** |

### Code test

```cpp
// Test 1: Nut nhan — GPIO1
#define BTN 1

void setup() {
  Serial.begin(115200);
  pinMode(BTN, INPUT);   // doi thanh INPUT_PULLUP neu module khong co tro keo
  Serial.println("San sang. Nhan nut...");
}

void loop() {
  static int last = -1;
  static unsigned long t = 0;
  int v = digitalRead(BTN);

  if (v != last && millis() - t > 50) {   // debounce 50 ms
    Serial.printf("SIG = %d\n", v);
    last = v;
    t = millis();
  }
}
```

### ✅ Đạt khi

Serial in ra giá trị đổi `0 ↔ 1` mỗi lần nhấn, và **không đổi lung tung** khi để yên.

Nếu giá trị nhảy loạn lúc không nhấn → module không có trở kéo, đổi thành `INPUT_PULLUP`.

---

## Giai đoạn 2 — Microphone INMP441

### 🔴 Bắt buộc 3.3 V

INMP441 chịu tối đa 3.3 V. Cắm nhầm 5 V là **hỏng vĩnh viễn ngay lập tức**. Kiểm tra kỹ hàng chân trước khi cấp nguồn.

### Đấu nối

| INMP441 | Board | Ghi chú |
|---|---|---|
| VDD | **3V3** | 🔴 Không phải 5V |
| GND | GND | |
| SCK | **GPIO41** | BCLK |
| WS | **GPIO42** | |
| SD | **GPIO47** | dữ liệu ra từ mic |
| L/R | **GND** | chọn kênh trái |

`L/R` nối GND = mic phát ở slot **LEFT**. Nếu sau này đọc ra toàn số 0, thử đổi `L/R` sang 3V3 và đổi slot trong code thành RIGHT — đây là lỗi phổ biến nhất với INMP441.

### Code test

```cpp
// Test 2: INMP441 — do muc am thanh
#include <ESP_I2S.h>

I2SClass i2s;

void setup() {
  Serial.begin(115200);
  // setPins(bclk, ws, dout, din, mclk)
  i2s.setPins(41, 42, -1, 47, -1);
  if (!i2s.begin(I2S_MODE_STD, 16000,
                 I2S_DATA_BIT_WIDTH_16BIT,
                 I2S_SLOT_MODE_MONO)) {
    Serial.println("I2S begin THAT BAI");
    while (1) delay(100);
  }
  Serial.println("Mic san sang. Noi vao mic...");
}

void loop() {
  const int N = 512;
  int16_t buf[N];
  size_t n = i2s.readBytes((char *)buf, sizeof(buf));
  if (n == 0) return;

  long sum = 0;
  int cnt = n / 2;
  for (int i = 0; i < cnt; i++) sum += abs(buf[i]);
  int level = (sum / cnt) / 200;          // chia cho vua man hinh

  Serial.printf("%5ld |", sum / cnt);
  for (int i = 0; i < min(level, 50); i++) Serial.print('#');
  Serial.println();
  delay(100);
}
```

> ⚠️ Đoạn code này **tôi chưa chạy thử trên phần cứng** — API `ESP_I2S` khác nhau giữa các phiên bản core. Bạn đang dùng core **3.3.10**; nếu không biên dịch được, mở `File → Examples → ESP_I2S` trong Arduino IDE và đối chiếu chữ ký hàm.

### ✅ Đạt khi

Số và dải `#` **tăng rõ rệt khi bạn nói vào mic**, về gần 0 khi im lặng.

| Triệu chứng | Nguyên nhân |
|---|---|
| Luôn bằng 0 | Sai slot L/R — đổi chân `L/R` hoặc đổi slot trong code |
| Số cố định không đổi | Sai chân SD, hoặc mic chưa được cấp nguồn |
| Nhiễu loạn không theo tiếng nói | Dây quá dài; GND chưa chung |

---

## Giai đoạn 3 — Ampli MAX98357A + loa

Giai đoạn rủi ro nhất. **Tạm rút INMP441 ra** để test ampli một mình — tránh trường hợp lỗi nguồn làm hỏng luôn mic.

### 🔴 Ba điều tuyệt đối

1. **Không nối `SPK−` xuống GND.** Ngõ ra là BTL vi sai, nối đất một đầu sẽ chập và phá ampli.
2. **Đặt tụ 470–1000 µF** sát chân VIN/GND của ampli. Dòng đỉnh tới ~1 A, thiếu tụ sẽ sụt áp làm ESP32 reset giữa chừng.
3. **Không cho dòng loa chạy đường dài trên breadboard.** Dây nguồn ampli phải ngắn và to hơn dây tín hiệu.

### Đấu nối

| MAX98357A | Board | Ghi chú |
|---|---|---|
| VIN | **5V** | rail 5 V, chừa ≥1 A |
| GND | GND | chung với board |
| BCLK | **GPIO41** | |
| LRC | **GPIO42** | |
| DIN | **GPIO21** | dữ liệu vào ampli |
| SD/MODE | để trống hoặc kéo 3V3 | trống = bật; kéo GND = tắt tiếng |
| GAIN | để trống | mặc định 9 dB |
| SPK+ / SPK− | loa 3W 8Ω | 🔴 không đầu nào chạm GND |

### Test an toàn: vặn nhỏ trước

Lần chạy đầu, để `amplitude` thấp (giá trị 2000 trong code dưới). Nghe được rồi hãy tăng dần — tránh dòng đỉnh đột ngột khi chưa chắc nguồn đủ khỏe.

### Code test

```cpp
// Test 3: MAX98357A — phat tone 440 Hz
#include <ESP_I2S.h>
#include <math.h>

I2SClass i2s;
const int SR = 16000;

void setup() {
  Serial.begin(115200);
  // setPins(bclk, ws, dout, din, mclk)
  i2s.setPins(41, 42, 21, -1, -1);
  if (!i2s.begin(I2S_MODE_STD, SR,
                 I2S_DATA_BIT_WIDTH_16BIT,
                 I2S_SLOT_MODE_MONO)) {
    Serial.println("I2S begin THAT BAI");
    while (1) delay(100);
  }
  Serial.println("Phat tone 440 Hz...");
}

void loop() {
  const int N = 256;
  int16_t buf[N];
  static float ph = 0;
  const float amp = 2000;              // bat dau nho, tang dan sau
  const float step = 2 * PI * 440 / SR;

  for (int i = 0; i < N; i++) {
    buf[i] = (int16_t)(sinf(ph) * amp);
    ph += step;
    if (ph > 2 * PI) ph -= 2 * PI;
  }
  i2s.write((uint8_t *)buf, sizeof(buf));
}
```

### ✅ Đạt khi

Nghe rõ **một nốt liên tục, đều, không rè**, và board **không tự reset**.

| Triệu chứng | Nguyên nhân |
|---|---|
| Board reset khi có tiếng | Nguồn 5 V yếu hoặc thiếu tụ bulk |
| Tiếng rè/méo | Amplitude quá cao; hoặc GND chung kém |
| Im lặng hoàn toàn | `SD/MODE` bị kéo xuống GND; sai chân DIN |
| Tiếng rất nhỏ | Loa nối sai cực, hoặc GAIN cấu hình thấp |

---

## Giai đoạn 4 — Tích hợp full-duplex

Cắm lại INMP441 (giữ nguyên ampli). Lúc này `GPIO41` và `GPIO42` **nối tới cả hai module cùng lúc** — đây là điểm mấu chốt của cách B.

### Sơ đồ chung clock

```
GPIO41 (BCLK) ──┬── INMP441.SCK
                └── MAX98357A.BCLK

GPIO42 (WS)  ──┬── INMP441.WS
                └── MAX98357A.LRC

GPIO47 ──────────── INMP441.SD    (vao ESP32)
GPIO21 ──────────── MAX98357A.DIN (ra tu ESP32)
GPIO1  ──────────── MKE-M02.SIG
```

Cả hai module nghe chung một nguồn clock do ESP32 phát ra. Chỉ ESP32 là master — không module nào tự phát clock, nên không tranh chấp.

### Bài test tích hợp: vọng âm

Đọc từ mic rồi phát thẳng ra loa. Nếu nghe được tiếng mình nói ra loa thì cả chuỗi đã thông.

Cấu hình I2S phải bật **đồng thời TX và RX trên cùng `I2S_NUM_0`**. Nếu `ESP_I2S` của core 3.3.10 không hỗ trợ, chuyển sang `driver/i2s_std.h` của ESP-IDF: tạo `i2s_chan_handle_t` cho cả tx và rx từ cùng một `i2s_chan_config_t`, rồi gọi `i2s_channel_init_std_mode()` cho từng chiều với **cùng cấu hình clock và cùng chân**.

### 🔴 Chống hú (feedback)

Mic và loa gần nhau + vọng âm = **hú rít rất to**, có thể hỏng loa và chói tai.

Trước khi chạy test này:
- Đặt loa **quay hướng khác**, cách mic càng xa càng tốt
- Giữ amplitude thấp
- Chuẩn bị sẵn tay để rút nguồn

An toàn hơn: **thu 3 giây rồi mới phát lại** thay vì vọng âm trực tiếp. Code dưới đây làm đúng như vậy — không có đường vọng âm trực tiếp nên không thể hú.

### Code test

`ESP_I2S` của core Arduino chỉ mở được một chiều mỗi lần, nên giai đoạn này dùng thẳng driver ESP-IDF. Điểm mấu chốt: **`i2s_new_channel()` gọi một lần, trả về cả `tx` và `rx` trên cùng `I2S_NUM_0`**, rồi nạp **cùng một `i2s_std_config_t`** cho cả hai chiều — đó là cái làm hai module dùng chung BCLK/WS mà không tranh chấp.

```cpp
// Test 4: Full-duplex — nhan nut, thu 3 giay, phat lai
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include <math.h>

#define PIN_BCLK    GPIO_NUM_41
#define PIN_WS      GPIO_NUM_42
#define PIN_DOUT    GPIO_NUM_21     // ra ampli
#define PIN_DIN     GPIO_NUM_47     // vao tu mic
#define BTN         1
#define BTN_ACTIVE  LOW             // doi theo ket qua Test 1

const int    SR     = 16000;
const int    SECS   = 3;
const size_t N_SAMP = (size_t)SR * SECS;

i2s_chan_handle_t tx = NULL, rx = NULL;
int16_t *rec = NULL;

void setup() {
  Serial.begin(115200);
  pinMode(BTN, INPUT_PULLUP);       // dung INPUT neu module co tro keo san

  rec = (int16_t *)heap_caps_malloc(N_SAMP * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  if (!rec) {
    Serial.println("Khong cap phat duoc buffer PSRAM");
    while (1) delay(100);
  }

  // MOT port, hai chieu -> full-duplex, chung clock
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.auto_clear = true;             // luc khong phat thi day 0, tranh u ri
  ESP_ERROR_CHECK(i2s_new_channel(&cc, &tx, &rx));

  i2s_std_config_t sc = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SR),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                  I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = PIN_BCLK,
      .ws   = PIN_WS,
      .dout = PIN_DOUT,
      .din  = PIN_DIN,
      .invert_flags = { false, false, false },
    },
  };
  sc.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;   // INMP441 co L/R noi GND

  // CUNG mot cau hinh cho ca hai chieu — bat buoc
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &sc));
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx, &sc));
  ESP_ERROR_CHECK(i2s_channel_enable(tx));
  ESP_ERROR_CHECK(i2s_channel_enable(rx));

  beep();          // kiem tra chieu TX ngay, doc lap voi mic
  Serial.println("San sang. Nhan nut de thu 3 giay.");
}

// Phat 0.4 s tone 440 Hz. Nghe duoc = chuoi TX (ampli + loa) da thong.
void beep() {
  const int N = 256;
  int16_t buf[N];
  float ph = 0;
  const float step = 2 * PI * 440 / SR;
  Serial.println(">>> BIP kiem tra loa...");
  for (int r = 0; r < SR * 4 / 10 / N; r++) {
    for (int i = 0; i < N; i++) {
      buf[i] = (int16_t)(sinf(ph) * 6000);
      ph += step;
      if (ph > 2 * PI) ph -= 2 * PI;
    }
    size_t n;
    i2s_channel_write(tx, (uint8_t *)buf, sizeof(buf), &n, portMAX_DELAY);
  }
}

void loop() {
  if (digitalRead(BTN) != BTN_ACTIVE) { delay(20); return; }
  delay(50);                                   // debounce

  Serial.println(">>> DANG THU...");
  size_t got = 0, n;
  while (got < N_SAMP * sizeof(int16_t)) {
    esp_err_t e = i2s_channel_read(rx, (uint8_t *)rec + got,
                                   N_SAMP * sizeof(int16_t) - got, &n,
                                   1000 / portTICK_PERIOD_MS);
    if (e != ESP_OK) {                          // KHONG nuot loi
      Serial.printf("LOI read: %s\n", esp_err_to_name(e));
      break;
    }
    got += n;
  }

  // Do muc de biet mic co thuc su thu duoc gi khong
  long sum = 0;
  int16_t peak = 0;
  size_t cnt = got / sizeof(int16_t);
  for (size_t i = 0; i < cnt; i++) {
    sum += abs(rec[i]);
    if (abs(rec[i]) > peak) peak = abs(rec[i]);
  }
  long avg = cnt ? sum / (long)cnt : 0;
  Serial.printf("Thu xong %u mau, trung binh = %ld, dinh = %d\n",
                (unsigned)cnt, avg, peak);

  // INMP441 ra rat nho o che do 16-bit -> phai KHUECH DAI truoc khi phat.
  // Tu dong chinh sao cho dinh len ~8000, gioi han he so toi da 64 lan.
  int gain = peak > 0 ? 8000 / peak : 1;
  if (gain < 1)  gain = 1;
  if (gain > 64) gain = 64;
  Serial.printf("He so khuech dai = %d\n", gain);
  for (size_t i = 0; i < cnt; i++) {
    int32_t v = (int32_t)rec[i] * gain;
    rec[i] = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);   // chan clip
  }

  Serial.println(">>> DANG PHAT LAI...");
  unsigned long t0 = millis();
  size_t put = 0;
  while (put < got) {
    esp_err_t e = i2s_channel_write(tx, (uint8_t *)rec + put, got - put, &n,
                                    1000 / portTICK_PERIOD_MS);
    if (e != ESP_OK) {
      Serial.printf("LOI write: %s (moi phat %u/%u byte)\n",
                    esp_err_to_name(e), (unsigned)put, (unsigned)got);
      break;
    }
    put += n;
  }
  Serial.printf("Phat %u byte trong %lu ms (dung phai ~%d ms)\n",
                (unsigned)put, millis() - t0, SECS * 1000);

  Serial.println("Xong. Nha nut ra.");
  while (digitalRead(BTN) == BTN_ACTIVE) delay(20);
}
```

> ⚠️ Chưa chạy thử trên phần cứng. Nếu `i2s_new_channel()` trả về lỗi `ESP_ERR_NOT_FOUND`, port đã bị chiếm — kiểm tra xem sketch còn gọi `ESP_I2S` ở đâu không.

### Đọc kết quả: lỗi nằm ở chiều nào

Ba dữ kiện — **tiếng bíp**, **con số `dinh`**, **thời gian phát** — tách bạch được toàn bộ các khả năng:

| Hiện tượng | Kết luận |
|---|---|
| Không nghe bíp lúc khởi động | Hỏng chiều **TX**, chưa liên quan gì tới mic. Quay lại Giai đoạn 3 |
| Có bíp, `dinh` ≈ 0 | Hỏng chiều **RX**. Sai `slot_mask`, sai chân DIN, hoặc mic mất nguồn |
| Có bíp, `dinh` > 500, phát ~3000 ms | Chuỗi đã thông — nếu vẫn nhỏ thì tăng trần `gain` lên 128 |
| Phát xong tức thì (≪ 3000 ms) | DMA không hề chạy — xem dòng `LOI write` in ra mã gì |

Mốc `dinh`: nói bình thường cách mic ~20 cm nên cho **trên 1000**. Dưới 100 là coi như mic không thu được gì.

### ✅ Đạt khi

Nghe lại được giọng mình, board không reset, camera vẫn stream bình thường.

---

## Giai đoạn 5 — Gộp với camera

Cuối cùng mới thêm code audio vào sketch camera.

Điểm cần theo dõi:

| Rủi ro | Cách xử lý |
|---|---|
| **Hết RAM** | Camera đã dùng nhiều PSRAM. Giảm `fb_count` hoặc `frame_size` nếu I2S không cấp phát được buffer |
| **Tổng dòng vượt nguồn** | Camera ~200 mA + ampli tới 1 A + WiFi đỉnh. Nguồn 5 V phải chịu **≥2 A** |
| **Tranh chấp CPU** | Web server và I2S nên ở hai task khác nhau; cân nhắc ghim I2S vào core 1 |
| **Nhiễu vào mic** | Dây I2S không chạy song song dây loa và dây camera |

### Nguyên tắc gộp

Không đụng vào `loop()`. Audio chạy trong **task FreeRTOS riêng, ghim vào core 1**, để web server và camera giữ nguyên hành vi cũ. Nếu nhét I2S vào `loop()`, mỗi lần thu 3 giây là stream đứng hình 3 giây.

Giữ nguyên sketch chính gần như hoàn toàn — chỉ thêm 2 dòng. Toàn bộ phần audio nằm ở tab mới. (Lúc này file còn tên `CameraWebServer.ino`; nay là [your-eyes-esp32-firmware.ino](your-eyes-esp32-firmware.ino).)

### File mới: `audio.ino`

> Ở **Giai đoạn 6** file này sẽ đổi tên thành `audio.cpp` vì lý do biên dịch. Giai đoạn 5 thì `.ino` vẫn chạy tốt — cứ làm theo mục này trước.

Tạo tab mới trong Arduino IDE (`Ctrl+Shift+N`), đặt tên `audio`, dán code này. Cùng thư mục nên IDE tự biên dịch chung.

> 🔴 **Không `#include "audio.ino"`** ở bất cứ đâu. Arduino gộp sẵn mọi tab `.ino` cùng thư mục thành một file (file chính trước, các tab sau theo alphabet); include thêm sẽ khiến code xuất hiện hai lần → lỗi `redefinition`. Thứ cần thiết duy nhất là dòng khai báo `bool startAudio();` ở mục dưới — vì `audio.ino` nối vào *sau*, chỗ gọi trong `setup()` phải biết trước hàm này.

```cpp
// Giai doan 5: audio chay song song camera, task rieng tren core 1
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include <math.h>

#define PIN_BCLK    GPIO_NUM_41
#define PIN_WS      GPIO_NUM_42
#define PIN_DOUT    GPIO_NUM_21
#define PIN_DIN     GPIO_NUM_47
#define BTN         1
#define BTN_ACTIVE  LOW

static const int    SR     = 16000;
static const int    SECS   = 3;
static const size_t N_SAMP = (size_t)SR * SECS;

static i2s_chan_handle_t tx = NULL, rx = NULL;
static int16_t *rec = NULL;

static void audioTask(void *arg) {
  for (;;) {
    if (digitalRead(BTN) != BTN_ACTIVE) {
      vTaskDelay(20 / portTICK_PERIOD_MS);      // vTaskDelay, KHONG delay()
      continue;
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);

    Serial.println(">>> DANG THU (camera van phai chay)...");
    size_t got = 0, n;
    while (got < N_SAMP * sizeof(int16_t)) {
      esp_err_t e = i2s_channel_read(rx, (uint8_t *)rec + got,
                                     N_SAMP * sizeof(int16_t) - got, &n,
                                     1000 / portTICK_PERIOD_MS);
      if (e != ESP_OK) { Serial.printf("LOI read: %s\n", esp_err_to_name(e)); break; }
      got += n;
    }

    int16_t peak = 0;
    size_t cnt = got / sizeof(int16_t);
    for (size_t i = 0; i < cnt; i++) if (abs(rec[i]) > peak) peak = abs(rec[i]);

    int gain = peak > 0 ? 8000 / peak : 1;
    if (gain < 1)  gain = 1;
    if (gain > 64) gain = 64;
    for (size_t i = 0; i < cnt; i++) {
      int32_t v = (int32_t)rec[i] * gain;
      rec[i] = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
    }
    Serial.printf("Thu %u mau, dinh = %d, gain = %d\n", (unsigned)cnt, peak, gain);

    Serial.println(">>> DANG PHAT LAI...");
    unsigned long t0 = millis();
    size_t put = 0;
    while (put < got) {
      esp_err_t e = i2s_channel_write(tx, (uint8_t *)rec + put, got - put, &n,
                                      1000 / portTICK_PERIOD_MS);
      if (e != ESP_OK) { Serial.printf("LOI write: %s\n", esp_err_to_name(e)); break; }
      put += n;
    }
    Serial.printf("Phat %u byte trong %lu ms | heap con %u | PSRAM con %u\n",
                  (unsigned)put, millis() - t0,
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());

    while (digitalRead(BTN) == BTN_ACTIVE) vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

// Goi sau startCameraServer(). Tra ve false neu khong du RAM.
bool startAudio() {
  pinMode(BTN, INPUT_PULLUP);

  Serial.printf("Truoc khi cap phat audio: heap %u, PSRAM %u\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());

  rec = (int16_t *)heap_caps_malloc(N_SAMP * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  if (!rec) {
    Serial.println("HET PSRAM — giam fb_count hoac frame_size cua camera");
    return false;
  }

  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.auto_clear = true;
  if (i2s_new_channel(&cc, &tx, &rx) != ESP_OK) {
    Serial.println("i2s_new_channel that bai");
    return false;
  }

  i2s_std_config_t sc = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SR),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                  I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = PIN_BCLK,
      .ws   = PIN_WS,
      .dout = PIN_DOUT,
      .din  = PIN_DIN,
      .invert_flags = { false, false, false },
    },
  };
  sc.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &sc));
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx, &sc));
  ESP_ERROR_CHECK(i2s_channel_enable(tx));
  ESP_ERROR_CHECK(i2s_channel_enable(rx));

  // Ghim core 1: core 0 dang lo WiFi/TCP. Priority 1 = duoi httpd.
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, NULL, 1, NULL, 1);

  Serial.printf("Audio san sang: heap %u, PSRAM %u. Nhan nut de thu.\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  return true;
}
```

### Sửa sketch chính — đúng 2 dòng

Khai báo cạnh hai dòng khai báo sẵn có ở đầu file:

```cpp
void startCameraServer();
void setupLedFlash();
bool startAudio();              // THEM
```

Rồi gọi ngay sau `startCameraServer()`:

```cpp
  startCameraServer();
  startAudio();                 // THEM

  Serial.print("Camera Ready! Use 'http://");
```

Giữ nguyên `loop()` với `delay(10000)` — audio không dùng tới nó.

### Cách chạy test

1. Nạp, mở Serial, chờ có IP.
2. **Mở stream trong trình duyệt trước**, để hình chạy liên tục.
3. Vừa nhìn stream vừa nhấn nút. Nói 3 giây, nghe phát lại.

Điều cần quan sát: **stream có khựng trong lúc thu/phát không.**

| Hiện tượng | Nguyên nhân |
|---|---|
| Log "HET PSRAM" | Giảm `fb_count` xuống 1, hoặc `frame_size` xuống SVGA |
| Stream đứng hình đúng lúc thu/phát | Task chưa được ghim đúng core, hoặc còn dùng `delay()` thay `vTaskDelay()` |
| Board reset khi phát | Nguồn — camera + WiFi + ampli vượt 2 A. Đây là lỗi nguồn, không phải code |
| Thu được nhưng lẫn tiếng rít đều | Nhiễu từ dây camera/XCLK. Tách dây I2S ra xa, thêm GND riêng |
| `heap con` tụt dần sau mỗi lần nhấn | Rò bộ nhớ — báo lại, buffer đang cấp phát một lần nên không được tụt |

Con số `heap con` in ra sau mỗi lần nhấn chính là phép kiểm rò bộ nhớ: nhấn 5–10 lần, giá trị phải **đứng yên**.

### ✅ Đạt khi

Stream chạy mượt **không khựng** trong suốt lúc thu và phát, nghe lại được giọng, board không reset, `heap con` không tụt sau nhiều lần nhấn.

---

## Giai đoạn 6 — Pipeline hoàn chỉnh: chụp + thu → server → phát

Đây là chức năng thật của sản phẩm. Bốn giai đoạn trước chỉ là chứng minh từng khối chạy được; giai đoạn này nối chúng thành một luồng.

### Luồng chạy — làm gì, ở host nào

```
[Người dùng]        [ESP32-S3 — task "audio", core 1]   [api.visioncare-host.uk]

  bấm giữ ──────►  1. esp_camera_fb_get()
                      chụp JPEG NGAY khoảnh khắc bấm
                      copy ra PSRAM, trả fb lại cho stream

                   2. i2s_channel_read() vòng lặp
   (đang giữ)         thu PCM 16 kHz mono vào PSRAM
                      mỗi vòng đọc ~32 ms rồi xem nút còn giữ không

  thả ra ───────►  3. dừng thu, chuẩn hoá biên độ
                      bọc PCM vào header WAV 44 byte

                   4. POST multipart/form-data ──────────►  nhận ảnh + tiếng
                      api.visioncare-host.uk/process        xử lý (ASR/LLM/TTS)
                      field "image" = capture.jpg
                      field "audio" = record.wav
                                     ◄────────────────────  trả về file .wav

                   5. đọc thân trả về vào PSRAM
                      duyệt khối RIFF, lấy PCM
                      đổi sample rate I2S nếu cần

  nghe loa ◄─────  6. i2s_channel_write() ra MAX98357A
                      xong thì trả I2S về 16 kHz cho lần thu sau
```

Trong suốt sáu bước, **core 0 vẫn chạy WiFi + httpd**, nên trình duyệt xem stream không bị khựng.

### 🔴 Ảnh là JPEG, không phải PNG
Đề ghi "định dạng pnj (chuẩn thu của camera)" — hai vế này mâu thuẫn nhau. Chuẩn thu của OV2640/OV3660 là **JPEG**; sensor nén JPEG ngay trong chip. Muốn ra PNG thì ESP32-S3 phải giải nén JPEG → RGB (một ảnh UXGA = 4.6 MB RAM) rồi nén lại PNG bằng zlib — mất nhiều giây và gần như chắc chắn hết RAM khi camera đang stream.

Code dưới đây gửi **JPEG** (`Content-Type: image/jpeg`, `filename="capture.jpg"`). Nếu server bắt buộc phải nhận PNG, việc chuyển đổi nên làm **ở phía server**, không phải trên board.

### Endpoint

```cpp
#define API_HOST    "api.visioncare-host.uk"
#define API_PATH    "/process"
#define API_PORT    443
```

**Đã POST thử thật từ máy tính bằng `curl`** — đây là hành vi đo được, không phải phỏng đoán:

| Hạng mục | Kết quả đo |
|---|---|
| Hạ tầng | Cloudflare (`104.21.67.134`, `172.67.176.233`, có cả AAAA) |
| Bắt tay TLS | 0.18–0.23 s, chứng chỉ hợp lệ |
| Nhận field `image` + `audio` | ✅ đúng tên, trả **200** |
| **Thời gian xử lý** | **~40 giây** (39.8 s / 41.8 s qua 2 lần đo) |
| Kiểu trả về | `Content-Type: audio/wav`, có `Content-Length` (**không** chunked) |
| Định dạng WAV | PCM 16-bit, **mono, 48000 Hz** |
| Kích thước | 177–184 KB ≈ 1.9 giây tiếng nói |
| Biên độ | đỉnh 14000–18000 — tiếng thật, đủ to, không cần khuếch đại thêm |

Bốn con số này đổi trực tiếp mấy chỗ trong code:

**1. Chờ ~40 giây, không phải vài giây.** Timeout ban đầu để 20 s là fail 100%. Nay:

```cpp
static const int NET_TIMEOUT_MS = 90000;   // rong gap doi thoi gian do duoc
```

Cloudflare tự ngắt ở 100 s (lỗi 524), nên 90 s là trần hợp lý. Về trải nghiệm: người dùng thả nút rồi phải đợi khoảng **40 giây** mới nghe được trả lời — đây là giới hạn của server, board không làm gì nhanh hơn được.

**2. WAV trả về ở 48 kHz.** Nghĩa là `setSampleRate()` chạy ở **mọi lần bấm**, không phải trường hợp hiếm. Mic thu 16 kHz, loa phát 48 kHz, mỗi vòng đổi clock hai lượt. MAX98357A chạy được 8–96 kHz nên không vấn đề, nhưng đây chính là lý do phải viết `setSampleRate()` tắt cả `tx` lẫn `rx` — nếu làm ẩu, lỗi sẽ hiện ra ngay lần bấm đầu tiên chứ không phải trường hợp hiếm gặp.

**3. 48 kHz ăn PSRAM gấp 3.** Trần `MAX_REPLY` nâng lên **2 MB** (≈ 21 giây ở 48 kHz mono; nếu để 1 MB thì chỉ được ~10 giây).

**4. Trả về có `Content-Length`.** Nhánh `chunked` trong code thành dự phòng — vẫn nên giữ, Cloudflare có thể đổi sang chunked bất cứ lúc nào mà không báo.

### 🔴 Không được gửi `Expect: 100-continue`

Server này **không trả lời `100 Continue`**. Bên gửi sẽ ngồi đợi cho tới khi timeout — lần test đầu tiên của tôi treo đúng 60 giây rồi chết vì lý do này.

Code trên board tự dựng header nên không dính. Nhưng khi bạn test bằng `curl`, nó **tự thêm** header đó cho mọi body lớn hơn 1 KB. Lệnh test đúng phải có `-H "Expect:"` để chặn:

```bash
curl -m 120 -H "Expect:" \
  -A "ESP32S3-VisionCare/1.0" \
  -F "image=@anh.jpg;type=image/jpeg;filename=capture.jpg" \
  -F "audio=@tieng.wav;type=audio/wav;filename=record.wav" \
  -o ketqua.wav -D - \
  https://api.visioncare-host.uk/process
```

Nếu bỏ `-H "Expect:"` mà thấy treo, đó không phải lỗi mạng.

### ⚠️ Ghi chú bảo mật: `setInsecure()`

Code dùng `client.setInsecure()` — bỏ qua kiểm tra chứng chỉ TLS. Kết nối vẫn được **mã hoá**, nhưng board **không xác minh** mình đang nói chuyện với đúng server nào, nên không chống được tấn công xen giữa.

Chấp nhận được khi đang dựng thử. Trước khi đưa vào dùng thật, thay bằng:

```cpp
static const char *ROOT_CA = R"(-----BEGIN CERTIFICATE-----
...chung chi goc Cloudflare dang dung cho api.visioncare-host.uk...
-----END CERTIFICATE-----)";

client.setCACert(ROOT_CA);      // thay cho setInsecure()
```

Lưu ý chứng chỉ gốc có hạn — hết hạn thì kết nối chết im lặng, cần cơ chế cập nhật.

### Định dạng gói tin gửi đi

Một request `multipart/form-data` duy nhất, hai phần:

| Field | Filename | Content-Type | Nội dung |
|---|---|---|---|
| `image` | `capture.jpg` | `image/jpeg` | JPEG nguyên bản từ sensor |
| `audio` | `record.wav` | `audio/wav` | WAV PCM 16-bit mono 16 kHz |

Điểm kỹ thuật quan trọng: multipart **bắt buộc khai `Content-Length` trước khi gửi byte đầu tiên**. Không thể vừa gửi vừa đếm. Vì vậy code dựng sẵn các chuỗi phân cách, cộng độ dài ảnh + độ dài audio + 44 byte header, rồi mới mở kết nối:

```cpp
size_t total = pImg.length() + jpgLen
             + pAud.length() + sizeof(wh) + pcmBytes
             + pEnd.length();
```

Sai một byte ở phép cộng này là server treo chờ dữ liệu cho tới khi timeout — lỗi rất khó đoán, nên đừng sửa phần multipart nếu không cần.

Cũng vì vậy mà **không dùng `HTTPClient`**: nó đòi toàn bộ thân request nằm trong một buffer liên tục. Ảnh 100 KB + tiếng 300 KB = 400 KB phải copy thêm một lần nữa. Ghi thẳng lên `WiFiClientSecure` thì ảnh và tiếng đi trực tiếp từ PSRAM ra socket.

### Server cần trả về gì

Thực tế đo được: HTTP **200**, `audio/wav` PCM 16-bit mono **48 kHz**, có `Content-Length`. Code chấp nhận rộng hơn thế để phòng server đổi:

- PCM **16-bit**, mono hoặc stereo, tần số bất kỳ.
- `Content-Length` hoặc `Transfer-Encoding: chunked`.
- Dưới **2 MB** (`MAX_REPLY`) — ở 48 kHz mono tương đương ~21 giây.

Code **không** hỗ trợ MP3 hay µ-law. Trả về định dạng khác thì Serial in `Chi ho tro PCM 16-bit` và không phát gì.

Tần số lấy từ khối `fmt ` rồi gọi `i2s_channel_reconfig_std_clock()`. Vì mic và ampli **dùng chung khối clock** (quyết định ở đầu tài liệu), phải tắt cả `tx` lẫn `rx` mới đổi được — hàm `setSampleRate()` làm đúng thứ tự đó, và phát xong thì trả về 16 kHz cho lần thu kế tiếp.

### Vì sao phải nạp trọn file rồi mới phát

Có thể phát thẳng từ socket ra I2S để tiết kiệm RAM. Đừng làm. WiFi trễ một nhịp là DMA hết dữ liệu → loa kêu "tách" rất khó chịu. Nạp hết vào PSRAM rồi mới phát thì phát liền mạch tuyệt đối, đổi lại tốn ≤1 MB PSRAM — vẫn còn dư.

### Ngân sách PSRAM

| Khoản | Kích thước |
|---|---|
| Buffer thu (10 s × 16 kHz × 2 byte) | 320 KB, cấp phát **một lần** lúc khởi động |
| Ảnh JPEG | 15–150 KB, cấp phát và giải phóng mỗi lần bấm |
| WAV trả về | ≤2 MB, cấp phát và giải phóng mỗi lần bấm (thực đo ~180 KB) |
| Camera fb (2 × UXGA) | ~400 KB |

Board có 8 MB PSRAM nên rất thoải mái. Con số `PSRAM` in ra sau mỗi lần bấm phải **trở về đúng giá trị cũ** — nếu tụt dần là có rò.

### 🔴 Đổi tên `audio.ino` → `audio.cpp`

Từ giai đoạn này, file audio **phải là `.cpp`**, không còn là tab `.ino` như GĐ 5 nữa. Đây không phải sở thích — để `.ino` thì **không biên dịch được**:

```
audio.ino:72:71: error: 'bool writeAll(WiFiClientSecure&, const uint8_t*, size_t)'
                        redeclared as different kind of entity
```

Lý do: Arduino tự sinh prototype cho mọi hàm trong tab `.ino` rồi **chèn lên đầu file gộp** — tức là trước dòng `#include <WiFiClientSecure.h>` nằm trong chính `audio.ino`. Tới lúc trình biên dịch đọc prototype `writeAll(WiFiClientSecure &c, ...)` thì kiểu `WiFiClientSecure` chưa tồn tại.

GĐ 5 không dính vì không hàm nào có tham số kiểu của thư viện ngoài. GĐ 6 có `writeAll()` và `readExact()` nên lộ ngay.

File `.cpp` nằm cùng thư mục sketch vẫn được biên dịch chung, nhưng Arduino **không đụng vào** — không chèn prototype. Đổi lại nó cũng không tự thêm `Arduino.h`, nên phải tự khai:

```cpp
#include <Arduino.h>     // bat buoc voi .cpp, .ino thi duoc chen san
```

Trong Arduino IDE: đóng sketch, đổi tên file trong Explorer từ `audio.ino` thành `audio.cpp`, mở lại. IDE sẽ hiện nó thành một tab riêng như cũ.

### Code

Ở GĐ 6 toàn bộ phần này nằm trong **một** file `audio.cpp` 2115 dòng. Sang [GĐ 7](#giai-đoạn-7--tách-module) nó đã được tách ra, nên bảng dưới ghi cả chỗ ở hiện tại:

| Việc | Hàm hiện tại | File |
|---|---|---|
| Chụp, copy ra PSRAM, **trả `fb` ngay** | `photoCapture()` | [src/camera/camera_capture.cpp](src/camera/camera_capture.cpp) |
| Thu từng miếng 32 ms, dừng khi nhả nút hoặc chạm trần 10 s | `recordWhileHeld()` | [src/audio/audio_recorder.cpp](src/audio/audio_recorder.cpp) |
| Lọc DC, chặn ồn, khuếch đại tự động (INMP441 ở 16-bit ra rất nhỏ) | `normalizeRecording()` | [src/audio/audio_dsp.cpp](src/audio/audio_dsp.cpp) |
| Dựng multipart, POST, đọc trả lời (cả `chunked`) | `apiSendCapture()` | [src/net/api_client.cpp](src/net/api_client.cpp) |
| Duyệt khối RIFF / đọc `x-audio-format` | `readWavHeader()`, `parseAudioFormat()` | [src/audio/audio_format.cpp](src/audio/audio_format.cpp) |
| Giải mã MP3 nếu server trả MP3 | `pcmRead()`, `mp3DecodeFrame()` | [src/audio/pcm_source.cpp](src/audio/pcm_source.cpp) |
| Trộn stereo→mono, đệm thích ứng, phát | `playPcmStream()` | [src/audio/audio_player.cpp](src/audio/audio_player.cpp) |
| Đổi clock cho **cả** `tx` và `rx` — bắt buộc vì full-duplex | `i2sSetSampleRate()` | [src/audio/audio_i2s.cpp](src/audio/audio_i2s.cpp) |
| Nối 6 bước, chạy trên core 1 | `audioTask()`, `sendAndPlay()` | [src/audio/audio_service.cpp](src/audio/audio_service.cpp) |
| Khởi tạo I2S + tạo task | `startAudio()` | [src/audio/audio_service.cpp](src/audio/audio_service.cpp) |

Sketch chính **không cần sửa gì thêm** — vẫn chỉ gọi `startAudio()` một dòng như GĐ 5.

Vài chỗ dễ bỏ sót nếu bạn tự viết lại:

**Chụp trước, thu sau.** Đề bài yêu cầu ảnh đúng khoảnh khắc bấm. Chụp sau khi thu xong là ảnh của lúc *thả* nút — sai lệch cả vài giây.

**Trả `fb` ngay sau khi copy.** Camera chỉ có 2 frame buffer. Giữ một cái suốt 5 giây thu + gửi là stream đứng hình.

**`client.write()` có thể ghi thiếu.** Khi buffer TLS đầy, nó trả về số byte nhỏ hơn yêu cầu chứ không lỗi. Không lặp thì gói tin cụt, server treo chờ. Đây là lý do có `writeAll()`.

**Stack task 12 KB.** Bắt tay TLS của mbedtls một mình đã ăn ~8 KB stack. Giữ nguyên 4096 của GĐ 5 là stack overflow ngay lần POST đầu tiên — board reset kèm `***ERROR*** A stack overflow in task audio has been detected`.

**Nhấn dưới 300 ms bị bỏ qua** (`MIN_MS`). Chạm nhầm không nên tốn một request.

### 🔴 Bẫy timeout của `WiFiClientSecure` ở core 3.x

Đây là chỗ dễ sai nhất, và sai thì **không có thông báo lỗi rõ ràng** — chỉ thấy `Server:` in ra rỗng. Đã đối chiếu trực tiếp với header của core 3.3.10 trên máy bạn:

| Hàm | Đơn vị | Ảnh hưởng tới |
|---|---|---|
| `setConnectionTimeout(ms)` | **mili giây** | `connect()`, `read()`, `write()` ở mức socket |
| `setTimeout(ms)` | **mili giây** | các hàm của `Stream` — ở đây là `readStringUntil()` |

Ba điều dễ nhầm:

**1. `setTimeout()` giờ tính bằng mili giây, không phải giây.** Ở core 2.x nó là `setTimeout(uint32_t seconds)`. Sang 3.x, `NetworkClient` không còn override nữa nên nó rơi về `Stream::setTimeout(unsigned long ms)`. Viết `client.setTimeout(90)` theo thói quen cũ là đặt timeout **90 mili giây**, trong khi phải chờ 40 giây.

**2. Phải gọi cả hai.** `setConnectionTimeout()` không đụng tới timeout của `Stream`. Thiếu `setTimeout()` thì `readStringUntil('\n')` bỏ cuộc sau **1 giây** mặc định, ngay ở dòng đọc `HTTP/1.1 200 OK`.

**3. `connect(host, port, timeout)` ghi đè `_timeout`.** Code dùng 15 s cho lúc kết nối để mạng chết thì biết ngay, nhưng vì vậy **phải đặt lại `setConnectionTimeout()` ngay sau khi connect thành công**:

```cpp
client.setTimeout(NET_TIMEOUT_MS);                  // Stream, truoc khi connect
if (!client.connect(API_HOST, API_PORT, 15000)) ... // 15 s cho rieng luc bat tay
client.setConnectionTimeout(NET_TIMEOUT_MS);        // noi lai NGAY sau connect
```

Đảo thứ tự ba dòng này là hỏng, mà biểu hiện lại giống hệt lỗi mạng.

### Biên dịch — đã kiểm chứng

Đã build sạch trên máy bạn bằng `arduino-cli` **bundle sẵn trong Arduino IDE** (`E:\Download\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe`), core esp32 **3.3.10**:

```bash
arduino-cli compile --clean --warnings all \
  --fqbn "esp32:esp32:esp32s3:PSRAM=opi,USBMode=default,PartitionScheme=huge_app,FlashMode=qio,FlashSize=16M" \
  e:/VisionCare
```

```
Sketch uses 1228830 bytes (39%) of program storage space. Maximum is 3145728 bytes.
Global variables use 70252 bytes (21%) of dynamic memory, leaving 257428 bytes...
```

**Không lỗi, không cảnh báo.** Hai chỉ số cần để ý:

- **Flash 39%** — thoải mái, nhưng phải chọn `PartitionScheme` có ≥3 MB app. Để `default` (1.2 MB) là **không nạp nổi**, IDE báo `text section exceeds available space`.
- **RAM tĩnh 21%**, còn 258 KB cho biến cục bộ. Stack 12 KB của task audio lấy từ đây, vẫn dư nhiều.

Trong Arduino IDE, chọn tương ứng ở menu **Tools**: Board `ESP32S3 Dev Module`, PSRAM `OPI PSRAM`, Partition Scheme `Huge APP (3MB No OTA/1MB SPIFFS)`, Flash Size `16MB`.

### Cách chạy test

1. Nạp, mở Serial 115200, chờ có IP.
2. **Mở stream trên trình duyệt trước** — để quan sát xem có khựng không.
3. Giữ nút, nói 2–3 giây, thả ra.

Serial phải in ra đúng chuỗi này:

```
===== BAM NUT =====
Chup xong: 320x240, 14832 byte
Thu 2380 ms, 76160 byte
Dinh truoc khi khuech dai = 1240, he so = 6
Ket noi api.visioncare-host.uk:443 ...
Gui 91384 byte (anh 14832 + am 76204)...
Gui xong trong 840 ms, dang cho server xu ly (~40 s)...
Server: HTTP/1.1 200 OK
Nhan 184364 byte trong 41230 ms
WAV: fmt=1 ch=1 48000 Hz 16-bit, PCM 184320 byte
Doi I2S sang 48000 Hz
>>> DANG PHAT...
Phat 184320 byte trong 1923 ms (du kien ~1920 ms)
Xong. heap 142xxx, PSRAM 6xxxxxx
```

Ba con số cần soi: **thời gian thu** phải khớp thời gian bạn giữ nút; **`Phat ... trong X ms`** phải xấp xỉ `du kien`; **`PSRAM`** phải giống hệt lần bấm trước.

### Bảng lỗi

| Serial in ra | Nguyên nhân | Xử lý |
|---|---|---|
| `KHONG ket noi duoc` | Bắt tay TLS fail, hoặc hết heap | Xem `heap` in lúc khởi động; cần dư ≥60 KB cho mbedtls |
| `KHONG ket noi duoc` mà máy tính vào được | Router chặn, hoặc DNS của mạng WiFi khác | Chạy lại `nslookup` **trên mạng board đang dùng** |
| `Server tra ma 403` kèm nội dung HTML | Cloudflare chặn (bot protection / WAF) | Nhờ phía server whitelist `User-Agent: ESP32S3-VisionCare/1.0` |
| `Server tra ma 502/524` | Server gốc sau Cloudflare chết hoặc xử lý quá 100 s | Lỗi phía server, không phải board |
| Treo ~20 s rồi `Server:` rỗng | `Content-Length` sai, server còn chờ byte | Không sửa phần multipart; nếu đã sửa thì hoàn nguyên |
| `Server tra ma 400` | Server không nhận tên field `image`/`audio` | Hỏi phía API tên field đúng, sửa trong `pImg`/`pAud` |
| `Server tra ma 413` | Ảnh + tiếng quá lớn | Giảm `MAX_SECS`, hoặc hạ `frame_size` xuống VGA |
| `Tra ve khong phai file WAV` | Server trả JSON/lỗi thay vì audio | In 100 byte đầu của `reply` ra để xem thực tế là gì |
| `Chi ho tro PCM 16-bit` | Server trả MP3 hoặc µ-law | Yêu cầu server trả WAV PCM 16-bit |
| `Nhan thieu du lieu` | Kết nối rớt giữa chừng | Tăng `NET_TIMEOUT_MS` (mặc định 90000) |
| `Server:` in ra **rỗng**, chỉ sau ~1 s | Thiếu `client.setTimeout()` | Xem mục bẫy timeout ở trên — cả hai hàm đều tính bằng **mili giây** |
| `Server:` rỗng sau đúng 15 s | Quên đặt lại `setConnectionTimeout()` sau `connect()` | Thứ tự ba dòng phải đúng như mục trên |
| Treo đúng 60–90 s rồi bỏ cuộc | Có gửi `Expect: 100-continue` | Server không trả `100` — bỏ header đó |
| Board reset lúc POST | Stack task quá nhỏ | Kiểm tra đúng 12288 ở `xTaskCreatePinnedToCore` |
| Stream khựng lúc bấm | `fb` chưa được trả sớm | `esp_camera_fb_return()` phải nằm ngay sau `memcpy` |
| `PSRAM` tụt dần mỗi lần bấm | Rò — thiếu `free()` ở một nhánh thoát | Soi các nhánh `return false` trong `sendAndPlay()` ([audio_service.cpp](src/audio/audio_service.cpp)): mỗi nhánh phải gọi `mp3Free()`/`apiClose()` trước khi thoát, và `photoRelease()` phải chạy ở cuối mọi vòng của `audioTask()` |

### ✅ Đạt khi

Giữ nút → nghe tiếng loa trả lời trong vòng vài giây, stream camera **không khựng** suốt quá trình, `PSRAM` không tụt sau 10 lần bấm liên tiếp, board không reset.

---

## Giai đoạn 7 — tách module

Tới cuối GĐ 6, code chạy được nhưng dồn vào ba file: `CameraWebServer.ino` 578 dòng, `audio.cpp` 2115 dòng, `app_httpd.cpp` 853 dòng. `audio.cpp` một mình ôm I2S, thu mic, lọc tín hiệu, chụp ảnh, HTTP client, giải mã MP3, phát loa và cả năm lệnh chẩn đoán — sửa một chỗ là phải đọc lại cả file để chắc không đụng chỗ khác.

GĐ 7 **không đổi một dòng logic nào**, chỉ chia lại: mỗi `.cpp` một việc, và mỗi file cung cấp hàm cho file khác gọi qua `.h` của nó.

### Cấu trúc hiện tại

Gốc thư mục = những gì bạn sửa. `src/` = mã.

```
your-eyes-esp32-firmware.ino   tên + mật khẩu Wi-Fi, rồi setup() + loop()
app_config.h        endpoint API, timeout mạng, chân GPIO, tần số thu
board_config.h      chọn model camera
camera_pins.h       sơ đồ chân camera
src/
```

| Thư mục | File | Việc |
|---|---|---|
| `src/camera/` | `camera_device` | dựng `camera_config_t`, init/reinit, in thông tin |
| | `camera_tuning` | profile cảm biến, warm-up, in setting |
| | `camera_capture` | chụp loạt + chọn khung nét, đo độ nét, đèn chiếu sáng |
| `src/net/` | `wifi_manager` | kết nối Wi-Fi, tự kiểm tra đường mạng |
| | `http_body_reader` | đọc thân HTTP (`chunked` / `Content-Length`) |
| | `api_client` | POST multipart ảnh + tiếng, đọc header trả về |
| `src/web/` | `web_server` | web server xem trực tiếp (từ `app_httpd.cpp`) |
| | `web_led` | LED flash onboard điều khiển qua web |
| `src/audio/` | `audio_i2s` | kênh I2S, đổi sample rate, bơm im lặng |
| | `audio_buffers` | `recBuf[]` và `stageBuf[]` |
| | `audio_recorder` | thu mic |
| | `audio_dsp` | lọc DC, cổng chặn ồn, chuẩn hoá |
| | `audio_format` | header WAV + `x-audio-format` |
| | `pcm_source` | nguồn PCM thô / giải mã MP3 |
| | `audio_player` | phát ra loa, đệm thích ứng, chống hụt |
| | `audio_service` | **điều phối**: nút → ảnh → tiếng → server → loa |
| `src/diag/` | `serial_console` | phân phối lệnh `c/m/t/w/n` |
| | `audio_diag`, `camera_diag` | các phép đo chẩn đoán |
| `src/util/` | `mem_alloc` | `bigAlloc()` ưu tiên PSRAM |

### Vì sao `src/` chứ không để phẳng

Arduino biên dịch **đệ quy** mọi `.c`/`.cpp` trong thư mục `src/` của sketch, nhưng **không** vào các thư mục con khác. Đó là lý do `src/libhelix-mp3/` có sẵn từ GĐ 6 vẫn build được, và cũng là lý do các module mới phải nằm trong `src/`.

Ba file cấu hình vẫn ở gốc vì đó là thứ bạn sửa thường xuyên nhất — và `.ino` thì Arduino bắt buộc phải nằm ở gốc, trùng tên thư mục.

### Lý do `.cpp` ở GĐ 6 vẫn còn nguyên giá trị

Cái bẫy prototype đã mô tả ở GĐ 6 giờ áp dụng cho **mọi** file trong `src/`: chúng đều là `.cpp` nên Arduino không chèn prototype vào, và đều phải tự `#include <Arduino.h>`.

### ✅ Đạt khi

Biên dịch sạch, và hành vi lúc chạy **giống hệt** GĐ 6: cùng thứ tự log ra Serial, cùng các lệnh chẩn đoán, cùng cách phản ứng khi mất Wi-Fi hay rút cáp camera.

---

## Bảng tổng hợp: dừng lại ở đâu

Không sang giai đoạn sau khi giai đoạn trước chưa đạt.

| GĐ | Điều kiện qua |
|----|---------------|
| 0 | Stream hình ở UXGA, `Get Still` chụp được |
| 1 | Serial in `0 ↔ 1` ổn định theo nút |
| 2 | Mức âm tăng khi nói, về 0 khi im |
| 3 | Tone 440 Hz rõ, board không reset |
| 4 | Nghe lại được giọng, không hú |
| 5 | Camera + audio chạy đồng thời ≥5 phút không reset |
| 6 | Giữ nút → nghe câu trả lời từ server, stream không khựng, PSRAM không tụt |
| 7 | Biên dịch sạch sau khi tách module, hành vi lúc chạy không đổi so với GĐ 6 |

---

## Ghi chú độ tin cậy

- Pinout camera và danh sách GPIO trống: lấy từ [GOOUUU_ESP32-S3-CAM](https://github.com/profharris/GOOUUU_ESP32-S3-CAM), đã đối chiếu khớp với [camera_pins.h](camera_pins.h) và **đã kiểm chứng thực tế** ở GĐ 0.
- Các đoạn code test **chưa chạy trên phần cứng** — cần đối chiếu với ví dụ đi kèm core esp32 3.3.10.
- Mức SIG của MKE-M02 và dòng đỉnh thực tế của MAX98357A: **phải đo**, không có datasheet đầy đủ.
- Giai đoạn 6: **phía server đã kiểm chứng thật** bằng `curl` từ máy tính — endpoint nhận đúng field `image`/`audio`, trả 200 kèm WAV PCM 16-bit mono 48 kHz, mất ~40 s. Các con số timeout và `MAX_REPLY` trong code lấy từ phép đo này.
- **Đã biên dịch sạch** bằng `arduino-cli` 1.5.1, core esp32 3.3.10, FQBN `esp32s3:PSRAM=opi,PartitionScheme=huge_app`: không lỗi, không cảnh báo từ mã dự án, flash 39%, RAM tĩnh 21%. Con số này là của bản **sau khi tách module** ở GĐ 7 (bản gộp một file ở GĐ 6 là 36%).
- **Phần chạy trên board thì chưa nạp thử.** Biên dịch được không có nghĩa là chạy đúng. Ba chỗ chưa kiểm chứng, nhiều khả năng phải chỉnh nhất khi nạp lần đầu: bắt tay TLS của mbedtls trên ESP32 (khác thư viện với curl), stack 12 KB có đủ không, và việc đổi clock I2S 16 kHz ↔ 48 kHz mỗi lần bấm.
