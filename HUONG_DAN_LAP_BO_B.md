# Hướng dẫn lắp BO B — pod nguồn (sạc + boost + pin)

| Mục | Nội dung |
|---|---|
| **Mã tài liệu** | YEV-HW-ASM-BOB-001 |
| **Phiên bản** | 0.3 |
| **Trạng thái** | 🔴 **CHƯA AI LẮP LẦN NÀO.** Mọi bước dưới đây là thiết kế trên giấy |
| **Bo mạch** | `out/BO_B/BO_B.kicad_pcb` — **65 × 30 mm** (thu gọn từ 70 ngày 2026-08-17). DRC **0 lỗi / 4 cảnh báo `lib_footprint_issues`** (chỉ là thư viện `yev` chưa khai trong cấu hình máy — footprint đã nhúng sẵn trong file `.kicad_pcb`, không ảnh hưởng bo in ra), unconnected 0, silk 0 đè pad / 0 đè chữ, ngữ nghĩa **70-0-0**, đối chiếu CSV **12/12** khớp, khe thân module nhỏ nhất **1.31 mm**. 🔴 Schematic parity và ERC **CHƯA CHẠY** — BO B không có `.kicad_sch` |
| **Nguồn sự thật netlist** | [BO_B_NETLIST.csv](BO_B_NETLIST.csv) |
| **Kích thước** | [DIMENSIONS.csv](DIMENSIONS.csv) |
| **Bo đối ứng** | BO A — xem [HUONG_DAN_LAP_TUNG_BUOC.md](HUONG_DAN_LAP_TUNG_BUOC.md) |
| **Cập nhật** | 2026-08-17 |

> 🔴 **Đọc kỹ khác biệt so với tài liệu BO A.** File `HUONG_DAN_LAP_TUNG_BUOC.md` ghi những gì **đã chạy thật** trên breadboard. File này thì ngược lại: **chưa có một bước nào được kiểm chứng trên phần cứng**. Mọi con số "mong đợi" ở đây là ước tính hoặc lấy từ datasheet, không phải kết quả đo. Gặp số đo khác dự đoán thì **dừng lại và nghĩ**, đừng cho rằng tài liệu đúng.

---

## 1. Bo trông thế nào

```
   truoc (ve ban le, noi sang BO A)                        sau (ve tai)
   +----------------------------------------------------------------+
   |  CHINH 5.00V TRUOC KHI CAM J3          J1: O VUONG = BAT+       |
   | [J3 o o]                              KHONG CO CONG TAC  [o o]  |
   |  +--------------------------+  +--------------------------+     |
   |  | o VOUT+       VIN+ o     |  | o OUT+                   |     |
   |  |                          |  | o B+                     |     |
   |  |     U6 MT3608 boost      |  | o B-    U5 TP4056        |[USB-C]
   |  | o VOUT-       VIN- o     |  | o OUT-                   |     |
   |  +--------------------------+  +--------------------------+     |
   +----------------------------------------------------------------+
   0  1                          37 39                           65
                                                                  |
                                              [ BT1 pin 50x20x10 ] v
                                              nam trong cang, sau bo
```

**Chiều dòng điện:** `BT1 → J1 → U5 (sạc + bảo vệ) → U6 (boost 5 V) → J3 → cáp qua gọng → J2 của BO A`

🔴 **SW1 đã bị xoá khỏi thiết kế 2026-08-17.** `U5.OUT+` nối **thẳng** vào `U6.VIN+`. Bo không còn bất kỳ điểm nào để ngắt pin — **không còn cả cặp pad để hàn cầu nối**. Muốn tắt máy phải **rút giắc pin**. Dòng chờ ~0.15–0.25 A 🟡 làm viên 1000 mAh cạn sau ~4–6 h 🟡 và chạm ngưỡng cắt 2.9 V của DW01A mỗi lần để quên. Xả kiệt lặp lại là cách làm chai pin nhanh nhất.

🔴 **Bắt buộc:** bấm đầu **JST-PH 2.0** vào hai dây pin thay vì hàn chết vào J1 — đó là cách duy nhất còn lại để ngắt nguồn. Muốn lắp công tắc sau thì **phải in lại bo**.

---

## 2. 🔴 Sáu điều làm sai là hỏng ngay — đọc hết trước khi cầm mỏ hàn

| # | Sai lầm | Hậu quả |
|---|---|---|
| B1 | Cắm cáp J3 sang BO A **trước khi** chỉnh MT3608 về 5.00 V | Module xuất xưởng có thể đang ở **28 V**. Cháy LDO và ESP32-S3 trên U1, cháy MAX98357A. **Toàn bộ BO A chết trong một giây** |
| B2 | Lấy tải từ `B+`/`B−` thay vì `OUT+`/`OUT−` | Bỏ qua DW01A + FS8205A. Pin mất chống quá xả, chống quá dòng, chống ngắn mạch. Xả kiệt tới 0 V là hỏng cell vĩnh viễn |
| B3 | Cắm ngược cực pin vào J1 | Cháy TP4056, và có thể phồng/nổ pin |
| B4 | Giọt thiếc bắc cầu giữa hai pad của J1 | **Ngắn mạch Li-ion trực tiếp.** Mạch bảo vệ nằm *sau* hai pad này nên không cứu được. Cell 1000 mAh xả ngắn mạch là nóng đỏ trong vài giây |
| B5 | Nối `VBAT_RAW_N` với `GND` ở bất cứ đâu | Nối tắt hai MOSFET bảo vệ. Mạch bảo vệ thành vô tác dụng mà nhìn ngoài vẫn "chạy bình thường" |
| B6 | Cắm cáp từ BO B **cùng lúc** với cắm USB-C vào U1 để nạp code | Hai nguồn 5 V đấu song song, đối đầu nhau. Cấm ở [CLAUDE.md](CLAUDE.md) §4 |

Thêm hai điều về cơ khí, làm khi lắp vào vỏ:

- 🔴 **Không bắt vít xuyên qua vùng cell pin.** Đâm thủng cell Li-ion là cháy.
- 🔴 **Không ép pin.** Khoang chứa phải cứng, đủ rộng, và có khe thoát khí.

---

## 3. Cần những gì

| Linh kiện | Ghi chú |
|---|---|
| Bo BO B đã gia công | 🔴 Hiện bo còn in `DO NOT FAB` — xem §8 |
| U5 TP4056 Type-C có bảo vệ | 4 pad `OUT+ B+ B− OUT−`, một hàng dọc |
| U6 MT3608 boost | 2×2 pad, có biến trở |
| BT1 pin Li-ion 1S 1000 mAh, có mạch bảo vệ, ra dây trần | |
| Dây 24 AWG silicone, 2 màu | Cáp sang BO A, ~180 mm |
| Đầu JST-PH 2.0 + hạt bấm | 🔴 **Bắt buộc** — sau khi bỏ SW1 đây là cách duy nhất ngắt được nguồn |

| Dụng cụ | Bắt buộc? |
|---|---|
| Đồng hồ vạn năng | 🔴 **Bắt buộc.** Không có thì không làm được bước nào cả |
| Mỏ hàn, thiếc, nhựa thông | Bắt buộc |
| Tuốc-nơ-vít dẹt nhỏ | Bắt buộc — vặn biến trở |
| Điện trở 10 Ω / 5 W | Nên có — làm tải giả ở GĐ B7 |
| Sơn móng tay | Nên có — khoá biến trở |

---

## 4. 🔴 Thứ tự lắp — không được đảo

Thứ tự này không phải để cho tiện tay. Mỗi bước tồn tại để **giới hạn thiệt hại** nếu bước đó sai.

```
B1  Chinh MT3608 ve 5.00 V  <- LAM KHI MODULE CON ROI, chua han gi ca
B2  Kiem bo tran
B3  Han U5, han U6
B5  Han day pin vao J1
B6  Thu SAC
B7  Thu XA, do ap ra tai J3
B8  Noi cap sang BO A     <- BUOC CUOI CUNG, khong duoc lam som
```

Ý tưởng xuyên suốt: **BO A chỉ được nối vào ở bước cuối**, sau khi đã tận mắt đo được 5.00 V ổn định tại J3.

---

## 5. Từng bước

### GĐ B1 — Chỉnh MT3608 về 5.00 V (làm khi module còn rời)

Làm bước này **trước khi hàn U6 xuống bo** thì an toàn nhất: module nằm rời trên bàn, không có gì khác đấu vào, không có gì để cháy.

1. Nối nguồn 3.7 V vào hai chân `VIN+` / `VIN−` của module. Dùng chính viên pin, hoặc nguồn bàn đặt 3.7 V.
2. Đặt que đỏ của đồng hồ lên `VOUT+`, que đen lên `VOUT−`. Để thang DC volt 20 V hoặc tự động.
3. **Giữ que đo trong suốt quá trình vặn.** Không bao giờ vặn biến trở khi không nhìn thấy số.
4. Bật nguồn, đọc số. Có thể là bất kỳ giá trị nào từ ~3.7 V tới ~28 V.
5. Vặn biến trở khoảng ¼ vòng, nhìn đồng hồ. Số đi về phía 5.00 thì vặn tiếp cùng chiều; đi xa ra thì vặn ngược lại. **Không có quy ước chung về chiều vặn** — mỗi lô module một khác.
6. Vặn từ từ tới **5.00 V**. Biến trở loại nhiều vòng cần 20–25 vòng từ đầu này sang đầu kia, nên vặn một lúc lâu chưa thấy đổi là bình thường. Khi chạm đầu cữ nó trượt lạo xạo — nghe thấy thì dừng, đừng cố.
7. Chấp nhận **4.95–5.10 V**. 🔴 Tuyệt đối không để vượt **5.2 V**.
8. Tắt, đợi vài giây, bật lại, đo lại. Bước này bắt trường hợp biến trở tiếp xúc chập chờn.
9. Nhỏ một giọt sơn móng tay lên trục biến trở để khoá. Kính rung lắc khi đeo, biến trở xoay nhẹ là điện áp đổi mà không ai biết.

> **Boost không hạ được xuống dưới điện áp vào.** Với pin 3.7 V thì mức thấp nhất chỉnh được là ~3.7 V, không phải 0. Nếu nó kẹt ở mức cao và không hạ được thì kiểm tra xem có đúng đang vặn biến trở của module đó không, hoặc module lỗi.

### GĐ B2 — Kiểm bo trần

Làm trước khi hàn bất cứ thứ gì. Mất 2 phút, cứu được cả buổi.

1. Để đồng hồ ở thang đo thông mạch (kêu bíp).
2. Đo giữa **bất kỳ pad `GND` nào** và **pad `B−` của U5** (`VBAT_RAW_N`). 🔴 **Phải KHÔNG kêu.** Kêu là bo bị chập hai net mà lẽ ra phải tách — dừng lại, không hàn gì thêm.
3. Đo giữa `J3.5V` và `J3.GND`. Phải không kêu.
4. Đo thông mạch theo netlist: `U5.OUT+` ↔ `U6.VIN+` phải kêu (nối thẳng, không còn công tắc ở giữa); `U6.VOUT+` ↔ `J3.5V` phải kêu; `U5.B+` ↔ `J1.BAT+` phải kêu; `U5.B−` ↔ `J1.BAT−` phải kêu.

### GĐ B3 — Hàn U5 và U6

1. 🔴 **Xác định chiều U5 trước khi hàn.** Bốn pad theo thứ tự từ trên xuống là `OUT+` / `B+` / `B−` / `OUT−`. Silkscreen in `GIUA = PIN THO` và `NGOAI = TAI` để nhắc: **hai chân giữa là cực pin trần, hai chân ngoài mới là ngõ ra cho tải**. Lệch một pad là tải đi thẳng vào cell.
2. Cạnh có cổng USB-C của U5 phải quay **ra mép sau của bo** (phía tai) — cổng sạc phải thò ra khỏi vỏ.
3. Hàn U6 (đã chỉnh 5.00 V ở GĐ B1). Đặt sao cho **biến trở quay về phía nắp vỏ**, để còn chỉnh lại được sau khi lắp.
4. Hàn xong, đo lại thông mạch như GĐ B2 bước 2: `GND` và `B−` **vẫn phải không kêu**.

### GĐ B4 — (đã bỏ)

Bước này trước đây là hàn cầu nối tắt hai pad SW1. **SW1 đã bị xoá khỏi bo 2026-08-17**, không còn pad nào để hàn — `U5.OUT+` đi thẳng vào `U6.VIN+`. Bỏ qua bước này và sang thẳng GĐ B5.

🔴 Đổi lại, ở GĐ B5 **phải** bấm đầu JST-PH 2.0 vào dây pin thay vì hàn chết. Đó là cách duy nhất còn lại để ngắt nguồn.

### GĐ B5 — Hàn dây pin vào J1

1. 🔴 **Đo cực tính bằng đồng hồ.** Để thang DC volt, chạm hai que vào hai dây pin. Que đỏ đọc ra số **dương** thì dây đang chạm que đỏ là `BAT+`. **Không được tin màu dây** — pin bán lẻ đảo màu là chuyện thường.
2. Hàn `BAT+` vào **pad vuông** của J1, `BAT−` vào pad tròn. Silkscreen in `O VUONG = BAT+`.
3. 🔴 **Nhìn kỹ hai mối hàn xong.** Hai pad này cách nhau 5.08 mm (rộng gấp đôi bình thường, cố ý) nhưng vẫn phải chắc chắn **không có sợi thiếc nào bắc cầu**. Đây là cực pin trần, mạch bảo vệ nằm phía sau — bắc cầu là ngắn mạch Li-ion trực tiếp.
4. Đo lại thông mạch giữa hai pad J1: **phải không kêu**.

### GĐ B6 — Thử sạc

1. Chưa nối gì sang BO A.
2. Cắm cáp USB-C vào cổng của U5.
3. Đèn trên module U5 phải sáng. 🟡 Thông thường **đỏ = đang sạc, xanh/lam = đầy**, nhưng màu tuỳ lô module — kiểm chứng bằng số đo ở bước sau chứ đừng tin màu đèn.
4. Đo điện áp trên hai pad `B+` / `B−`. Nó phải **tăng dần** theo thời gian, và dừng ở **4.2 V** 📄 khi đầy.
5. 🔴 **Kiểm Rprog trên module thật.** BOM yêu cầu dòng sạc 500 mA, tương ứng Rprog = **2.4 kΩ**. Nhiều module bán sẵn gắn 1.2 kΩ = 1000 mA. Với viên 1000 mAh thì 1000 mA là sạc 1C — chạy được nhưng nóng và giảm tuổi thọ. Đọc mã trên con điện trở đó.

### GĐ B7 — Thử xả và đo áp ra

1. Rút cáp sạc.
2. Đo tại hai pad của **J3**. Phải đọc được **5.00 V** ±0.1 — đúng con số đã chỉnh ở GĐ B1. Khác đi thì dừng, tìm nguyên nhân, đừng nối sang BO A.
3. **Thử có tải.** Mắc điện trở 10 Ω / 5 W vào J3 — nó rút 0.5 A. Điện áp phải giữ ≥ **4.8 V** 🟡. Sụt nhiều hơn nghĩa là module yếu hơn công bố; phải biết bây giờ chứ đừng để phát hiện lúc loa kêu thì ESP32 reset.
4. 🔴 **Thử ngưỡng ngắt của mạch bảo vệ trên pin.** Viên pin đã mua có PCM tích hợp, và PCM của pin tai nghe thường đặt ngưỡng quá dòng **thấp** — một số loại chỉ 1–2 A. Rút thử ~1.7 A trong 1–2 giây và xem nó có ngắt không. Nếu ngắt thì máy sẽ tắt đột ngột đúng lúc phát audio to. 🔴 Chưa ai đo cái này.

### GĐ B8 — Nối sang BO A

**Chỉ làm bước này sau khi GĐ B7 đạt.**

1. Cắt hai đoạn dây 24 AWG silicone, dài ~180 mm, **xoắn đôi với nhau**. Xoắn để giảm diện tích vòng dây — nó chạy ngay cạnh antenna của U1 và cạnh mic.
2. Hàn một đầu vào J3: dây đỏ vào **pad vuông** (`+5V`), dây đen vào pad còn lại.
3. Đầu kia **để hở**, đo bằng đồng hồ: dây đỏ phải đọc ra **+5.00 V** so với dây đen. Không tin màu dây, tin đồng hồ.
4. 🔴 **Xác nhận USB-C của U1 trên BO A đang KHÔNG cắm gì.** Hai nguồn 5 V song song là điều cấm — silkscreen của cả hai bo đều in `CHI DUNG 1 NGUON 5V`.
5. Hàn vào J2 của BO A: đỏ vào **pad vuông** (`5V`), đen vào pad còn lại.
6. Bật nguồn. Đo lại tại chân 20 (`5V0`) của U1: phải là **5.00 V**.

> **Quy trình vĩnh viễn từ đây về sau:** muốn nạp code cho ESP32 qua USB-C của U1 thì **rút dây J2 ra trước**. Cắm cả hai là hai nguồn 5 V đối đầu nhau.

---

## 6. Bảng đo mong đợi

🔴 Toàn bộ cột "mong đợi" là **ước tính hoặc datasheet, chưa ai đo**. Ghi số đo thật của bạn vào cột cuối.

| Điểm đo | Điều kiện | Mong đợi | Nhãn | Đo thật |
|---|---|---|---|---|
| `B+` ↔ `B−` | Pin đầy | 4.2 V | 📄 | |
| `B+` ↔ `B−` | Pin cạn, trước khi PCM cắt | ~2.9 V | 📄 | |
| `J3` | Không tải | 5.00 V | ✅ đã chỉnh tay | |
| `J3` | Tải 10 Ω (0.5 A) | ≥ 4.8 V | 🟡 | |
| Dòng sạc | Đang sạc | 500 mA (Rprog 2.4 kΩ) | 📄 | |
| Dòng tiêu thụ | Wake-word, nằm chờ | 0.15–0.25 A từ pin | 🟡 | |
| Thời gian dùng | Wake-word liên tục | ~4–6 h | 🟡 | |
| Ngưỡng ngắt PCM của pin | Rút dòng đỉnh | **CHƯA BIẾT** | 🔴 | |

---

## 7. Sự cố thường gặp

| Hiện tượng | Nguyên nhân hay gặp nhất |
|---|---|
| Đo J3 ra 0 V | PCM của pin đang ở trạng thái cắt — cắm sạc vài giây để nó nhả |
| J3 ra đúng bằng điện áp pin (~3.7 V) | Biến trở đang vặn ở đáy. Boost không hạ dưới Vin được — vặn lên |
| Vặn hết mà không xuống 5 V | Đang vặn nhầm biến trở, hoặc module lỗi |
| Sạc không vào, đèn không sáng | Cáp USB-C hỏng (rất hay gặp), hoặc cổng USB-C của U5 chưa tiếp xúc |
| Máy tắt đột ngột khi loa kêu to | PCM của pin ngắt vì quá dòng — xem GĐ B7 bước 4 |
| ESP32 reset khi loa kêu | Sụt áp trên `+5V_SYS`. Đường lui: hàn một tụ 470–1000 µF thẳng vào hai chân `VIN`/`GND` trên module U4 của BO A |

---

## 8. 🔴 Bo hiện chưa được phép gia công

Bo đang in `!! DIMS UNVERIFIED - DO NOT FAB !!` lên silkscreen vì còn **một tham số quyết định vị trí lỗ khoan chưa được đo**: `u5_pad_pitch` — bước giữa 4 pad `OUT+ B+ B− OUT−` của TP4056, đang để tạm 2.54 mm với nhãn `GIA_DINH`.

Cách gỡ:

- [ ] In `out/BO_B/BO_B_UOM_1-1.pdf` **tỷ lệ 100 %**, đặt module TP4056 thật lên giấy. Bốn chân trùng bốn lỗ thì 2.54 đúng — đổi nhãn sang `DO_ROI` trong [DIMENSIONS.csv](DIMENSIONS.csv), chạy lại generator, dòng `DO NOT FAB` tự biến mất.

Các thứ khác **không chặn gia công** vì không quyết định lỗ khoan, nhưng phải có trước khi dựng vỏ:

- [x] ✅ Biến trở của U6 nằm mặt nào — **đã đo 2026-08-15: `u6_pot_side = MAT_TREN`**, cùng phía linh kiện. Vỏ đã khoét lỗ chỉnh trên nắp pod trái
- [x] ✅ Chiều cao thật của U6 — **đã đo 2026-08-15: `u6_hgt = 6 mm`**, không phải 14. Số 14 cũ sai hơn gấp đôi; U6 không còn là món cao nhất hệ (U4 = 11 mm mới là)
- [ ] Đầu nào của U6 có cuộn cảm (`u6_coil_end` vẫn `CHUA_BIET`) — quyết định chiều đặt module
- [ ] `u5_hgt` — vẫn `GIA_DINH` 4 mm, chưa ai đo
- [ ] `u5_len` / `u5_wid` — vẫn `NHA_BAN` 26 × 17, lấy từ trang bán, chưa đo
- [ ] `u6_len` / `u6_wid` — vẫn `NHA_BAN` 36 × 17, chưa đo

---

## 9. Lịch sử thay đổi

| Ngày | Phiên bản | Nội dung |
|---|---|---|
| 2026-08-17 | 0.3 | 🔴 **Xoá SW1 khỏi thiết kế** theo yêu cầu người dùng. `U5.OUT+` nối thẳng `U6.VIN+`; net `VSW` biến mất, netlist còn **5 net / 12 chân**. Bo không còn bất kỳ điểm nào ngắt pin — kể cả cặp pad hàn cầu nối. **Bắt buộc bấm đầu JST-PH 2.0 vào dây pin**; muốn lắp công tắc sau thì phải in lại bo. Bỏ GĐ B4. **Thu gọn bo 70 → 65 × 30**: J3 chuyển từ lề trái lên dải trống `y=18..30` (chỗ vốn để không vì thân U5/U6 chỉ cao 17 trên bo cao 30), nhờ đó U6 lùi từ `x=6` về `x=1`. Bề rộng giữ 30 để hai càng kính dùng chung một mặt cắt ray trượt. Khe thân module nhỏ nhất **0.21 → 1.31 mm**. Sửa 4 cảnh báo `text_height` (chữ 0.75 < ngưỡng 0.8) và 1 chỗ chữ đè chữ. Dời `DO NOT FAB` sang mặt sau — đó là cảnh báo gửi xưởng, không phải cảnh báo lắp ráp, và mặt trước bo 65 mm đã hết chỗ. Thêm `gen/body_clearance_check.py`. |
| 2026-08-16 | 0.2 | **Dọn silkscreen + bổ sung bộ kiểm ngữ nghĩa.** Sửa 2 chỗ chữ đè pad ở mặt sau (`THU NGHIEM - KHONG GUI XUONG` đè lên `U5.OUT+` và `U6.VIN+` — KiCad DRC cũng bắt là `silk_over_copper`) và 3 chỗ chữ đè chữ ở mặt trước. **Sửa mâu thuẫn tài liệu:** §5 bước 4 khẳng định "silkscreen của cả hai bo đều in `CHI DUNG 1 NGUON 5V`" — BO B trước đó **không in**; nay đã thêm vào bo. Cập nhật §8: `u6_pot_side` và `u6_hgt` đã đo từ 2026-08-15, checklist cũ đã lạc hậu. Thêm `gen/semantic_check_bo_b.py` (77 phép, 0 trượt) và `gen/netlist_parity_csv.py` (14/14 chân khớp CSV). Sinh lại `.kicad_pcb`, `DRC.json`, `.step`, PDF 1:1 hai mặt, `mat_tren.png`, `mat_sau.png` |
| 2026-08-15 | 0.1 | Tạo mới, cùng lúc với bản BO B đầu tiên (`out/BO_B/`, 70 × 30 mm). Trước đó BO B chưa bao giờ được vẽ. Toàn bộ nội dung là thiết kế trên giấy, chưa lắp lần nào |
