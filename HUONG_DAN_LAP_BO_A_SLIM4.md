# Hướng dẫn lắp BO A bản SLIM-4 — chỉ ghi những gì KHÁC bản đã chạy thật

| Mục | Nội dung |
|---|---|
| **Mã tài liệu** | YEV-HW-ASM-BOA4-001 |
| **Phiên bản** | 0.3 |
| **Trạng thái** | 🔴 **CHƯA LẮP LẦN NÀO** trên bo này |
| **Bo mạch** | `out/BO_A_SLIM4/BO_A_SLIM4.kicad_pcb` — **90 × 30 mm, bản ướm thử** |
| **Bản in 1:1 để ướm** | `out/BO_A_SLIM4/SLIM4_90x30_UOM_1-1.pdf` — in **100 % / Actual size** |
| **Cổng đã qua** | DRC 0 · unconnected 0 · parity 0 · ERC 0 · semantic 83-0-0 · silk 0/0 · U1 CSV 40/40 |
| **Nền tảng đã chạy thật** | [HUONG_DAN_LAP_TUNG_BUOC.md](HUONG_DAN_LAP_TUNG_BUOC.md) — GĐ 0–4 |
| **Bo nguồn đối ứng** | [HUONG_DAN_LAP_BO_B.md](HUONG_DAN_LAP_BO_B.md) |
| **Cập nhật** | 2026-08-17 |

> **File này KHÔNG thay thế** [HUONG_DAN_LAP_TUNG_BUOC.md](HUONG_DAN_LAP_TUNG_BUOC.md). File đó ghi những gì **đã chạy thật** trên breadboard (GĐ 0–4) và vẫn là nguồn sự thật của pinout. File này chỉ liệt kê **bốn chỗ bản SLIM-4 khác đi**, vì đúng bốn chỗ đó là nơi có thể làm hỏng đồ.

---

## Bốn thay đổi so với bản đã chạy

| # | Đổi gì | Vì sao |
|---|---|---|
| A1 | 🔴 **Loa: 3 W hộp cộng hưởng 31×70 → tròn trần Ø15, 8 Ω, 1 W** | Linh kiện mua mới 2026-08-14 |
| A2 | 🔴 **GAIN của U4: để thả (9 dB) → nối cứng vào VDD (3 dB)** | Hệ quả bắt buộc của A1 |
| A3 | **Nút SW2 giữ nguyên 3 pad, nhưng nay nhận cả nút 2 chân** | Wake-word bị đánh giá không khả thi |
| A4 | **Thêm J2** — 2 pad nhận 5 V từ BO B | Trước đây 5 V vào qua USB-C của U1, không có chỗ đấu BO B |

---

## 1. 🔴 Thay loa — đọc hết trước khi cắm nguồn

### Vấn đề: ampli mạnh hơn loa

| | Loa cũ | **Loa mới** |
|---|---|---|
| Công suất chịu được | 3 W | **1 W** |
| Trở kháng | 8 Ω | 8 Ω |
| Hình dạng | Có hộp cộng hưởng nhựa 31 × 70 | Tròn trần Ø15 |
| Đầu nối | Có sẵn PH2.0 | **Dây đồng trần** |

MAX98357A ở VDD 5 V vào tải 8 Ω đưa ra được **≈1.6 W** 📄 (con số 3.2 W trong datasheet là vào **4 Ω**, không phải 8 Ω). Loa cũ 3 W nên còn biên. Loa mới **1 W** thì ampli mạnh hơn loa **1.6 lần** — mở hết cỡ là đốt cuộn dây loa.

### Cách hãm: GAIN đã được nối sẵn trên bo

Bảng GAIN của MAX98357A:

| GAIN nối vào | Độ lợi |
|---|---|
| GND qua 100 k | 15 dB |
| GND trực tiếp | 12 dB |
| **Để thả** | **9 dB** ← cấu hình đã test ở GĐ 3, thời còn loa 3 W |
| VDD qua 100 k | 6 dB |
| **VDD trực tiếp** | **3 dB** ← bản SLIM-4 dùng cái này |

Bo SLIM-4 đã **nối cứng GAIN vào `+5V_SYS`** bằng đường đồng. Trần công suất rơi về **≈0.4 W**, dưới mức chịu đựng của loa. Silkscreen in `GAIN NOI VDD = 3dB`.

> 🔴 **Đừng cắt đường đó.** Người lắp sau nhìn thấy GAIN nối vào nguồn rất dễ tưởng là lỗi thiết kế rồi cắt đi "cho đúng datasheet". Cắt xong loa sẽ to hơn — và cháy. Muốn quay lại 9 dB thì phải có lý do, và phải đổi loa trước.

### Đấu dây loa

1. 🔴 **Tháo cầu đấu vít bắt dây loa trên module U4.** Con MAX98357A với board của nó chỉ dày ~4 mm; phần lớn trong 10 mm chiều cao đo được là cái cầu đấu đó. Tháo ra rồi hàn thẳng hai dây loa vào pad — **pod mỏng đi ~6 mm**, nhiều hơn mọi thứ khác cộng lại. 🔴 Từ 2026-08-16 **silkscreen không còn in `THAO CAU DAU U4`** — ba từ đó không nói được tháo cái gì, còn bước đầy đủ thì nằm ở chính dòng này. Bước tháo cầu đấu vẫn bắt buộc.
2. Hàn hai dây đồng trần của loa vào hai pad `SPK+` / `SPK−` trên **chính module U4**. Tín hiệu loa **không đi qua BO A** — từ rev0.5 bo không còn pad loa nào.
3. **Xoắn hai dây loa với nhau.** Chúng mang tín hiệu đóng ngắt theo nhịp tiếng nói; xoắn lại để giảm bức xạ vào mic.
4. Đi dây loa **ra mép sau của bo**, không chạy ngang qua U3. Floorplan đã đặt U4 ở vị trí cuối chính vì lý do này.

### 🔴 Đo trước khi cấp nguồn

Ngõ ra của MAX98357A là **BTL vi sai** — cả hai dây đều là ngõ ra đang tích cực, **không dây nào là mass**. Chạm `SPK−` xuống GND là **cháy ampli ngay lập tức**.

Từ rev0.5 tín hiệu loa không đi qua bo nữa, nên **bo không còn ngăn được lỗi này**. Phải tự đo:

1. Để đồng hồ ở thang thông mạch.
2. Đo giữa **từng dây loa** và một điểm GND bất kỳ trên bo.
3. 🔴 **Cả hai lần đều phải KHÔNG kêu.** Kêu một lần là có chỗ chạm — tìm ra rồi mới được cấp nguồn.
4. Đo giữa hai dây loa với nhau: phải đọc ra **≈8 Ω** (trở kháng cuộn dây). Kêu thông mạch hẳn (0 Ω) nghĩa là loa chập; không thông chút nào nghĩa là loa đứt hoặc mối hàn hỏng.

Silkscreen in `SPK- KHONG CHAM GND` ngay cạnh U4.

### Thử tiếng lần đầu

🔴 **Không dùng lại `amp = 2000` của GĐ 3.** Con số đó đo trên loa 3 W.

1. Đặt biên độ I2S ở mức **thấp**, khoảng 1/5 giá trị cũ.
2. Phát tone 440 Hz. Nghe thấy tiếng thì tăng dần từng nấc.
3. Dừng lại ngay khi nghe **rè hoặc méo** — đó là dấu loa đã quá tải, không phải "chưa đủ to".
4. Ghi lại mức an toàn tìm được và đặt nó thành trần cứng trong firmware.

> 🟡 **Rủi ro chức năng phải biết trước:** mất hộp cộng hưởng và tụt còn Ø15 thì **độ to giảm nhiều** so với loa cũ. Đây là kính hỗ trợ người khiếm thị, tiếng nói là kênh ra duy nhất. **Phải thử nghe ở môi trường ồn thật** trước khi chốt — nếu không đủ to thì vấn đề nằm ở việc chọn loa, không phải ở firmware.

### Lắp loa vào vỏ

- Mặt loa hướng **về phía tai**, quay **lưng lại phía mic** — đây là điều kiện chống hú đã dựa vào ở GĐ 4.
- Đặt càng xa U3 càng tốt. Trên bản 90 × 30, U3 và U4 vẫn xếp sát theo bề rộng; người dùng đã xác nhận hai thân module thật không chạm nhau.
- Có đệm cao su chống rung truyền từ loa sang mic.

---

## 2. Nút SW2 — 3 pad, nhận được hai loại nút

Bo có **3 pad** ở góc phải dưới, thứ tự từ dưới lên: `G` (ô vuông) / `3V3` / `S`. Nối tới GPIO1 (chân 38 của U1).

Giữ đủ 3 pad dù nút trần chỉ cần 2, vì 3 pad **không tốn thêm milimet nào** (chúng nằm trong khe trống cuối bo) mà nhận được cả hai loại:

| Bạn dùng | Đấu vào | Firmware |
|---|---|---|
| Module MKE-M02 đang có (3 chân) | Cả 3 pad `G` / `3V3` / `S` | Đúng cấu hình đã test ở GĐ 1 |
| **Nút bấm thường 2 chân bất kỳ** | Chỉ `G` và `S`, **để trống pad `3V3`** | Bật `INPUT_PULLUP` cho GPIO1 |

Nút 2 chân chạy được nhờ **điện trở kéo lên bên trong ESP32-S3**, không cần linh kiện ngoài. Nghỉ = HIGH, bấm = LOW.

> 🔴 **Hai kiểu này ngược logic nhau.** Module MKE-M02 tự đẩy mức ra; nút trần kéo xuống mass. **Đổi loại nút là phải sửa firmware**, không phải cắm vào là chạy y hệt. Đo mức tại chân `S` bằng đồng hồ ở cả hai trạng thái trước khi tin.

> 🔴 **Cắm ngược module MKE-M02 là hỏng nó.** Cắm nhầm chiều thì chân GND của module đâm vào GPIO1 và chân SIG đâm xuống GND. Ô vuông là `G`.

> 🔴 **Đừng dựa vào ô vuông trên bo này.** Ô vuông của `J_SW2` là `G`, nhưng ô vuông của `J2` ngay bên cạnh lại là `+5V` — hai quy ước ngược nhau, hai cột pad cách nhau 2.46 mm. Từ 2026-08-16 silkscreen in **tên từng pad** dọc mép phải bo, xoay 90°, đọc từ trên xuống: `S` · `3V3` · `G` (đó là `J_SW2`) rồi `G` · `5V` (đó là `J2`). **Đọc chữ, đừng đếm ô vuông.**
>
> 🔴 Nút 2 chân đấu nhầm vào cặp `G` + `3V3` là **chập +3V3 xuống GND mỗi lần bấm** — LDO trên board U1 phải gánh, có thể reset board hoặc hỏng LDO. Nút 2 chân đấu vào `G` và `S`, để trống `3V3`.

### Vì sao trả nút về thay vì dùng wake-word

Wake-word phải nghe liên tục, tức mic và CPU không bao giờ được ngủ. Nút thì ngược lại — **GPIO1 là chân RTC của ESP32-S3** nên đánh thức được từ deep sleep bằng EXT0.

| | Wake-word | **Nút + deep sleep** |
|---|---|---|
| Dòng lúc chờ | ~0.15–0.25 A 🟡 | ~1–3 mA 🟡 (chủ yếu là dòng nghỉ của MT3608 và TP4056) |
| Pin 1000 mAh trụ được | ~4–6 h | **~2 tuần** |

Chênh nhau khoảng trăm lần. Debounce 20–50 ms bằng firmware.

---

## 3. J2 — cửa vào 5 V từ BO B

Hai pad ở góc phải dưới bo, là **cặp dưới cùng** của cột pad sát mép phải. Pad **vuông** là `+5V`, pad tròn là `GND`.

Silkscreen in tên ngay bên phải từng pad, chữ xoay 90°: pad dưới cùng là `5V`, pad trên nó là `G`. 🔴 Dòng cũ `J2 = 2 PAD GOC PHAI DUOI - O VUONG LA 5V` **đã bỏ 2026-08-16** — nó nằm ở giữa bo, cách cột pad 66 mm, và quy ước "ô vuông" của nó ngược với ô vuông của `J_SW2` ngay bên cạnh.

Trình tự đấu nằm ở [HUONG_DAN_LAP_BO_B.md](HUONG_DAN_LAP_BO_B.md) §5 GĐ B8. Tóm tắt phần thuộc về BO A:

1. 🔴 Chỉ hàn dây vào J2 **sau khi** đã đo được 5.00 V ổn định tại J3 của BO B.
2. Đo cực tính đầu dây bằng đồng hồ trước khi hàn. Không tin màu dây.
3. Hàn xong, đo tại **chân 20 (`5V0`) của U1**: phải là 5.00 V.

> 🔴 **Quy trình vĩnh viễn:** bo giờ có **hai** cửa vào 5 V — J2 và cổng USB-C của chính U1. Nạp code qua USB-C thì **phải rút dây J2 ra trước**. Hai nguồn 5 V đấu song song là điều cấm ở [CLAUDE.md](CLAUDE.md) §4. Silkscreen in `CHI DUNG 1 NGUON 5V`.

---

## 4. Thứ tự lắp

```
1. Han U3 (mic) va U4 (ampli)   <- lam TRUOC
2. Do thong mach toan bo
3. Han U1                        <- lam SAU CUNG
4. Dau loa vao U4, do SPK+/SPK- voi GND
5. Han day J2 (sau khi BO B da dat)
```

🔴 **Hàn U1 sau cùng, không được đảo.** Bản SLIM-4 dự tính hàn U1 phẳng sát bo, và khi đó khe hở dưới gầm U1 gần như không còn — đưa mỏ hàn vào mối hàn của U3/U4 là không thể nữa. Hàn U1 trước là tự khoá tay mình.

---

## 5. Những gì chưa biết

| Hạng mục | Tình trạng |
|---|---|
| Chiều cao U4 kể cả cầu đấu | ✅ **ĐÃ ĐO 11 mm**; tháo cầu đấu còn khoảng 4 mm |
| Chiều cao U3 khi lắp header | ✅ **ĐÃ ĐO 6 mm** |
| Chiều cao U1 khi hàn phẳng | 📄 **DATASHEET 4.8 mm** = board 1.6 + USB-C 3.2. Chưa đo cả cụm sau khi hàn |
| Hình dạng đuôi U1 | 🔴 `u1_tail_len`, `u1_tail_end`, `u1_tail_offset` còn **CHƯA BIẾT**. Bản 90 mm chỉ để ướm, chưa gửi xưởng |
| Độ to thực tế của loa Ø15 | 🔴 Chưa nghe thử |
| Mức biên độ I2S an toàn | 🔴 Chưa đo. Phải tìm bằng cách tăng dần từ thấp |
| Dòng lúc deep sleep | 🔴 Chưa đo. Ước tính 1–3 mA, phần lớn là dòng nghỉ của MT3608 và TP4056 chứ không phải ESP32 |
| Logic của nút nếu đổi sang nút 2 chân | 🔴 Chưa test. Ngược với module MKE-M02 |

---

## 6. Lịch sử thay đổi

| Ngày | Phiên bản | Nội dung |
|---|---|---|
| 2026-08-17 | 0.3 | **Bản ướm 90 × 30:** tăng chiều dài 80 → 90 mm; dời U3 và U4 ra xa U1 đúng 10 mm; chuyển J_SW2 vào dải trống mới; nhích U4 0.5 mm về mép trên để tách courtyard. Khoảng courtyard U1→U3/U4 = 5.08/5.38 mm. Netlist không đổi. Cổng: DRC/parity/ERC/unconnected 0; semantic 83-0-0; silk 0/0; U1 CSV 40/40. Xuất `SLIM4_90x30_UOM_1-1.pdf`. 🔴 Chỉ để ướm vì hình dạng đuôi U1 chưa đo đủ; thân U4 có chủ ý nhô khỏi mép PCB khoảng 1.3 mm |
| 2026-08-16 | 0.2 | **Dọn silkscreen — không đụng copper, netlist, footprint hay lỗ khoan.** Sửa 4 chỗ chữ in đè lên pad và 6 chỗ chữ in đè lên chữ. Bỏ: `THAO CAU DAU U4`, `J2: O VUONG=5V`, `J_SW2 = G / 3V3 / S`, `O VUONG = G`, `NUT 2 CHAN: BO TRONG PAD 3V3`, hai nhãn chân `GND`/`VIN` của U4, ref des `U3` và `J2`. Rút gọn `CHAN 1 = O VUONG = DAU ANTENNA` → `CHAN 1 = O VUONG`. Thêm: tên từng pad của `J_SW2` và `J2` in xoay 90° dọc mép phải. Cổng chạy lại: DRC 0 lỗi / 1 cảnh báo, parity 0, unconnected 0, ERC 0, semantic 83-0-0 |
| 2026-08-15 | 0.1 | Tạo mới cùng bản BO_A_SLIM4 (80 × 30). Ghi bốn thay đổi so với cấu hình đã chạy thật ở GĐ 0–4: đổi loa sang Ø15/1 W, GAIN nối VDD 3 dB, bỏ nút SW2, thêm J2 |
