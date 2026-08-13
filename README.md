# VisionCare

Thiết bị hỗ trợ người khiếm thị chạy trên **ESP32-S3**: giữ nút → chụp ảnh + thu giọng nói → POST lên server AI → phát câu trả lời ra loa. Kèm một web server phụ để xem hình trực tiếp và chỉnh cảm biến khi cần chẩn đoán.

Một lần bấm nút trả lời được các câu hỏi kiểu *"trước mặt tôi có gì?"*, *"tờ giấy này viết gì?"* — server lo OCR + mô tả cảnh + TTS, board chỉ lo chụp, thu, gửi và phát.

| | |
|---|---|
| **Nền tảng** | Arduino (sketch `.ino` + thư mục `src/`), arduino-esp32 core 3.3.x |
| **Không phải** | PlatformIO · CMake · không có test tự động |
| **Kiểm chứng bằng** | biên dịch bằng `arduino-cli` + các lệnh chẩn đoán gõ qua Serial |
| **Ngôn ngữ comment** | tiếng Việt **không dấu** trong mã nguồn; có dấu trong `.md`/`.csv` |
| **Trạng thái** | pipeline chính chạy được đầu-cuối; đang thử module 4G để cắt phụ thuộc Wi-Fi |

---

## 1. Luồng chạy

```
setup()                      VisionCare.ino
  cameraStart()              khong co camera -> dung han, khong dung web server
  webLedBegin()
  wifiConnect(30s)           mat Wi-Fi van di tiep (nut/mic/loa phai song)
  startCameraServer()        chi khi co IP
  startAudio()               goi SAU web server, de camera+httpd chiem RAM truoc
loop()                       chi trong chung Wi-Fi, cu 10 s mot lan

task "audio" (ghim core 1)   audio_service.cpp — noi DUY NHAT biet trinh tu
  consolePoll()              lenh chan doan Serial
  giu BTN  -> photoCapture() -> recordWhileHeld() -> normalizeRecording()
  nha BTN  -> apiSendCapture()  (multipart: image=JPEG, audio=WAV)
           -> playPcmStream()   qua PcmSource (PCM tho / WAV / MP3)

core 0                       Wi-Fi/TCP + esp_http_server (stream MJPEG)
```

**Nguyên tắc kiến trúc:** các khối con (`camera_capture`, `audio_recorder`, `audio_dsp`, `api_client`, `audio_player`) **không biết gì về nhau**. Mọi phối hợp đi qua [audio_service.cpp](src/audio/audio_service.cpp). Muốn đổi trình tự một lần bấm nút → sửa đúng file đó, đừng sửa các khối.

Thời gian thực tế một lần bấm: nhả nút xong phải đợi **~40 giây** cho server xử lý. Đó là giới hạn của server, board không nhanh hơn được.

---

## 2. Phần cứng

| # | Linh kiện | Vai trò | Nguồn | GPIO / kết nối | Trạng thái |
|---|---|---|---|---|---|
| 1 | **ESP32-S3 WROOM N16R8 CAM** (GOOUUU, pinout = ESP32S3_EYE) | MCU trung tâm, Wi-Fi | 5 V qua USB-C | xem pin map dưới | ✅ |
| 2 | **MAX98357A** I2S Class-D amp | Phát câu trả lời | VIN 5 V | BCLK=41, LRC=42, DIN=21; SD/MODE thả nổi | ✅ |
| 3 | **INMP441** I2S MEMS mic | Thu giọng nói | **3.3 V — tuyệt đối không 5 V** | BCLK=41, WS=42, SD=47; L/R→GND (khe TRÁI) | ✅ |
| 4 | **Loa mini 3 W 8 Ω** | Ngõ ra tiếng | từ SPK± của amp | không nối GPIO | ✅ |
| 5 | **OV2640 góc rộng 120° + cáp FPC 75 mm** | Ảnh cho OCR + mô tả cảnh + stream web | qua đế camera | nối sẵn PCB, khai ở [camera_pins.h](camera_pins.h) | ✅ |
| 6 | **MKE-M02 Button Module** | Nút giữ-để-nói | TTL 3.3/5 V | SIG=GPIO1, `INPUT_PULLUP`, active LOW | ✅ |
| 7 | Breadboard 400 point | Prototype | — | — | chưa test |
| 8 | Jumper wire | Đấu nối | — | — | chưa test |

Bảng đầy đủ (dòng tiêu thụ, kích thước, độ tin cậy từng số liệu, lưu ý lắp đặt): [BangTomTat.csv](BangTomTat.csv).

### Pin map — chốt trong [app_config.h](app_config.h)

| Chức năng | GPIO |
|---|---|
| I2S BCLK (chung mic + amp) | 41 |
| I2S WS/LRC (chung) | 42 |
| I2S DOUT → amp DIN | 21 |
| I2S DIN ← mic SD | 47 |
| Nút nhấn | 1 (active LOW) |
| Camera XCLK / SIOD / SIOC / PCLK / VSYNC / HREF | 15 / 4 / 5 / 13 / 6 / 7 |
| Camera D0–D7 | 11, 9, 8, 10, 12, 18, 17, 16 |
| Đèn chiếu sáng (`FLASH_LED_PIN`) | **-1 = chưa lắp** |

Chân **còn trống**: 2, 14, 38, 39, 40, 48 (38–40 là thẻ SD, 48 là NeoPixel onboard — chỉ dùng khi chấp nhận bỏ hai thứ đó). **Tránh**: GPIO 0/3/45/46 (strap), 19/20 (USB-JTAG).

---

## 3. Build & nạp

### Arduino IDE (bắt buộc đúng 4 mục này)

Board `ESP32S3 Dev Module` · PSRAM `OPI PSRAM` · Partition Scheme `Huge APP (3MB No OTA/1MB SPIFFS)` · Flash Size `16MB`.
Không chọn đúng **PSRAM OPI + Huge APP** thì firmware (~1.23 MB) **không nạp nổi**.

### arduino-cli (đã cài sẵn trên máy này, có trong PATH)

```powershell
arduino-cli compile --upload -p COM11 -b "esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=huge_app,FlashSize=16M,CDCOnBoot=cdc" "e:\VisionCare"
arduino-cli monitor -p COM11 -c baudrate=115200
```

🔴 **`CDCOnBoot=cdc` là bắt buộc dù bảng Arduino IDE không nhắc.** Board nối PC qua **USB gốc của ESP32-S3** (`VID_303A&PID_1001`, USB-Serial/JTAG) — không có cầu UART CH340 nên **không có COM thứ hai**. Mặc định `CDCOnBoot=default` đẩy `Serial.print` ra UART0 (GPIO 43/44) → nạp xong máy vẫn chạy nhưng Serial **câm hoàn toàn**, rất dễ tưởng firmware chết.

| Mục | Giá trị trên máy này |
|---|---|
| Thực thi | `C:\Program Files\Arduino CLI\arduino-cli.exe` (v1.5.1) |
| Thư mục data | `C:\Users\PCPV\AppData\Local\Arduino15` |
| Core đã cài | `esp32:esp32` 3.3.10, `arduino:avr` 1.8.8 |
| Thư viện cài riêng | không có — mọi thứ đi kèm core hoặc nằm trong `src/` |
| Cổng board | `COM11 · ESP32 Family Device` (COM 3–10 là Bluetooth ảo) |

Thư viện dùng: `esp32-camera`, `driver/i2s_std.h` (ESP-IDF — **không** dùng `ESP_I2S`), `WiFi`/`WiFiClientSecure`, `esp_http_server`, `libhelix-mp3` (đã chép sẵn vào `src/libhelix-mp3/`).

Mốc kích thước để so sánh — lệch nhiều là có gì đó vừa đổi:

| Bản dựng | Program storage | RAM tĩnh |
|---|---|---|
| không `CDCOnBoot` | 1 228 818 byte (39%) | 70 268 byte (21%) |
| có `CDCOnBoot=cdc` + chẩn đoán Wi-Fi | 1 246 867 byte (39%) | 70 656 byte (21%) |

---

## 4. Cấu trúc mã

**Gốc thư mục = những gì người dùng sửa. `src/` = mã.**

| File gốc | Nội dung |
|---|---|
| [VisionCare.ino](VisionCare.ino) | **SSID + mật khẩu Wi-Fi** (dòng 37–38), rồi `setup()` + `loop()` |
| [app_config.h](app_config.h) | endpoint API, timeout mạng, chân GPIO, XCLK, thông số thu âm |
| [board_config.h](board_config.h) | chọn model camera (`CAMERA_MODEL_ESP32S3_EYE`) |
| [camera_pins.h](camera_pins.h) | sơ đồ chân camera theo model |

### `src/camera/`
| File | Việc |
|---|---|
| [camera_device](src/camera/camera_device.h) | dựng `camera_config_t`, `cameraStart()/cameraReinit()/cameraIsReady()`. **Nguồn duy nhất** của frame size (UXGA 1600×1200) + JPEG quality |
| [camera_tuning](src/camera/camera_tuning.h) | profile cảm biến cho OCR, `cameraWarmUp()`, in setting |
| [camera_capture](src/camera/camera_capture.h) | `photoCapture()` chụp một LOẠT rồi giữ khung nét nhất vào PSRAM; đèn chiếu sáng; `photoMeasureSharpness()` |

### `src/net/`
| File | Việc |
|---|---|
| [wifi_manager](src/net/wifi_manager.h) | `wifiConnect()`, `wifiSelfTest()` (Wi-Fi → DNS → TCP/TLS), `wifiScanReport()` |
| [http_body_reader](src/net/http_body_reader.h) | `BodyReader` đọc thân HTTP cả `Content-Length` lẫn `chunked` |
| [api_client](src/net/api_client.h) | `apiSendCapture()` — TLS, dựng multipart, đọc status + header. **Không phát gì ra loa**, trả `ApiReply` để người gọi tự đọc thân |

### `src/audio/`
| File | Việc |
|---|---|
| [audio_i2s](src/audio/audio_i2s.h) | mở kênh I2S full-duplex, `i2sSetSampleRate()`, `i2sWriteSilence()` |
| [audio_buffers](src/audio/audio_buffers.h) | `recBuf[]` (thu) + `stageBuf[]` (phát) — xin MỘT lần lúc khởi động |
| [audio_recorder](src/audio/audio_recorder.h) | `recordWhileHeld()`, `measureNoiseOnly()` — chỉ đọc mic |
| [audio_dsp](src/audio/audio_dsp.h) | làm việc TẠI CHỖ trên `recBuf[]`: lọc thông cao 76 Hz, `noiseGate()`, `normalizeRecording()` (khuếch đại tối đa 64×) |
| [audio_format](src/audio/audio_format.h) | `wavHeader()`, `parseAudioFormat()`, `readWavHeader()` — thuần số liệu |
| [pcm_source](src/audio/pcm_source.h) | `PcmSource`: PCM thô lấy thẳng từ socket, hoặc giải mã MP3 (Helix) trước |
| [audio_player](src/audio/audio_player.h) | `playPcmStream()` vừa nhận vừa phát, đệm tự điều chỉnh (`PLAY_GAIN_PCT = 70`); `playTone()` |
| [audio_service](src/audio/audio_service.h) | **điều phối** — file duy nhất biết trình tự nút → ảnh → tiếng → server → loa |

### `src/web/`, `src/diag/`, `src/util/`
| File | Việc |
|---|---|
| [web_server](src/web/web_server.h) | cổng **80**: `/`, `/capture`, `/status`, `/control`, `/bmp`, `/xclk`, `/reg`, `/greg`, `/pll`, `/resolution` · cổng **81**: `/stream` (MJPEG) |
| [web_index_html.h](src/web/web_index_html.h) | trang HTML nhúng (gzip trong mảng byte) |
| [web_led](src/web/web_led.h) | LED onboard chạy PWM, chỉnh qua `/control?var=led_intensity`. **Khác** đèn chiếu sáng của `camera_capture` |
| [serial_console](src/diag/serial_console.h) | phân phối lệnh `c/m/t/w/s/n` |
| [audio_diag](src/diag/audio_diag.h) | `diagSlotCompare()`, `diagSpeakerTone()`, `diagNoiseVsCamera()` |
| [camera_diag](src/diag/camera_diag.h) | `diagCaptureTest()` chụp thử + đo độ nét |
| [mem_alloc](src/util/mem_alloc.h) | `bigAlloc()` — **cửa duy nhất** cấp phát vài trăm KB, ưu tiên PSRAM rồi lui về RAM trong |
| `src/libhelix-mp3/` | bộ giải mã MP3 Helix chép từ ESP8266Audio (RCSL/RPSL). **Vendored — không sửa** |

---

## 5. Sửa gì thì vào đâu

| Muốn đổi | File |
|---|---|
| Wi-Fi SSID / mật khẩu | [VisionCare.ino:37](VisionCare.ino#L37) |
| Endpoint API, timeout mạng | [app_config.h:23-50](app_config.h#L23-L50) |
| Chân GPIO I2S / nút / đèn | [app_config.h:76-101](app_config.h#L76-L101) |
| Tần số thu, trần thời gian thu | [app_config.h:109-113](app_config.h#L109-L113) |
| XCLK camera | [app_config.h:74](app_config.h#L74) |
| Model camera | [board_config.h:16](board_config.h#L16) |
| Độ phân giải / chất lượng JPEG / profile cảm biến | `src/camera/camera_device.cpp` + `camera_tuning.cpp` |
| Ngưỡng lọc ồn, độ khuếch đại bản thu | `src/audio/audio_dsp.cpp` |
| Mức đệm khi phát, độ lớn tiếng | `src/audio/audio_player.cpp` |
| Trình tự một lần bấm nút | `src/audio/audio_service.cpp` |

**Quy ước:** `app_config.h` chỉ chứa thứ dùng chung nhiều khối. Các nút vặn riêng của một khối nằm ngay trong `.cpp` của khối đó, cạnh dòng mã dùng tới nó — đừng kéo lên `app_config.h`.

---

## 6. API contract

```
POST https://api.visioncare-host.uk/process        (443, TLS)
Content-Type: multipart/form-data
  field "image" — capture.jpg,  image/jpeg
  field "audio" — record.wav,   audio/wav (PCM 16-bit mono 16 kHz)

Tra ve 200:
  audio/wav    (do duoc: PCM 16-bit mono 48 kHz, co Content-Length)
  audio/mpeg   -> giai ma bang Helix
  PCM tho kem header  x-audio-format: pcm_s16le;rate=16000;channels=1
```

Nhánh `chunked` vẫn giữ làm dự phòng — Cloudflare có thể đổi bất cứ lúc nào.
TLS đang dùng `setInsecure()` (không xác thực chứng chỉ) — đã biết, ghi trong GĐ 6 của hướng dẫn bring-up.

---

## 7. Lệnh chẩn đoán (gõ qua Serial, 115200)

| Phím | Việc |
|---|---|
| `c` | chụp thử + đo độ nét, không gửi đi đâu |
| `m` | nghe so sánh hai kiểu ghi khe I2S (đoán chế độ SD_MODE của amp) |
| `t` | phát sin sạch tăng dần biên độ, đo méo tiếng bằng chính mic |
| `w` | kiểm tra đường mạng theo chặng: Wi-Fi → DNS → TCP/TLS, rồi ép thử riêng IPv4 và IPv6 |
| `s` | quét mọi AP 2.4 GHz bắt được, đánh dấu cái trùng `WIFI_SSID` |
| `n` | đo nền nhiễu khi camera BẬT và khi camera TẮT |

---

## 8. Ràng buộc đã trả giá để biết — đừng phá

**Cấu trúc mã**
- **Mọi file trong `src/` phải là `.cpp`, không `.ino`.** Arduino tự sinh prototype chèn lên đầu file gộp, trước `#include <WiFiClientSecure.h>` → `redeclared as different kind of entity`. Đổi lại: mỗi `.cpp` phải tự `#include <Arduino.h>`.
- **Arduino chỉ biên dịch đệ quy trong `src/`**, không vào thư mục con khác ở gốc. Module mới bắt buộc nằm trong `src/`.
- **Không có vòng lặp vô hạn trong task audio.** Từng có chế độ chỉnh nét ống kính chờ ký tự Serial — nó chặn luôn task audio, nút mất tác dụng, nhìn ngoài giống hệt thiết bị hỏng. Đã bỏ.

**Bộ nhớ**
- **Buffer lớn xin một lần lúc khởi động**, không xin theo lần bấm nút — PSRAM phân mảnh sẽ làm cấp phát thất bại giữa chừng và người dùng chỉ thấy máy im lặng.
- **`startAudio()` gọi SAU `startCameraServer()`** để camera + httpd chiếm RAM trước; phần PSRAM còn lại mới là phần audio thật sự được dùng.

**Audio**
- **I2S TX và RX dùng CHUNG một khối clock.** Không thể "tắt loa khi thu" bằng cách disable TX — tắt TX là tắt luôn đồng hồ của mic. `i2sSetSampleRate()` phải tắt cả hai chiều.
- **Server trả WAV 48 kHz, mic thu 16 kHz** → đổi sample rate hai lượt **mỗi lần bấm**, không phải trường hợp hiếm.
- **SD/MODE của amp thả nổi = phát ra (TRÁI + PHẢI)/2**, nên firmware **bắt buộc** ghi cả hai khe I2S; chỉ ghi khe trái là mất đúng 6 dB (vừa nhỏ vừa rè).

**Mạng**
- **`BODY_GAP_MS = 30000`, không phải 8000.** Server sinh tiếng theo TỪNG CÂU; khoảng nghỉ giữa hai câu có thể vượt 8 s. Để nhỏ thì firmware tưởng hết luồng và **vứt mất phần còn lại của câu trả lời** — đã xảy ra đúng như vậy.
- **`NET_TIMEOUT_MS = 90000`** vì server xử lý ~40 giây thật; Cloudflare tự ngắt ở 100 s (lỗi 524).
- **Không gửi `Expect: 100-continue`.** Server không trả `100 Continue` → treo tới timeout. Board tự dựng header nên không dính; test bằng `curl` phải thêm `-H "Expect:"`.
- **Bật IPv6 lên là mất quyền chọn đường** — nên `wifiConnect()` thử IPv4 trước, hỏng mới bật IPv6. `NetworkManager::hostByName()` của core 3.3.x: hễ interface có địa chỉ IPv6 toàn cục thì nó hỏi AAAA trước và dùng luôn, **không bao giờ thử IPv4**. Mà `enableIPv6(false)` chỉ xoá cờ `WANT_IP6`, **không thu hồi địa chỉ đã cấp** — lỡ bật rồi thì phải nối lại mạng mới gỡ được. Đã trả giá cả hai chiều: hotspot điện thoại **bắt buộc** phải có IPv6, còn router gia đình quảng bá IPv6 nhưng nhà mạng không định tuyến → bật lên là mọi kết nối chết ở đúng 15 s dù IPv4 vẫn tốt.
- **Đừng dùng `hostByName()` để kết luận "IPv4 hỏng"** — vì lý do trên, nó chưa từng thử IPv4. Muốn tách hai đường phải ép `ai_family` bằng tay qua `lwip_getaddrinfo()`, như `probeFamily()` trong `wifi_manager.cpp`.
- **Bắt tay TCP xong KHÔNG có nghĩa là đường đó dùng được.** Đo thật trên hotspot 4G: `TCP 443 OK (1340 ms)` nhưng TLS trên đúng socket đó chết sau **124 giây** (middlebox/CGNAT tự trả SYN-ACK thay server). Mọi phép thử dùng để *quyết định* đường đi phải bắt tay **TLS trọn vẹn** (`tlsReachable()`).
- **`setHandshakeTimeout()` là bắt buộc, đơn vị GIÂY.** Thiếu nó đã đo được **124 304 ms cho một lần "timeout 15 giây"** — nhìn ngoài giống hệt treo máy.
- **Gọi `enableIPv6(true)` sau khi đã liên kết thì không có địa chỉ** (SLAAC chỉ chạy lúc mới liên kết). Nên `wifiConnect()` bật cờ rồi `disconnect()` + `begin()` lại.
- **Phép thử IPv4 chạy HAI lần trước khi bỏ sang IPv6** — trượt oan một lần là không gỡ được trong phiên đó.
- **Hotspot điện thoại vừa bật thì chặng TCP/TLS hỏng vài chục giây đầu** dù Wi-Fi và DNS đã xanh. Đã đo: lần đầu hỏng ở 15 002 ms, vài phút sau cùng địa chỉ đó bắt tay xong trong 2 585 ms. Đừng sửa code vì một lần đo — chạy lại `w` sau 1–2 phút trước đã.

**Camera**
- **`XCLK = 16 MHz` chứ không 20 MHz** vì cáp FPC 75 mm (dài gấp 3 cáp gốc, 8 đường song song không bọc chống nhiễu). Nhanh quá → vệt sọc ngang, `EV-EOF-OVF`, hoặc `esp_camera_init()` trả 0x105.
- **Ảnh mờ vì PHÒNG TỐI, không phải cấu hình sai.** Mọi thiết lập phần mềm chỉ dịch cân giữa nhoè-chuyển-động và nhiễu-hạt, không tạo thêm ánh sáng. Lối thoát duy nhất: lắp đèn vào `FLASH_LED_PIN`.

---

## 9. Quy ước khi viết mã

- **Comment bằng tiếng Việt KHÔNG dấu** trong `.ino`/`.h`/`.cpp` — giữ nguyên phong cách hiện có. File `.md`/`.csv` thì có dấu bình thường.
- Comment giải thích **vì sao**, không phải **cái gì**. Dấu 🔴 đánh dấu chỗ đã sai một lần rồi và lý do không được sửa lại.
- Mỗi header mở đầu bằng khối `===` nói rõ khối đó **lo gì và KHÔNG lo gì**. Thêm file mới thì giữ đúng khuôn đó.
- Một khối không gọi sang khối ngang hàng — mọi phối hợp đi qua `audio_service`.

---

## 10. Tài liệu và nhánh thử nghiệm

| Tài liệu | Nội dung |
|---|---|
| [CLAUDE.md](CLAUDE.md) | context cho AI agent — bản cô đọng của README này |
| [HUONG_DAN_LAP_TUNG_BUOC.md](HUONG_DAN_LAP_TUNG_BUOC.md) | nhật ký bring-up 8 giai đoạn + số liệu đo thực tế. **Đọc trước khi tra code** — code trong đó là bản ở thời điểm đó, không phải bản cuối |
| [HUONG_DAN_MKE_M21_4G.md](HUONG_DAN_MKE_M21_4G.md) | lắp + unit test module 4G MKE-M21 (SIMCom A7680C) |
| [BangTomTat.csv](BangTomTat.csv) | bảng linh kiện đầy đủ |

Nguyên tắc bring-up đã theo suốt project: **không bao giờ cắm hai linh kiện mới rồi mới nạp code.** Thứ tự đã đi: camera → nút → mic → amp+loa → tích hợp full-duplex → gộp camera → pipeline server → tách module.

### Sketch thử nghiệm (độc lập, không đụng firmware chính)

| Sketch | Chứng minh điều gì |
|---|---|
| [test/Test_MKE_M21/](test/Test_MKE_M21/Test_MKE_M21.ino) | module 4G sống, đăng ký được mạng, gửi SMS, gọi điện thoại. UART1 trên GPIO 2/14, baud 9600 |
| [test/Test_MKE_M21_PPP/](test/Test_MKE_M21_PPP/Test_MKE_M21_PPP.ino) | **ESP32** thật sự có internet qua PPP (có IP, có DNS, mở được socket) — khác hẳn việc module tự gọi HTTP rồi đọc kết quả ra UART |

🔴 Hai sketch này **không nạp chung** — cả hai đều dùng UART1.
