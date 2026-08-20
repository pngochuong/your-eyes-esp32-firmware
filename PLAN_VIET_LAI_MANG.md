# Kế hoạch viết lại tầng mạng — bàn giao sang phiên khác

Trạng thái: **chưa commit gì cả**. Toàn bộ việc ngày 2026-08-18/19 nằm trong working tree.
Việc đầu tiên của phiên mới: đọc file này hết, rồi commit hiện trạng làm mốc quay về.

---

## 0. Mục tiêu người dùng đặt ra

1. **Gửi xong request lên server ≤ 1 giây kể từ lúc nhả nút.** Đây là yêu cầu cứng.
2. Nhả nút là phát `warm_melodic.wav` ngay (đã xong).
3. Kết nối được **mọi mạng 2.4 GHz**; đổi mạng chỉ sửa 2 dòng SSID/mật khẩu, không sửa code (đã đúng).
4. Timeout ngắn, hỏng thì kêu bíp cho người dùng bấm lại — không có vòng thử lại dài trong mã.
5. **Code phải đơn giản.** Board chỉ làm: thu âm → gửi ảnh + tiếng → nhận ADPCM → giải mã → phát.
6. Tận dụng hàm có sẵn của core/ESP-IDF; chỉ tự viết khi không có.

---

## 1. Số đo thực tế — ĐỪNG đo lại, đã trả giá rồi

### Đường truyền

| Mạng | Chiều lên | Chiều xuống | Ghi chú |
|---|---|---|---|
| Hotspot Redmi Note 14 (Viettel 4G) | **5.9 KB/s** thô, **9 KB/s** qua TLS | 14.0 KB/s | chậm hơn 4G bình thường 20–100 lần |
| Router `Ngoc Phat` (FTTH) | chưa đo được (cổng 80 tới API bị chặn) | — | lệnh `u` vô dụng ở đây |

### Kích thước dữ liệu một lần bấm

| Thứ | Byte |
|---|---|
| Ảnh UXGA 1600×1200 q10 | 127 408 |
| Ảnh **HD 1280×720 q10** (đang dùng) | 59 000 – 76 600 |
| PCM thu 3 giây, 16 kHz | 103 424 |
| Sau nén ADPCM | **26 428** (25%) |

### Bắt tay TLS — con số quyết định mọi timeout

Các lần **THÀNH CÔNG** đo được: **1321, 3212, 3321, 5117 ms**.

⟹ `NET_HANDSHAKE_S` phải **≥ 8 giây**. Đặt 5 giây đã loại oan đúng đường đang chạy được — đã xảy ra một lần.

### Một lần chạy trọn vẹn (bản trước khi đẩy ảnh sớm)

```
chup anh          168 ms   0%
thu am           3022 ms   9%
loc + nen ADPCM   166 ms   0%
cho TLS san sang    1 ms   0%    <- mo san luc giu nut, an tien
upload anh + am 17044 ms  52%    <- anh 124 KB @ 9 KB/s
keu tieng da gui  759 ms   2%
server nghi        36 ms   0%    <- server NHANH, khong phai 40 s
phat het cau    11335 ms  34%
```

### Bộ nhớ

| Thời điểm | RAM nội trống | Khối liền lớn nhất |
|---|---|---|
| Sau khi cấp phát audio (rỗi) | ~158 000 | 114 676 – 131 060 |
| Lúc bắt tay TLS trong một lần bấm | ~140 000 | **45 044 – 98 292** |

mbedtls cần ~45 KB **liền**. Tài liệu Espressif: `BIGNUM - Memory allocation failed` xảy ra **dù còn 100K internal RAM** — do phân mảnh.

### Server

- FastAPI + uvicorn sau Cloudflare, `POST /process`, `image` và `audio` đều là `UploadFile | None`.
- Nguồn ở `d:\Study\innostar\Sever_test\app.py` (repo riêng).
- **`Transfer-Encoding: chunked` ở request ĐÃ kiểm chứng chạy được**: curl trả `200` trong 5.6 s, server không phải sửa gì.
- Trả về hiện tại: `Content-Type: audio/wav` + `Transfer-Encoding: chunked` + `x-audio-format: ima_adpcm;rate=16000;channels=1;block=256`, thân bắt đầu bằng `RIFF`.

---

## 2. Chẩn đoán — vấn đề nằm ở board, không ở mạng

Phép thử quyết định: **PC ở CÙNG mạng `Ngoc Phat`** (192.168.1.112, cùng gateway 192.168.1.1, MTU 1500) với tới server trong **193–530 ms** (HTTP 405 = đúng, endpoint chỉ nhận POST). Board (192.168.1.122) không bắt tay TLS nổi lần nào. DNS từ board 9734 ms, từ PC 833 ms.

⟹ Không phải mạng, không phải server, không phải Cloudflare.

Tài liệu Espressif xác nhận đây là điểm yếu cố hữu:

> "Sometimes the TLS handshake takes too long to run on the ESP32 and the connection is reset, particularly problematic in **weak WiFi signal conditions**."

RSSI lúc hỏng: −56 … −60 dBm.

**Nguyên nhân cấu trúc trong code hiện tại:** `wifiConnect()` tự tạo **3 lần bắt tay bỏ đi** trước khi người dùng bấm nút (2 lần thử IPv4 + 1 lần IPv6), mỗi lần `connect()` rồi `stop()`. Mỗi lần là một cơ hội hỏng **và** một lần làm phân mảnh internal RAM.

Đã loại trừ:
- Issue #6077 (`stop()` xoá `handshake_timeout`) — bug của 2.0.2; 3.3.10 sạch, và code đã gọi `setHandshakeTimeout()` trước mỗi connect.
- Hết bộ nhớ theo tổng dung lượng — hỏng cả khi khối liền còn 98 KB.
- MTU của router — PC cùng mạng chạy tốt ở MTU 1500.

---

## 3. Kiến trúc đích

Nguyên tắc: **bắt tay càng ít lần càng tốt; lần nào thành công thì GIỮ LẠI.**

Gộp phần dò đường của `wifi_manager` và phần kết nối của `api_client` thành một khối `src/net/net_session`:

```
netSessionEnsure()                <- diem vao DUY NHAT, tra ve WiFiClientSecure* hoac nullptr
  phien con song + con han?       -> tra ve ngay, 0 ms
  chua co dia chi?                -> WiFi.hostByName() (ham san cua core)
  bat tay TLS                     -> THANH CONG thi GIU LAI, khong stop
  hong + chua bat IPv6?           -> bat IPv6, noi lai mang, thu lai mot lan
```

Xoá được:

| Xoá | Vì |
|---|---|
| `wifiProvenServerIp()` + `apiSetAddress()` | không còn hai nguồn địa chỉ để lệch nhau |
| `tlsReachableOn()` | phép thử biến thành chính phiên làm việc |
| `resolveFamily()` | `WiFi.hostByName()` đã làm đúng thứ tự cần |
| 3 lần bắt tay bỏ đi | còn **1** |

Giữ nguyên (đang chạy tốt, đã kiểm chứng):
- Pipeline 4 bước bất đồng bộ trong `api_client` (`apiBegin` → `apiPushImage` → `apiPushAudio` → `apiWaitReply`), task `netsend` ghim core 0
- Đẩy ảnh trong lúc người dùng đang nói
- `cueSent()` lúc nhả nút, chạy song song với việc đẩy tiếng; `cueError()` khi hỏng
- ADPCM hai chiều, `PcmSource`, `playPcmStream`

---

## 4. Cách đạt mốc ≤ 1 giây

**Đây là phần chưa làm, và là phần quan trọng nhất.**

Người dùng đã xác nhận **server nhận ADPCM thô** ⟹ bỏ được header WAV ⟹ nén và đẩy **từng khối 256 B ngay trong lúc thu**.

```
bam nut  -> mo TLS (song song)
         -> chup anh -> day anh len ngay
         -> thu am: moi 505 mau -> nen 1 khoi ADPCM 256 B -> day len ngay
nha nut  -> chi con phai day: khoi cuoi + mieng dong goi  < 1 KB
         -> < 0.2 s tren MOI duong truyen
```

Vì sao phải bỏ header WAV: header chứa `RIFF size`, `data size`, `fact` số mẫu — cả ba chỉ biết khi thu xong, mà header phải đi trước dữ liệu. Không bỏ được header thì chỉ còn cách khai kích thước tối đa 10 giây rồi đệm đuôi, tức luôn gửi 81 KB dù chỉ nói 3 giây.

Điều kiện: đường phải theo được **8 KB/s** trong lúc giữ nút (ADPCM 16 kHz 4-bit sinh ra đúng mức đó). FTTH thừa sức. Hotspot 9 KB/s thì vừa đủ tiếng nhưng **không còn chỗ cho ảnh** — lúc đó phải hạ ảnh xuống SVGA/VGA (bảng đổi chác đã ghi trong `camera_device.cpp`).

**Cần chốt với người dùng trước khi viết:** hợp đồng mới chính xác là gì?
- vẫn `multipart/form-data` field `audio`, nhưng thân là ADPCM thô (không RIFF)?
- hay bỏ multipart, gửi body thuần + `x-audio-format` header, ảnh đi riêng?

Sai chỗ này là phải làm lại lần nữa.

---

## 5. Danh sách việc, theo thứ tự

1. **Commit hiện trạng** làm mốc quay về.
2. Hỏi người dùng hợp đồng ADPCM thô (mục 4).
3. Viết `src/net/net_session.{h,cpp}`; rút gọn `wifi_manager` còn đúng việc nối Wi-Fi + chẩn đoán.
4. Đổi `api_client` sang dùng `netSessionEnsure()`; xoá `openTls`/`apiResolve`/`apiSetAddress`.
5. **Gọi `netSessionEnsure()` TRƯỚC `startCameraServer()`** — bắt tay lúc RAM còn thoáng nhất.
6. Cho `audio_recorder` trả PCM theo từng khối (callback hoặc hàm `recordStep()`), để `audio_service` nén + `apiPushAudioBlock()` ngay trong lúc thu.
7. Đo lại: chặng "sau khi nhả nút" phải < 1 giây.
8. Cập nhật `CLAUDE.md` + `HUONG_DAN_SERVER.md` theo hợp đồng mới.

Việc riêng, làm sau: **Wi-Fi provisioning** để client tự cấu hình mạng. Hướng đề xuất: SoftAP + trang cấu hình (board đã có `esp_http_server` và hạ tầng HTML nhúng), lưu vào NVS bằng `Preferences`, chỉ vào chế độ cấu hình khi nối thất bại.

---

## 6. Bug đã tìm ra và sửa — ĐỪNG để hồi quy

1. 🔴 **`setConnectionTimeout()` gọi SAU `connect()` vô tác dụng.** `start_ssl_client()` nạp giá trị đó vào `SO_SNDTIMEO`/`SO_RCVTIMEO` của socket ngay lúc connect. Phải đặt **rộng trước** connect, và chặn riêng lần bắt tay bằng `setHandshakeTimeout()` (tầng mbedtls, không đụng option socket). Triệu chứng khi sai: `ghi dung o 0/60537 byte sau 5004 ms` — đúng bằng con số cũ.

2. 🔴 **`if (WiFi.hostByName(...))` là sai, phải `== 1`.** Thất bại hàm trả `err_t` **âm**, mà số âm trong C là truthy ⟹ nhánh lỗi bị đọc thành thành công, địa chỉ nhận được là `0.0.0.0`. Đã in ra `DNS -> 0.0.0.0 (14001 ms)` rồi đi tiếp như không có gì. Ghi chú cũ trong `CLAUDE.md` nói `hostByName()` hỏng là **sai** — hàm đó đúng, lỗi ở bên gọi.

3. 🔴 **`NET_HANDSHAKE_S` không được < 8.** Xem mục 1.

4. 🔴 **DNS dự phòng phải nhét vào ô TRỐNG.** `dnsAddFallback()` trong `wifi_manager.cpp` thêm 8.8.8.8 + 1.1.1.1, không đụng DNS mà DHCP cấp. Hotspot Redmi chỉ cấp resolver IPv6 ⟹ hỏi bản ghi A chết 7 giây. Sau khi thêm: **14000 ms → 156 ms**.

5. 🔴 **`apiAbort()` hết giờ mà task mạng còn sống** thì lần bấm sau đẻ task thứ hai ghi chung một socket TLS. Cờ `g_taskAlive`, `apiBegin()` đợi task cũ thoát hẳn. Giữ cơ chế này khi viết lại.

6. lwip chỉ trả **một** địa chỉ cho một lần phân giải ⟹ mọi ý tưởng "danh sách IP để luân phiên" đều vô dụng. Đã thử và bỏ.

---

## 7. Lệnh kiểm chứng

```powershell
$cli = "D:\AppDownload\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$fqbn = "esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=huge_app,FlashSize=16M,CDCOnBoot=cdc"

& $cli compile --upload -p COM8 -b $fqbn "d:\Study\innostar\your-eyes-esp32-firmware"
```

🔴 Nếu cổng báo busy: Arduino IDE đang giữ COM8 qua tiến trình **`serial-monitor`**. Đóng Serial Monitor trong IDE, hoặc `Stop-Process -Name serial-monitor -Force`.

Bắt log không cần IDE — script đã có sẵn ở scratchpad của phiên cũ, nội dung: mở `System.IO.Ports.SerialPort` COM8 115200, đọc N giây, gửi từng ký tự lệnh, đọc tiếp. Viết lại 20 dòng PowerShell nếu mất.

Lệnh Serial hữu ích: `p` giả lập bấm nút 3 giây · `w` kiểm tra mạng từng chặng · `b` nghe thử hai tiếng báo · `r` in bộ nhớ · `u`/`d` đo tốc độ lên/xuống (⚠️ `u` nhắm cổng 80 của API — bị chặn trên FTTH, vô dụng ở đó).

Mốc kích thước bản dựng hiện tại: **1 291 647 byte (41%)**, RAM tĩnh **77 192 byte (23%)**.

---

## 8. Việc đã xong trong phiên trước, đã kiểm chứng trên board

- DNS: 14 000 ms/lần bấm → **0 ms** (phân giải một lần lúc khởi động + DNS dự phòng IPv4)
- TLS: 8 000 ms sau khi nhả nút → **1 ms** (mở sẵn trong lúc giữ nút, task core 0)
- Ảnh: UXGA 127 KB → **HD 720p ~60 KB**, và đẩy lên trong lúc người dùng đang nói
- `warm_melodic.wav` nhúng flash 16 kHz (`src/audio/cue_sent_pcm.h`, sinh bằng máy — đừng sửa tay), kêu lúc nhả nút, song song với việc đẩy tiếng
- `cueError()` hai tiếng 400 Hz khi hỏng
- Âm lượng: `PLAY_GAIN_PCT` 70 → **55** (méo nằm ở analog: đo được đỉnh 14889/32767, **0 mẫu chạm trần**) · `CUE_GAIN` 0.30 trong `audio_cues.cpp`
- `Connection: keep-alive` + giữ phiên TLS 8 phút (`TLS_KEEPALIVE_MS`), chỉ giữ khi thân đã đọc **hết** — **chưa được chạy thử lần nào** vì chưa có lần bấm nào thành công sau đó
- Bỏ toàn bộ phần tự gọi `lwip_getaddrinfo`, dùng `WiFi.hostByName()`

---

## Nguồn

- [ESP-TLS — ESP-FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/protocols/esp-tls.html)
- [arduino-esp32 #6077 — handshake fails after client.stop()](https://github.com/espressif/arduino-esp32/issues/6077)
- [esp-idf #630 — hardware MPI acceleration hangs mbedtls_ssl_handshake](https://github.com/espressif/esp-idf/issues/630)
- [ESP32 Forum — TLS handshake slow](https://esp32.com/viewtopic.php?t=10679)
