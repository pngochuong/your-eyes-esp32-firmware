# Lắp và unit test module 4G MKE-M21 (SIM7680C) với ESP32-S3

Mục tiêu: **chứng minh module 4G sống, đăng ký được mạng, gửi được SMS và gọi được điện thoại — trước khi động tới mic/loa/ampli.** Đúng nguyên tắc bring-up của [HUONG_DAN_LAP_TUNG_BUOC.md](HUONG_DAN_LAP_TUNG_BUOC.md): mỗi lần chỉ thêm một linh kiện.

Sketch test đi kèm: [test/Test_MKE_M21/Test_MKE_M21.ino](test/Test_MKE_M21/Test_MKE_M21.ino) — độc lập hoàn toàn, không dùng camera, không dùng I2S, không đụng gì tới firmware chính.

**Nguồn thông số:** [trang sản phẩm hshop](https://hshop.vn/mke-m21-sim768x-4g-sms-call-iot-module) và [github.com/makereduvn/MKE-M21-SIM7680C-4G-MODULE](https://github.com/makereduvn/MKE-M21-SIM7680C-4G-MODULE).

---

## 0. Thông số chốt

| Hạng mục | Giá trị |
|---|---|
| Chip | **A7680C-LANS** (SIMCom), LTE Cat-1 — hshop ghi "SIM7680C" nhưng `ATI` trả về A7680C ✅ đã xác minh trên board thật |
| Firmware đo được | `22124B02A7680M6A` |
| Bộ lệnh AT | họ **A76XX** |
| Nguồn **khối SIM** | **3.7 – 4.0 VDC** ⚠️ |
| Nguồn **khối cấp nguồn** (bán rời) | 5 – 24 VDC |
| Dòng | ~300 mA trung bình, **đỉnh tức thời tới 1 A** khi gọi/SMS |
| UART | TTL, **đã có level shifter tích hợp**, tương thích 3.3 V và 5 V |
| Baudrate mặc định | **9600 bps** |
| SIM | **Nano SIM 4G, phải có VoLTE (HD Voice)** |
| Chân khối SIM | TX, RX, 4V, GND, MIC+/−, SP+/−, NET (LED trạng thái), RST, ATN (ăng-ten) |
| Chân khối cấp nguồn | GND, 5V, TX, RX |
| Cáp | 4P XH2.54 → Dupont |
| Không có | PWRKEY, chân PCM số |

Ba con số in đậm ở trên là ba thứ dễ làm hỏng buổi test nhất. Đọc tiếp mục 1 và 2.

---

## 1. ⚠️ Nguồn — đọc kỹ, đây là chỗ làm cháy module

**Khối SIM chỉ chịu 3.7 – 4.0 VDC.** Đây không phải module 5 V.

> 🔴 **Cắm thẳng 5 V vào chân 4V của khối SIM là hỏng module.** Không có regulator trên khối SIM để bảo vệ — đó chính là lý do MakerEDU bán "khối cấp nguồn" riêng.

Bạn đang có bản nào? Trang hshop bán ba kiểu:

| Bạn mua | Cấp nguồn thế nào |
|---|---|
| Khối SIM + khối cấp nguồn (290k) | ✅ Cấp 5 V vào **khối cấp nguồn**, khối này hạ áp và chuyển tiếp cả UART sang khối SIM |
| Chỉ khối SIM (245k) | ⚠️ Phải tự lo nguồn **3.7 – 4.0 V, chịu được đỉnh 1 A**. Pin Li-ion 1 cell (3.7 V) là hợp nhất. Hoặc mua thêm khối cấp nguồn (45k) |

**Nếu chỉ có khối SIM và không có nguồn 3.7–4.0 V đúng chuẩn: dừng lại, mua khối cấp nguồn trước.** Đừng "thử tạm 5 V xem sao" — không có đường về.

Các điểm còn lại:

- **Tuyệt đối không lấy từ chân 3.3 V của board ESP32-S3.** Regulator 3.3 V trên board không gánh nổi, và điện áp cũng sai (khối cấp nguồn cần ≥ 5 V).
- **Chân 5 V của board thì có ngoại lệ** — xem mục 1.1 ngay dưới.
- **Bắt buộc chung GND** giữa ESP32 và module. Không chung GND thì UART là rác ngẫu nhiên.
- **Nên thêm tụ bulk 470–1000 µF** ngay tại chân nguồn của khối SIM. Đỉnh dòng của LTE là xung rất ngắn (cỡ ms), dây nguồn không kịp gánh nhưng tụ thì kịp.
- **Dây nguồn ngắn và dày.** Jumper breadboard mảnh dài 20 cm đủ để sụt vài trăm mV lúc đỉnh dòng.

### 1.1 — Bỏ adapter riêng, dùng chung nguồn USB của ESP32?

Được **cho giai đoạn unit test**, không được cho bản lắp hoàn chỉnh. Lý do là phép cộng dòng, không phải nguyên tắc.

**Lúc test** (sketch `Test_MKE_M21` — không chạy camera, không chạy I2S, không bật Wi-Fi):

| Món | Dòng |
|---|---|
| ESP32-S3 chạy sketch test, radio tắt | ~50–100 mA |
| Module 4G | ~300 mA, đỉnh 1 A |
| **Tổng** | **~400 mA, đỉnh ~1.1 A** |

Một củ sạc điện thoại 5 V / 2 A gánh được mức này. **Ba điều kiện bắt buộc:**

1. **Củ sạc rời ≥ 2 A. Không dùng cổng USB laptop** — cổng USB 2.0 chỉ cấp 500 mA, thiếu ngay từ mức trung bình.
2. **Tụ bulk 470–1000 µF** giữa `+` và `-`, càng gần khối cấp nguồn càng tốt. Đỉnh 1 A chỉ kéo dài vài trăm µs — tụ cấp tại chỗ được, cáp USB + trace trên board thì không.

   **Không có tụ vẫn test được**, đừng để nó chặn buổi test. Khối cấp nguồn là mạch buck nên đã có sẵn tụ hóa 100–220 µF ở đầu vào/ra; tụ bạn thêm là **bổ sung**. Và đỉnh 1 A là số xấu nhất của nhà sản xuất — sóng tốt thì module phát công suất thấp, đỉnh thực tế nhỏ hơn. T1–T5 gần như chắc chắn qua vì đều chỉ phát từng nhịp ngắn; chỉ T6 mới thực sự thử nguồn. Thiếu tụ không phá gì, triệu chứng là reset ở T6 — đúng thứ bài test sinh ra để bắt.

   **Bốn thứ miễn phí thay được phần lớn tác dụng của tụ**, xếp theo hiệu quả: ① dùng adapter riêng thay vì chung rail với ESP32 — quan trọng nhất, vì tách ra thì module tụt áp cũng chỉ mình nó chịu, ESP32 không reset và bạn còn log để đọc ② dây nguồn ngắn và dày ③ cắm `+`/`-` thẳng vào khối cấp nguồn, tránh đi vòng qua breadboard (mỗi lỗ là một tiếp xúc ~10–50 mΩ) ④ củ sạc ≥ 2 A.

   **Chọn trị số thế nào.** Cap gánh phần dòng nguồn chưa kịp cấp: `ΔV = I × t / C`. Quy về phía 5 V, đỉnh 1 A ở 3.9 V ≈ 0.85 A; trừ 300 mA nguồn gánh sẵn còn ~0.6 A trong ~200 µs. Ngưỡng chịu được là ~0.3 V (khối cấp nguồn phải giữ trên ~4.2 V mới đẻ ra nổi 3.9 V):

   | Cap | Sụt áp | |
   |---|---|---|
   | 100 µF | ~1.2 V | ❌ rail tụt còn 3.8 V, buck mất dropout |
   | 470 µF | ~0.26 V | ✅ |
   | 1000 µF | ~0.12 V | ✅ |

   Còn một yếu tố ăn nhiều hơn cả điện dung: **ESR**. Tụ hóa 100 µF có ESR ~0.5–1 Ω, nhân 0.85 A ra 0.4–0.85 V sụt tức thời — riêng nó đã tiêu hết ngân sách. Tụ to thì ESR thấp, nên 1000 µF thắng gấp đôi.

   **Chỉ có tụ 100 µF?** Ghép **song song 5 con**: điện dung cộng thẳng (500 µF) và ESR chia 5, tốt hơn một con 470 µF đơn lẻ. Chọn loại **≥ 10 V**, đừng dùng 6.3 V cho rail 5 V. Có đúng một con thì cứ cắm và chạy test — sai ở đây không phá gì, triệu chứng là reset ở T6, đúng thứ bài test sinh ra để bắt.
3. **Đo trước.** Cắm USB, đo giữa chân 5V và GND của board ESP32-S3: phải ra 4.8–5.2 V. Một số board có diode/polyfuse nối tiếp trên đường VBUS làm sụt áp hoặc chặn dòng ở ~500 mA — đo mới biết board bạn thuộc loại nào.

**Lúc lắp hoàn chỉnh** (camera ~200 mA + Wi-Fi đỉnh ~300 mA + ampli tới ~1 A + module 4G đỉnh 1 A) thì tổng đỉnh vượt xa mọi cổng USB. Lúc đó **phải** tách nguồn. Nhưng đó là chuyện của giai đoạn sau — đừng để nó chặn buổi test hôm nay.

**Dấu hiệu nguồn không đủ, nhận ra ngay:** ESP32 reset đúng lúc chạy T6 (gọi đi), hoặc log lặp lại banner khởi động của module. Thấy một trong hai thì dừng, cắm adapter riêng cho module.

> Nếu không muốn mua adapter bàn: một **cáp USB cũ cắt đầu** hoặc **module USB breakout ra 2.54** (~10–20 k) cắm vào củ sạc là đủ, cho module một đường nguồn độc lập hoàn toàn. Rẻ hơn và chắc chắn hơn mọi cách chia sẻ rail.

Triệu chứng nguồn yếu — nhận ra để khỏi đi tìm nhầm trong code:

| Bạn thấy | Thật ra là |
|---|---|
| `AT` trả OK bình thường, nhưng `AT+CEREG?` mãi ở stat=2 | Sụt áp khi bật sóng phát |
| ESP32 tự reset đúng lúc bấm gọi | Đỉnh dòng kéo tụt cả rail chung |
| Log lặp lại banner khởi động module | Module tự reset vì under-voltage |
| SMS gửi được, cuộc gọi thì rớt | Cuộc gọi phát sóng liên tục; SMS chỉ phát một nhịp ngắn |

Test T8 (`AT+CBC`) đo đúng chuyện này. Chạy nó **trong lúc đang gọi**, không phải lúc rảnh — sketch có sẵn đường làm việc đó (bấm `v` trong lúc T6 đang chạy).

---

## 2. ⚠️ SIM phải bật VoLTE

Nhà sản xuất ghi rõ: **"SIM phải được đăng ký hòa mạng 4G và dịch vụ VoLTE (HD Voice) trước khi sử dụng."**

Lý do: module này là LTE Cat-1, **không có đường lùi về 2G**, mà Việt Nam đã tắt sóng 2G. Nên cuộc gọi thoại bắt buộc đi qua VoLTE. SIM không bật VoLTE thì:

- SMS vẫn gửi/nhận được ✅
- Mạng vẫn đăng ký được ✅
- **Cuộc gọi luôn thất bại** ❌ — thường trả `NO CARRIER` gần như tức thì

Đây là bẫy khó chịu vì mọi thứ khác trông vẫn ổn. Test **T3b** trong sketch (`AT+CIREG?` / `AT+CAVIMS?`) là để bắt trường hợp này trước khi bạn ngồi mò phần cứng.

**Cách bật VoLTE:** lắp SIM vào một điện thoại có hỗ trợ VoLTE, bật mục "VoLTE / Cuộc gọi HD" trong cài đặt mạng, gọi thử một cuộc. Nếu không lên thì gọi tổng đài nhà mạng yêu cầu kích hoạt HD Voice cho thuê bao. Xong mới lắp lại vào module.

Ngoài ra SIM phải là **Nano SIM**, đã tắt mã PIN, còn hạn, còn tiền.

---

## 3. Chọn chân GPIO và nối dây

Board VisionCare còn rất ít chân. Kiểm kê hiện tại (theo [app_config.h](app_config.h) và [camera_pins.h](camera_pins.h)):

| Đã dùng | Chân |
|---|---|
| Camera | 4–13, 15–18 |
| I2S (mic + ampli) | 21, 41, 42, 47 |
| Nút nhấn | 1 |

Còn trống và an toàn: `2, 14, 38, 39, 40, 48`.
Tránh: `0, 3, 45, 46` (strapping — sai mức lúc boot là board không nạp được), `19, 20` (USB-JTAG).
Lưu ý: `38, 39, 40` là chân thẻ SD, `48` là NeoPixel onboard — dùng được, đổi lại là bỏ hai thứ đó.

### 3.1 — Bốn dây của khối cấp nguồn

Khối cấp nguồn có một cáp 4 dây, ký hiệu trên board là **`R`, `T`, `+`, `-`**. Đây là **toàn bộ** giao diện ra ngoài — bạn chỉ làm việc với 4 dây này, không đụng gì tới khối SIM nữa.

| Ký hiệu | Là gì | Nối vào đâu |
|---|---|---|
| **`+`** | Nguồn vào 5–24 VDC | Cực **+** nguồn ngoài, **hoặc** chân 5V của ESP32 (xem mục 1.1) |
| **`-`** | GND | Cực **−** nguồn ngoài **VÀ** chân GND của ESP32 |
| **`R`** | **R**X — module *nhận* | GPIO **2** của ESP32 (chân ESP32 *phát*) |
| **`T`** | **T**X — module *phát* | GPIO **14** của ESP32 (chân ESP32 *nhận*) |

> 🔴 **`R` và `T` là tên chức năng của MODULE, không phải của ESP32.** Module nhận ở `R` nên phải nối vào chân phát của ESP32. Chéo nhau. Nối thẳng `T`↔TX là lỗi số 1 khi bắt tay UART thất bại.

**Dây `-` phải đi tới HAI chỗ**, không phải một. Đây là chỗ hay sót: nguồn cấp cho module là nguồn ngoài, nhưng ESP32 lấy điện từ USB — hai mạch riêng. Không nối chung GND thì tín hiệu UART không có mốc so sánh và bạn nhận về rác ngẫu nhiên (hoặc không gì cả).

Cách làm gọn trên breadboard: cắm `-` vào **hàng ray GND**, rồi cắm cả cực − của nguồn ngoài lẫn chân GND của ESP32 vào cùng hàng ray đó. Ba điểm chụm về một nút.

**Cách 1 — nguồn riêng cho module** (bắt buộc ở bản lắp hoàn chỉnh):

```
   Adapter 5V ──┬────────────────────────►  +   (khối cấp nguồn)
                │
                └── (−) ──┐
                          ├── ray GND breadboard ──►  -   (khối cấp nguồn)
   ESP32 GND ─────────────┘

   ESP32 GPIO 2  (phát) ─────────────────►  R   (module nhận)
   ESP32 GPIO 14 (nhận) ◄─────────────────  T   (module phát)
```

**Cách 2 — chung nguồn USB của ESP32** (chỉ dùng khi test, đọc mục 1.1 trước):

```
   Củ sạc ≥2A ──USB──► ESP32-S3

   ESP32  5V  ──► ray + breadboard ──┬──►  +   (khối cấp nguồn)
                                     │
                                  [ tụ 470–1000µF ]   ← BẮT BUỘC ở cách này
                                     │
   ESP32  GND ──► ray - breadboard ──┴──►  -   (khối cấp nguồn)

   ESP32 GPIO 2  (phát) ─────────────────►  R   (module nhận)
   ESP32 GPIO 14 (nhận) ◄─────────────────  T   (module phát)
```

Ở cách 2, GND của ESP32 **chính là** cực − của nguồn — nên chỉ cần một jumper từ ray `-` sang chân GND của ESP32. Tụ cắm chân dài (+) vào ray `+`, chân ngắn (−, có vạch trắng) vào ray `-`. **Cắm ngược tụ hóa là nó nổ** — kiểm tra hai lần trước khi cấp điện.

Cách 2 cần **đủ 4 dây nguồn**, dễ sót hai dây cuối: khối cấp nguồn `-` → ray `-`, khối cấp nguồn `+` → ray `+`, **ESP32 `5V` → ray `+`**, **ESP32 `GND` → ray `-`**. Ray breadboard không tự có điện; nối module vào ray mới là một nửa việc.

> ⚠️ **Ray nguồn breadboard thường đứt ở giữa.** Nhiều breadboard 400 lỗ có khoảng hở trên vạch đỏ/xanh ở chính giữa — hai nửa **không** thông nhau. Cắm module ở nửa trên, ESP32 ở nửa dưới là không có điện, mà nhìn vẫn thấy "cùng một ray". Dò bằng chế độ thông mạch của đồng hồ, hoặc cắm cả 4 dây vào cùng một nửa.

> ⚠️ **Đúng chân `5V`, không phải `3.3V`.** Silkscreen có thể ghi `5V`, `VIN`, `VBUS`, `VUSB` — đều được. Nhầm sang `3.3V` thì buck không đẻ ra nổi 3.9 V (đầu vào thấp hơn đầu ra), và còn kéo quá tải LDO trên board.

**Mức logic không cần lo** — module đã có level shifter tích hợp, tương thích cả 3.3 V và 5 V. Nối `R`/`T` thẳng vào GPIO ESP32-S3 an toàn.

**Đừng nhận diện dây theo màu.** Màu cáp XH2.54 không có chuẩn chung, và cáp cắm được cả hai chiều. Nhìn **chữ in trên board** ngay cạnh từng chân của connector, rồi dò theo dây tương ứng.

### 3.2 — Thứ tự lắp (làm đúng thứ tự này)

Mỗi bước có một điểm kiểm chứng. Không bỏ bước nào, nhất là bước 3 — nó là lần cuối bạn còn cơ hội phát hiện nguồn sai trước khi khối SIM ăn điện.

**Bước 1 — Ghép hai khối, chưa cấp điện.**
Khối cấp nguồn nối sang khối SIM (board-to-board hoặc cáp ngắn, tùy bản bạn có). Cắm cáp 4 dây vào connector ngoài của **khối cấp nguồn**, đầu kia để hở.

**Bước 2 — Vặn ăng-ten vào chân ATN, lắp Nano SIM.**
Vặn chặt tay. Phát sóng khi không có ăng-ten thì công suất phản xạ ngược vào tầng khuếch đại. Để ăng-ten **xa camera, xa dây I2S** — sóng 4G phát cạnh cáp FPC camera là nguồn nhiễu thật, không phải lý thuyết.

**Bước 3 — Cấp nguồn cho RIÊNG module. Chưa nối dây tín hiệu.**
Chỉ nối `+` và `-` vào nguồn (adapter riêng, hoặc chân 5V/GND của ESP32 nếu đi theo cách 2 — lúc này chưa cắm `R`/`T`, ESP32 chỉ đóng vai nguồn).
- Đèn **NET** phải sáng hoặc nhấp nháy trong ~10 giây → khối cấp nguồn chạy đúng.
- Có đồng hồ thì đo giữa chân **4V** và **GND** của **khối SIM**: phải đọc được **3.7 – 4.0 V**. Đọc ra ~5 V nghĩa là khối cấp nguồn không hoạt động hoặc bạn đang cắm nhầm chỗ → **rút điện ngay**, đừng đi tiếp.
- Đèn không sáng gì → kiểm tra lại `+`/`-` có ngược không, adapter có điện không.

**Bước 4 — Rút điện. Nối dây tín hiệu.**
`-` → ray GND (chung với ESP32 GND) · `R` → GPIO 2 · `T` → GPIO 14.
Nối dây khi cả hai bên đều **không có điện**.

**Bước 5 — Cấp điện cho module TRƯỚC, cắm USB cho ESP32 SAU.**
Thứ tự này tránh việc chân phát của ESP32 đẩy dòng vào đầu vào của module đang tắt.

**Bước 6 — Nạp sketch, chạy test T1** (mục 4).

### 3.3 — Nếu T1 không bắt tay được

Trước khi đi tìm nguyên nhân phức tạp: **đảo hai dây `R` và `T` cho nhau rồi chạy lại T1.** Thao tác này an toàn — cả hai đều là chân tín hiệu qua level shifter, đảo nhầm không hỏng gì. Nếu tài liệu của tôi hiểu sai quy ước đặt tên của MakerEDU thì đây là cách sửa trong 10 giây, nhanh hơn mọi cách suy luận khác.

Đảo rồi vẫn không được thì mới đi theo bảng lỗi ở mục 5.

### 3.4 — Chân RST (tùy chọn)

Chân **RST** nằm trên **khối SIM**, không có trong cáp 4 dây. Chỉ nối khi module treo cứng và không trả lời lệnh AT nào nữa — nối vào GPIO 38, rồi đổi `M21_RST_PIN` trong sketch từ `-1` thành `38`. Lần lắp đầu **bỏ qua chân này**.

Sau khi thêm module 4G, chân còn lại: `39, 40, 48` (và `38` nếu không dùng RST). Đủ để sau này lắp đèn chiếu sáng vào `FLASH_LED_PIN`.

---

## 4. Nạp và chạy sketch test

### 4.1 — Mở sketch

Sketch nằm ở thư mục riêng nên **Arduino IDE không gộp nó vào firmware chính** (Arduino chỉ biên dịch đệ quy trong `src/`, không vào thư mục con khác ở gốc).

```
File → Open → e:\VisionCare\test\Test_MKE_M21\Test_MKE_M21.ino
```

Board: `ESP32S3 Dev Module`. Các thiết lập PSRAM/Partition không quan trọng ở sketch này (chương trình rất nhỏ), cứ để nguyên cấu hình của VisionCare cho khỏi phải đổi qua đổi lại khi quay về firmware chính.

Serial Monitor: **115200 baud**, line ending **"Both NL & CR"** hoặc **"New Line"** (để "No line ending" thì các bước hỏi số điện thoại sẽ không nhận được Enter).

> **Thư viện `MKE_ONE` của MakerEDU:** có bản mẫu chạy sẵn (Library Manager → `MKE_ONE`, ví dụ ở File → Examples → MKE_ONE → Module → M21_SIM7680C). Sketch này **không dùng** thư viện đó — nó nói chuyện AT thô, vì phần tích hợp vào VisionCare sau này cũng sẽ phải là AT thô. Nhưng nếu test T1 thất bại và bạn nghi ngờ chính sketch, nạp thử ví dụ của MKE_ONE làm đối chứng độc lập là cách kiểm tra chéo tốt.

### 4.2 — Baudrate: 9600, không phải 115200

Module mặc định **9600 bps**, và nhà sản xuất khuyến cáo không nâng lên 115200. Sketch đặt `M21_BAUD = 9600` và có sẵn cơ chế tự quét nếu module đã bị đổi baud từ trước.

Cảnh báo của MakerEDU nhắm vào `SoftwareSerial` trên Arduino AVR (bit-bang, dễ sai khung ở tốc độ cao). ESP32-S3 dùng UART **phần cứng** nên chạy được cao hơn. Nhưng **đừng nâng ở giai đoạn test** — test là để biết module có sống không, không phải để tối ưu tốc độ. Chuyện nâng baud để lát nữa, xem mục 7.

Sketch **cố ý không gọi `AT+IPR`**: lệnh đó ghi vào bộ nhớ module và giữ qua cả lần tắt nguồn — một lần gõ nhầm là lần sau không ai biết vì sao module câm.

### 4.3 — Thứ tự chạy test

Chạy **đúng thứ tự này**. Mỗi bước dựa trên bước trước; nhảy cóc thì lỗi ở bước sau không truy ngược được.

| Bước | Phím | Kiểm chứng điều gì | Đạt khi |
|---|---|---|---|
| **T1** | `i` | Dây UART đúng, module bật, đúng chip | `ATI` in ra tên model + IMEI |
| **T2** | `s` | Khe SIM đọc được thẻ | `+CPIN: READY` |
| **T3** | `s` | Ăng-ten + sóng + đăng ký mạng | `+CEREG: 0,1` (hoặc `0,5`), `AT+COPS?` ra tên nhà mạng |
| **T3b** | `e` | **VoLTE — điều kiện sống còn của cuộc gọi** | `+CIREG`/`+CAVIMS` trả về `1` |
| **T8** | `v` | Nguồn đủ khỏe | `+CBC` trong dải 3.7–4.0 V, **và không tụt khi đang gọi** |
| **T4** | `m` | Đường gửi tin nhắn | Máy khác nhận được SMS |
| **T5** | `r` | Đường nhận tin nhắn | Nhắn tin vào SIM → thấy `+CMT:` trong log |
| **T6** | `c` | Cuộc gọi đi | Máy kia **đổ chuông** |
| **T7** | `n` | Cuộc gọi đến | Gọi vào SIM → thấy `RING` + `+CLIP:` |
| **T9** | `l` | Âm lượng loa / độ nhạy mic | *(chỉ sau khi đã hàn mic + loa)* |
| **T0** | `b` | Gõ lệnh AT tay để mò lỗi | *(dùng khi cần)* |

Phím `1` chạy liền T1 → T2 → T3 → T3b, tiện cho lần bật máy đầu.
Phím `a` nhận máy, `h` ngắt máy, `?` hiện lại menu.

> **T6/T7 chỉ kiểm chứng phần báo hiệu (signalling), không kiểm chứng âm thanh.** Máy kia đổ chuông là đạt. Chưa nghe được tiếng ở bước này là **đúng như dự kiến** nếu bạn chưa hàn mic/loa vào chân MIC±/SP± — xem mục 6.

---

## 5. Bảng lỗi thường gặp

| Triệu chứng | Nguyên nhân, theo thứ tự khả năng |
|---|---|
| T1: quét hết baud không thấy gì | **① vừa chạy sketch PPP xong → module còn ở chế độ dữ liệu, im lặng ở mọi baud, nhìn y hệt đứt dây.** T1 tự gửi `+++` rồi quét lại nên thường tự khỏi; nếu không, bấm `x`. ② TX/RX chưa chéo ③ chưa chung GND ④ nguồn sai (xem mục 1) ⑤ đèn NET không sáng = module chưa chạy |
| T1: ra ký tự rác | Sai baud (để sketch tự quét), hoặc dây UART quá dài/nhiễu |
| T2: `SIM not inserted` | ① **cắm SIM khi module đang chạy** — module chỉ đọc SIM lúc boot, phải tắt nguồn rồi bật lại ② sai chiều (mặt vàng úp xuống board, góc vát khớp hình vẽ) ③ khay chưa khóa/chưa "tách" ④ không phải Nano SIM nguyên bản ⑤ chân tiếp xúc bẩn ⑥ SIM chết — thử trong điện thoại |
| T2: `SIM PIN` | SIM còn khóa PIN — tắt PIN bằng điện thoại |
| T3: RSSI = 99 mãi | ① radio đang tắt (`AT+CFUN?` ≠ 1) ② ăng-ten chưa vặn chặt vào chân ATN |
| **T3: stat = 0** | **"không đăng ký và KHÔNG tìm mạng"** — radio nằm im, không phải sóng yếu. Theo thứ tự: `AT+CFUN?` phải = 1 → ăng-ten → `AT+COPS=?` xem có nhìn thấy nhà mạng nào không. T3 tự thử `CFUN=1`/`CNMP=2`/`COPS=0` rồi mới kết luận |
| T3: stat = 2 kéo dài | Sóng yếu (ra gần cửa sổ) hoặc **nguồn yếu** — chạy T8 song song |
| T3: stat = 3 (bị từ chối) | SIM hết hạn/chưa kích hoạt/bị khóa mạng — không phải lỗi phần cứng |
| **T3b: `+CIREG: 0,0`** | **SIM chưa bật VoLTE — sẽ không gọi được. Xem mục 2** |
| T4: không thấy dấu nhắc `>` | Số điện thoại sai định dạng — dùng dạng quốc tế `+84...` |
| T4: gửi xong không có `+CMGS` | SIM hết tiền, hoặc sóng tụt đúng lúc gửi |
| T6: `NO CARRIER` ngay lập tức | ① VoLTE chưa bật (khả năng cao nhất) ② số sai ③ SIM chưa có gói thoại |
| T6: ESP32 reset khi bấm gọi | **Nguồn.** Xem lại mục 1, thêm tụ bulk |
| T6: đổ chuông nhưng không nghe gì | Bình thường nếu chưa hàn mic/loa vào MIC±/SP± — xem mục 6 |
| Log lặp banner khởi động module | Module tự reset vì under-voltage |

Khi bí, dùng chế độ cầu nối `b` để gõ lệnh AT tay. Hai lệnh đáng gõ đầu tiên:

```
AT+CMEE=2      → từ đây module báo lỗi bằng chữ thay vì mã số
AT+CPSI?       → một dòng đủ cả: hệ mạng, band LTE, mức thu, chất lượng
```

Tài liệu tập lệnh đầy đủ: bộ **AT Command A76XX** (link trên trang hshop).

---

## 6. Âm thanh cuộc gọi — kết hợp với mic/loa hiện tại thế nào

Đây là phần trả lời trực tiếp câu hỏi của bạn, và có một tin không vui.

**INMP441 và MAX98357A là thiết bị I2S — chúng nói chuyện SỐ với ESP32. Module này chỉ đưa ra đường audio ANALOG: `MIC+/MIC-` và `SP+/SP-`.** Không có cách nào nối thẳng ba món với nhau.

Và khối SIM **không đưa ra chân PCM số** — chỉ có TX, RX, 4V, GND, MIC±, SP±, NET, RST, ATN. Đã kiểm chứng thêm ở mức firmware: **`AT+CPCMREG=?` trả `ERROR`** trên bản V11.0.01 này, tức module không mở giao tiếp PCM số. Phương án "bắc cầu PCM số qua ESP32 để dùng chung một mic một loa" vì vậy **không khả thi** — không phải suy đoán nữa mà là kết quả đo.

Vậy còn hai lối:

### Cách A — hai bộ tai-miệng riêng ✅ khuyến nghị

Hàn thêm **một mic electret vào MIC+/MIC−** và **một loa nhỏ vào SP+/SP−** (module có ampli trong, không cần MAX98357A cho đường này).

- Cuộc gọi chạy **hoàn toàn trong module**. ESP32 chỉ ra lệnh `ATD` / `ATA` / `ATH`.
- Trợ lý AI vẫn dùng INMP441 + MAX98357A qua I2S như hiện tại, không đụng gì.
- Chỉnh mức: `AT+CLVL=<n>` (âm lượng loa), `AT+CMIC=...` (độ nhạy mic) — test T9 in ra dải giá trị hợp lệ của bản firmware bạn đang có.

**Đánh đổi:** tốn thêm một mic + một loa, tốn chỗ trong vỏ. Đổi lại là chạy được ngay và **không phá gì đang chạy**.

### Cách B — dùng chung một loa, chuyển mạch

Hai đường audio vào chung một loa qua relay hoặc IC chuyển mạch analog (ví dụ chuyển giữa ngõ ra MAX98357A và SP± của module). Mic thì vẫn phải hai cái vì INMP441 không có ngõ analog ra.

Tiết kiệm được một loa, nhưng thêm linh kiện chuyển mạch và thêm một trạng thái phải quản lý. **Không đáng làm ở bản đầu** — làm cách A trước, khi máy chạy ổn rồi mới tính gọn.

> **Thứ tự đề xuất:** chạy hết test trong tài liệu này với module trần (chưa mic/loa) → hàn mic + loa vào MIC±/SP± → chạy lại T6/T7 + T9 để chỉnh mức → rồi mới ghép chung vỏ với phần I2S.

---

## 7. Về việc thay Wi-Fi bằng 4G cho đường gọi server

Bạn nói muốn dùng 4G thay Wi-Fi. Làm được, nhưng **không phải đổi một dòng cấu hình** — nói trước để bạn tính lịch. Có ba rào cản, theo thứ tự nghiêm trọng:

**① Băng thông UART là nút cổ chai, không phải sóng 4G.** *(đã đo, không phải ước lượng)*

Đường data **chạy được**: `AT+CGATT? → 1`, `AT+IPADDR → 100.115.99.166`, `HTTP GET example.com → 200` chỉ sau **300 ms**. Sóng 4G nhanh. Vấn đề nằm ở sợi dây UART:

| | byte/giây |
|---|---|
| **Đo thực tế ở 9600 baud** (`AT+HTTPREAD=0,512` → 954 ms) | **536** |
| Trần lý thuyết 9600 baud 8N1 | 960 |
| Trần lý thuyết 460800 baud | 46 080 |
| — | |
| Cần cho WAV 48 kHz 16-bit mono *(server đang trả)* | **96 000** |
| Cần cho WAV 16 kHz | 32 000 |
| Cần cho MP3 64 kbps | 8 000 |

Ở tốc độ hiện tại, tải 10 giây tiếng WAV 48 kHz mất **~30 phút**. Không phải "chậm" mà là sai bậc độ lớn ~180 lần.

→ Bắt buộc **hai** thay đổi, thiếu một cái là không đủ:
> 1. Nâng baud bằng `AT+IPR` lên 460800 (ESP32-S3 dùng UART phần cứng nên chịu được; cảnh báo 115200 của MakerEDU nhắm vào `SoftwareSerial` trên AVR).
> 2. Đổi server sang trả **MP3** — nhánh Helix đã có sẵn trong [pcm_source.cpp](src/audio/pcm_source.cpp). Ở 460800 baud, MP3 64 kbps chỉ cần 8 000 B/s trên trần 46 080 B/s: dư thoải mái. Còn WAV 48 kHz thì **kể cả 460800 baud vẫn không đủ**.

**② Kiến trúc streaming thì KHÔNG phải viết lại — nhờ PPP.** *(đã chạy được)*

Ban đầu tôi tưởng phải viết lại `api_client` theo lệnh AT (`AT+CCHOPEN`/`AT+CIPOPEN`). **Không cần.** Sketch [test/Test_MKE_M21_PPP](test/Test_MKE_M21_PPP/Test_MKE_M21_PPP.ino) đã chứng minh PPP chạy được trên module này: ESP32 nhận **IP riêng của chính nó** và dùng ngăn xếp lwIP của nó.

```
[PPP] DA CO IP
IP      : 100.115.99.166
Gateway : 10.64.64.64
DNS     : 10.53.120.254
DNS: example.com -> 172.66.147.243  (296 ms)
TCP bat tay xong sau 141 ms
HTTP/1.1 200 OK
```

DNS, socket TCP, HTTP — tất cả do **ESP32** làm, không phải module. Hệ quả: `NetworkClient` / `NetworkClientSecure` dùng bình thường, TLS do ESP32 lo, và **cơ chế vừa-nhận-vừa-phát của [audio_player.cpp](src/audio/audio_player.cpp) giữ nguyên**. Đổi Wi-Fi sang 4G về mặt mã nguồn chỉ là đổi cách dựng giao diện mạng, không phải viết lại tầng HTTP.

Hai điều kiện của cách này:
> - `PPP.begin()` **không** tự chuyển sang chế độ dữ liệu — phải gọi `PPP.mode(ESP_MODEM_MODE_DATA)` riêng. Thiếu bước này thì modem nằm ở chế độ lệnh và `PPP.connected()` vĩnh viễn false, nhìn ra ngoài giống hệt "mạng không lên".
> - PDP context phải là **IPv4 thuần**. Mạng cấp sẵn kiểu `IPV4V6`; ép về bằng `AT+CGDCONT=1,"IP","m-wap"` (phím `p` trong sketch AT).

**③ Ảnh JPEG gửi lên cũng đi qua cùng đường UART đó** — cùng chung trần thông lượng ở bảng trên.

**Đề xuất thứ tự:** làm phần SMS + cuộc gọi trước (giá trị mới, không phá gì đang chạy) → rồi nâng baud + đổi server sang MP3 → **rồi mới** chuyển đường data sang PPP. Giữ Wi-Fi làm đường chính trong lúc đó; hai đường sống song song được.

---

## 8. Bảng lệnh AT tra nhanh

| Lệnh | Việc |
|---|---|
| `AT` | bắt tay, phải trả `OK` |
| `ATE0` | tắt echo (log dễ đọc hơn nhiều) |
| `AT+CMEE=2` | báo lỗi bằng chữ thay vì mã số |
| `ATI` / `AT+CGMR` / `AT+GSN` | model / firmware / IMEI |
| `AT+CPIN?` | trạng thái SIM (`READY` là được) |
| `AT+CSQ` | mức sóng; `dBm = -113 + 2×rssi`, 99 = chưa đo được |
| `AT+CEREG?` | đăng ký LTE: stat 1 = mạng nhà, 5 = roaming, 3 = bị từ chối |
| `AT+COPS?` | tên nhà mạng đang bám |
| `AT+CPSI?` | một dòng đủ cả: hệ mạng, band, mức thu, chất lượng |
| `AT+CIREG?` / `AT+CAVIMS?` | **đăng ký IMS / thoại VoLTE có sẵn không** |
| `AT+CBC` | điện áp nguồn module |
| `AT+CMGF=1` | SMS chế độ text |
| `AT+CNMI=2,2,0,0,0` | đẩy thẳng tin nhắn đến ra UART |
| `AT+CMGS="+84..."` | gửi SMS — chờ dấu `>`, gõ nội dung, kết thúc bằng Ctrl+Z (0x1A) |
| `ATD+84...;` | gọi thoại (**dấu `;` bắt buộc**, thiếu nó là cuộc gọi data) |
| `ATA` / `ATH` | nhận máy / ngắt máy |
| `AT+CLIP=1` | hiện số gọi đến |
| `AT+CLCC` | trạng thái cuộc gọi hiện tại |
| `AT+COUTGAIN=<0-7>` | mức ra loa |
| `AT+CMICGAIN=<0-7>` | độ khuếch đại mic |
| `AT+CRSL=<0-100>` | âm lượng chuông |
| `AT+CSDVC=<1\|3>` | đường ra tiếng: 1 = handset, 3 = loa ngoài |
| ~~`AT+CLVL`~~ / ~~`AT+CMIC`~~ | ❌ **firmware này trả `ERROR`** — đó là lệnh của đời SIM800, đừng dùng |
| `AT+IPR=<baud>` | đổi baudrate — **ghi vĩnh viễn**, cân nhắc trước khi gõ |

---

## 9. Kết quả đo thực tế

Đo trên chính board này, ngày 2026-08-03, qua `arduino-cli` + điều khiển cổng Serial:

| Hạng mục | Đo được | |
|---|---|---|
| Model | `A7680C-LANS`, revision `V11.0.01` | ✅ |
| Firmware | `22124B02A7680M6A` | |
| Baud bắt tay | 9600 | ✅ đúng mặc định NSX |
| SIM | `+CPIN: READY`, ICCID `8984012507141987203` | ✅ |
| `AT+CFUN?` | 1 (radio bật) | ✅ |
| Sóng | `+CSQ: 17` ≈ **−79 dBm** | ✅ tốt |
| Nhà mạng | `+COPS: 0,2,"45201",7` → **MobiFone** (MCC 452 / MNC 01), LTE | ✅ |
| Chi tiết | `LTE,Online,452-01,EUTRAN-BAND3` | ✅ band 3 |
| **VoLTE** | `+CIREG: 1,1,15` và `+CAVIMS: 1` | ✅ **đã bật — gọi được** |
| Nguồn | `+CBC: 4.088V` (lúc rảnh) | ✅ trên dải 3.7–4.0 V |
| Audio | `COUTGAIN=4`, `CMICGAIN=4`, `CRSL=5`, `CSDVC=1` | |
| PCM số | `AT+CPCMREG=?` → `ERROR` | ❌ không có |
| **Data 4G** | `+CGATT: 1`, IP `100.115.99.166`, APN tự cấp `m-wap.mnc001.mcc452.gprs` | ✅ |
| HTTP | `GET http://example.com/` → **200**, 559 byte, 300 ms | ✅ |
| **PPP — ESP32 có IP riêng** | IP `100.115.99.166` · GW `10.64.64.64` · DNS `10.53.120.254` | ✅ |
| DNS + socket do ESP32 mở | `example.com → 172.66.147.243` (296 ms), TCP 141 ms, HTTP 200 | ✅ |
| **Thông lượng qua PPP** | **766 B/s** (20 012 byte / 26 099 ms) ở 9600 baud, trần 960 | ⚠️ xem mục 7 |

Nối dây `GPIO2→R`, `GPIO14→T`, GND chung: **đúng, đã kiểm chứng** — T1 bắt tay được ở lần đầu và lặp lại ổn định qua nhiều lần nạp.

**Còn chưa chắc:**

1. **`+CBC: 4.088V` mới là số lúc rảnh.** Con số dưới tải nặng (đang gọi) chưa đo được — cần chạy T6 rồi gõ `v` trong lúc cuộc gọi đang chạy.
2. **Cách nhấp nháy của đèn NET** là quy ước phổ biến của SIMCom nhưng chưa xác minh cho bản này. Dùng làm gợi ý, `AT+CEREG?` mới là câu trả lời thật.
3. **Kiểu chân MIC±/SP±** — pad hàn, header, hay JST. Ảnh hưởng tới việc cần hàn hay chỉ cần cắm.
4. **T4/T5 (SMS) và T6/T7 (cuộc gọi) chưa chạy** — cần số điện thoại thật.

Xong test, ghi kết quả từng bước vào [HUONG_DAN_LAP_TUNG_BUOC.md](HUONG_DAN_LAP_TUNG_BUOC.md) như một "Giai đoạn 8" — giữ đúng thói quen ghi số đo thật của dự án này.
