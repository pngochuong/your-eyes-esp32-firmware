# VisionCare — Project Context

Thiết bị hỗ trợ người khiếm thị chạy trên ESP32-S3: **giữ nút → chụp ảnh + thu giọng nói → POST lên server → phát câu trả lời ra loa**. Kèm một web server phụ để xem hình trực tiếp và chỉnh cảm biến.

Firmware Arduino (sketch `.ino` + thư mục `src/`). Không phải project PlatformIO, không có CMake, không có test tự động — kiểm chứng bằng biên dịch trong Arduino IDE và các lệnh chẩn đoán qua Serial.

---

## 1. Luồng chạy chính

```
setup()                      your-eyes-esp32-firmware.ino
  cameraStart()              không có camera → dừng hẳn, không dựng web server
  webLedBegin()
  wifiConnect(30s)           mất Wi-Fi vẫn đi tiếp (nút/mic/loa phải sống)
  startCameraServer()        chỉ khi có IP
  startAudio()               gọi SAU web server, để camera+httpd chiếm RAM trước
loop()                       chỉ trông chừng Wi-Fi, cứ 10 s một lần

task "audio" (ghim core 1)   audio_service.cpp — nơi DUY NHẤT biết trình tự
  consolePoll()              lệnh chẩn đoán Serial
  giữ BTN  → photoCapture() → recordWhileHeld() → normalizeRecording()
  nhả BTN  → adpcmEncode() nén 4:1 vào sendBuf (WAV IMA ADPCM dựng sẵn)
           → apiSendCapture() (multipart: image=JPEG, audio=WAV ADPCM)
           → playPcmStream() qua PcmSource (ADPCM / PCM thô / WAV / MP3)

core 0                       Wi-Fi/TCP + esp_http_server (stream MJPEG)
```

Các khối con (`camera_capture`, `audio_recorder`, `audio_dsp`, `api_client`, `audio_player`) **không biết gì về nhau**. Muốn đổi trình tự → sửa `audio_service.cpp`, đừng sửa các khối.

---

## 2. Phần cứng đang dùng (BOM)

| # | Linh kiện | Vai trò | Nguồn | GPIO / kết nối | Trạng thái |
|---|---|---|---|---|---|
| 1 | **Kit ESP32-S3 WROOM N16R8 CAM** (GOOUUU, pinout = ESP32S3_EYE) | MCU trung tâm, Wi-Fi | 5 V qua USB-C, regulator ra 3.3 V | xem pin map bên dưới | ✅ trong firmware |
| 2 | **MAX98357A** I2S Class-D amp | Phát câu trả lời ra loa | VIN 5 V (khuyến nghị) | BCLK=41, LRC/WS=42, DIN=21; SD/MODE thả nổi | ✅ |
| 3 | **INMP441** I2S MEMS mic | Thu giọng nói | **3.3 V — tuyệt đối không 5 V** | BCLK=41, WS=42, SD=47; L/R→GND (khe TRÁI) | ✅ |
| 4 | **Loa mini 3 W 8 Ω** | Ngõ ra tiếng | từ SPK± của MAX98357A | không nối GPIO | ✅ |
| 5 | **Camera OV2640 góc rộng 120° + cáp FPC 75 mm** | Ảnh cho OCR + mô tả cảnh + stream web | qua đế camera trên board | đã nối sẵn PCB, khai báo ở `camera_pins.h` | ✅ |
| 6 | **MKE-M02 Button Module** | Nút giữ-để-nói | tín hiệu TTL 3.3/5 V | SIG=GPIO1, `INPUT_PULLUP`, active LOW | ✅ |
| 7 | Breadboard 400 point | Prototype | — | — | chưa test |
| 8 | Jumper wire | Đấu nối | — | — | chưa test |

Nguồn: [BangTomTat.csv](BangTomTat.csv) (bảng đầy đủ: dòng tiêu thụ, kích thước, độ tin cậy từng số liệu).

### Pin map (chốt trong [app_config.h](app_config.h))

| Chức năng | GPIO |
|---|---|
| I2S BCLK (chung mic + amp) | 41 |
| I2S WS/LRC (chung) | 42 |
| I2S DOUT → amp DIN | 21 |
| I2S DIN ← mic SD | 47 |
| Nút nhấn | 1 (active LOW) |
| Camera XCLK / SIOD / SIOC / PCLK / VSYNC / HREF | 15 / 4 / 5 / 13 / 6 / 7 |
| Camera D0–D7 | 11, 9, 8, 10, 12, 18, 17, 16 |
| Đèn chiếu sáng (FLASH_LED_PIN) | **-1 = chưa lắp** |

Chân **còn trống**: 2, 14, 38, 39, 40, 48 (38–40 là thẻ SD, 48 là NeoPixel onboard — chỉ dùng khi chấp nhận bỏ hai thứ đó). Tránh: GPIO0/3/45/46 (strap), 19/20 (USB-JTAG).

### Cài đặt Arduino IDE (bắt buộc)

Board `ESP32S3 Dev Module` · PSRAM `OPI PSRAM` · Partition Scheme `Huge APP (3MB No OTA/1MB SPIFFS)` · Flash Size `16MB` · arduino-esp32 core 3.3.x.
Không chọn đúng PSRAM OPI + Huge APP thì firmware (~1.23 MB) **không nạp nổi**.

Thư viện: `esp32-camera`, `driver/i2s_std.h` (ESP-IDF, không dùng `ESP_I2S`), `WiFi`/`WiFiClientSecure`, `esp_http_server`, `libhelix-mp3` (đã chép sẵn vào `src/libhelix-mp3/`).

### Máy build — trạng thái thực tế

⚠️ Phần này trước đây mô tả một máy khác (user `PCPV`, sketch ở `e:\VisionCare`, board ở COM11). Đã đo lại trên máy hiện tại:

| Mục | Giá trị |
|---|---|
| Thư mục project | `d:\Study\innostar\your-eyes-esp32-firmware` |
| Thư mục data (core + tool) | `C:\Users\Ho Hoang Luan\AppData\Local\Arduino15` — **có đủ** |
| Core đã cài | `esp32:esp32` **3.3.10** |
| Toolchain | `esp-x32` 2601 (dùng cho core 3.x) + `s3-gcc` 2021r2-p5 (thừa từ core 2.x) |
| esptool | 4.5.1 và 5.3.0 |
| **Arduino IDE** | `D:\AppDownload\Arduino IDE\` — bản **2.3.10**. 🔴 Cài trên **ổ D**, không phải `C:\Program Files` hay `LOCALAPPDATA\Programs` — quét mấy chỗ đó là trượt. Lối tra chắc ăn: registry `Uninstall`, hoặc shortcut `%APPDATA%\...\Start Menu\Programs\Arduino IDE.lnk` |
| **arduino-cli** | `D:\AppDownload\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe` — **1.5.1**, đi kèm IDE. **KHÔNG có trong PATH**, phải gọi bằng đường dẫn đầy đủ |
| Thư viện cài riêng | không có — mọi thứ project dùng đều đi kèm core hoặc nằm trong `src/` |

CLI kèm IDE dùng chung `Arduino15` ở trên, `core list` cho thấy đúng `esp32:esp32 3.3.10` — không cần cài thêm gì.

### Tên thư mục phải khớp tên sketch

Arduino bắt buộc thư mục sketch cùng tên với file `.ino` trong nó, nếu không `compile` báo *"main file missing from sketch"*.

Đã xử: sketch chính tên **`your-eyes-esp32-firmware.ino`** (trước là `VisionCare.ino`, đổi bằng `git mv` nên history còn nguyên). Đổi tên thư mục repo thì phải đổi tên file `.ino` theo.

### Cổng COM

| Cổng | Là gì |
|---|---|
| **COM8** | ✅ **board** — `USB\VID_303A&PID_1001` (USB-Serial/JTAG gốc của ESP32-S3), trạng thái `OK`, đang cắm |
| COM7 | `USB-SERIAL CH340` — **thiết bị khác**, không phải board này |
| COM3, COM4, COM5, COM6 | Bluetooth ảo |

### Biên dịch + nạp

FQBN đã gộp sẵn các tuỳ chọn bắt buộc — thiếu một cái là không nạp nổi hoặc mất console:

```powershell
$cli = "D:\AppDownload\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"

# chi bien dich (da chay thanh cong)
& $cli compile -b "esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=huge_app,FlashSize=16M,CDCOnBoot=cdc" "d:\Study\innostar\your-eyes-esp32-firmware"

# bien dich + nap
& $cli compile --upload -p COM8 -b "esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=huge_app,FlashSize=16M,CDCOnBoot=cdc" "d:\Study\innostar\your-eyes-esp32-firmware"

& $cli monitor -p COM8 -c baudrate=115200
```

🔴 **`CDCOnBoot=cdc` là bắt buộc dù bảng Arduino IDE không nhắc.** Board nối PC qua **USB gốc của ESP32-S3** (`VID_303A&PID_1001`, USB-Serial/JTAG) — không có cầu UART trên board, nên **không có COM thứ hai**. Mặc định `CDCOnBoot=default` đẩy `Serial.print` ra UART0 (GPIO 43/44) → nạp xong máy chạy bình thường nhưng Serial **câm hoàn toàn**, rất dễ tưởng là firmware chết.

Mốc so sánh kích thước — lệch nhiều so với nó thì có gì đó vừa đổi:

| Bản dựng | Program storage | RAM tĩnh |
|---|---|---|
| không `CDCOnBoot` | 1228818 byte (39%) | 70268 byte (21%) |
| có `CDCOnBoot=cdc` + chẩn đoán Wi-Fi | 1246867 byte (39%) | 70656 byte (21%) |
| **+ `perf_probe`** (đo tại chỗ, 2026-08-15) | 1249727 byte (39%) | 70792 byte (21%) |
| **+ ADPCM hai chiều** (2026-08-15) | **1252187 byte (39%)** | **70800 byte (21%)** |
| **+ chọn đường phát theo byte thật** (2026-08-16) | **1252939 byte (39%)** | **70800 byte (21%)** |
| **+ pipeline 4 bước, đẩy ảnh sớm, cue** (2026-08-18) | 1291647 byte (41%) | 77192 byte (23%) |
| **+ ADPCM theo luồng, bỏ IPv6, gom miếng 4 KB** (2026-08-19) | **1290595 byte (41%)** | **77328 byte (23%)** |

Tra tên tuỳ chọn khác của board: `arduino-cli board details -b esp32:esp32:esp32s3`.

---

## 3. Cấu trúc mã

**Gốc thư mục = những gì người dùng sửa. `src/` = mã.**

| File gốc | Nội dung |
|---|---|
| [your-eyes-esp32-firmware.ino](your-eyes-esp32-firmware.ino) | **SSID + mật khẩu Wi-Fi** (dòng 37–38), rồi `setup()` + `loop()` |
| [app_config.h](app_config.h) | endpoint API, timeout mạng, chân GPIO, XCLK, thông số thu âm |
| [board_config.h](board_config.h) | chọn model camera (`CAMERA_MODEL_ESP32S3_EYE`) |
| [camera_pins.h](camera_pins.h) | sơ đồ chân camera theo model |
| [HUONG_DAN_LAP_TUNG_BUOC.md](HUONG_DAN_LAP_TUNG_BUOC.md) | nhật ký bring-up 8 giai đoạn + số liệu đo thực tế |
| [GHI_CHU_PHIEN_2026-08-19.md](GHI_CHU_PHIEN_2026-08-19.md) | ADPCM theo luồng, gỡ IPv6, các con số đo được và việc còn lại |
| [HUONG_DAN_SERVER.md](HUONG_DAN_SERVER.md) | **hợp đồng board ↔ server**: byte gửi lên, 4 định dạng nhận về, ngưỡng tốc độ nhả, các mốc timeout, lệnh ffmpeg/curl |
| [BangTomTat.csv](BangTomTat.csv) | bảng linh kiện đầy đủ |
| [README.md](README.md) | giới thiệu project |
| [HUONG_DAN_MKE_M21_4G.md](HUONG_DAN_MKE_M21_4G.md) | hướng dẫn module 4G MKE-M21 — **nhánh riêng, chưa nằm trong luồng chính** |
| [test/](test/) | sketch thử độc lập: `Test_MKE_M21`, `Test_MKE_M21_PPP`. Arduino **không** biên dịch thư mục này cùng sketch chính (chỉ `src/` mới được biên dịch đệ quy) |
| `VisionCare.zip` | bản đóng gói cũ — không phải nguồn, đừng sửa |

### `src/camera/`
| File | Việc |
|---|---|
| [camera_device](src/camera/camera_device.h) | dựng `camera_config_t`, `cameraStart()/cameraReinit()/cameraIsReady()`. **Nguồn duy nhất** của frame size + JPEG quality |
| [camera_tuning](src/camera/camera_tuning.h) | profile cảm biến cho OCR, `cameraWarmUp()`, in setting. Mọi lý do "vì sao đặt số này" nằm trong `.cpp` |
| [camera_capture](src/camera/camera_capture.h) | `photoCapture()` chụp một LOẠT rồi giữ khung nét nhất vào PSRAM; đèn chiếu sáng; `photoMeasureSharpness()` (~200 ms, chỉ dùng khi chẩn đoán) |

### `src/net/`
| File | Việc |
|---|---|
| [wifi_manager](src/net/wifi_manager.h) | `wifiConnect()`, `wifiSelfTest()` (Wi-Fi → DNS → TCP/TLS) |
| [http_body_reader](src/net/http_body_reader.h) | `BodyReader` đọc thân HTTP cả `Content-Length` lẫn `chunked` |
| [api_client](src/net/api_client.h) | `apiSendCapture()` — TLS, dựng multipart, đọc status + header. **Không phát gì ra loa**, trả `ApiReply` để người gọi tự đọc thân |

### `src/audio/`
| File | Việc |
|---|---|
| [audio_i2s](src/audio/audio_i2s.h) | mở kênh I2S full-duplex, `i2sSetSampleRate()`, `i2sWriteSilence()` |
| [audio_buffers](src/audio/audio_buffers.h) | `recBuf[]` (thu) + `sendBuf[]` (WAV ADPCM dựng sẵn để POST) + `stageBuf[]` (phát) — xin MỘT lần lúc khởi động, giữ đến khi tắt máy |
| [audio_recorder](src/audio/audio_recorder.h) | `recordWhileHeld()`, `measureNoiseOnly()` — chỉ đọc mic |
| [audio_dsp](src/audio/audio_dsp.h) | làm việc TẠI CHỖ trên `recBuf[]`: `quietestRms()`, `highBandRms()`, `noiseGate()`, `normalizeRecording()` |
| [adpcm_codec](src/audio/adpcm_codec.h) | IMA/DVI ADPCM 4-bit nén 4:1, dùng cho CẢ hai chiều. Thuần số nguyên, không cấp phát, không biết gì về HTTP/I2S |
| [audio_format](src/audio/audio_format.h) | `wavHeader()`, `wavHeaderAdpcm()`, `parseAudioFormat()`, `readWavHeader()` — thuần số liệu, không đụng phần cứng |
| [pcm_source](src/audio/pcm_source.h) | `PcmSource`: PCM thô lấy thẳng từ socket, hoặc giải nén ADPCM / MP3 (Helix) trước |
| [audio_player](src/audio/audio_player.h) | `playPcmStream()` vừa nhận vừa phát, đệm tự điều chỉnh; `playTone()` để chẩn đoán |
| [audio_service](src/audio/audio_service.h) | **điều phối** — file duy nhất biết trình tự nút → ảnh → tiếng → server → loa |

### `src/web/`, `src/diag/`, `src/util/`
| File | Việc |
|---|---|
| [web_server](src/web/web_server.h) | cổng 80: `/`, `/capture`, `/status`, `/control`, `/bmp`, `/xclk`, `/reg`, `/greg`, `/pll`, `/resolution` · cổng 81: `/stream` (MJPEG) |
| [web_index_html.h](src/web/web_index_html.h) | trang HTML nhúng (949 dòng, gzip trong mảng byte) |
| [web_led](src/web/web_led.h) | LED onboard `LED_GPIO_NUM` chạy PWM, chỉnh qua `/control?var=led_intensity`. **Khác** đèn chiếu sáng của `camera_capture` |
| [serial_console](src/diag/serial_console.h) | phân phối lệnh `c/m/t/w/s/n/r` |
| [audio_diag](src/diag/audio_diag.h) | `diagSlotCompare()`, `diagSpeakerTone()`, `diagNoiseVsCamera()` |
| [camera_diag](src/diag/camera_diag.h) | `diagCaptureTest()` chụp thử + đo độ nét |
| [mem_alloc](src/util/mem_alloc.h) | `bigAlloc()` — **cửa duy nhất** cấp phát vài trăm KB, ưu tiên PSRAM rồi lui về RAM trong |
| [perf_probe](src/util/perf_probe.h) | `perfBegin/perfMark/perfReport` đo từng chặng một lần bấm nút; `memReport()` tách **RAM nội** khỏi PSRAM (còn trống / khối liền lớn nhất / thấp nhất từng chạm). Chỉ ghi và in — gỡ hết lời gọi thì chương trình chạy y hệt |
| `src/libhelix-mp3/` | bộ giải mã MP3 Helix chép từ ESP8266Audio (RCSL/RPSL). **Vendored — không sửa** |

---

## 4. Sửa gì thì vào đâu

| Muốn đổi | File |
|---|---|
| Wi-Fi SSID / mật khẩu | [your-eyes-esp32-firmware.ino:37](your-eyes-esp32-firmware.ino#L37) |
| Endpoint API, timeout mạng | [app_config.h:23-50](app_config.h#L23-L50) |
| Chân GPIO I2S / nút / đèn | [app_config.h:76-101](app_config.h#L76-L101) |
| Tần số thu, trần thời gian thu | [app_config.h:109-113](app_config.h#L109-L113) |
| XCLK camera | [app_config.h:74](app_config.h#L74) |
| Model camera | [board_config.h:16](board_config.h#L16) |
| Độ phân giải / chất lượng JPEG / profile cảm biến | `src/camera/camera_device.cpp` + `camera_tuning.cpp` |
| Ngưỡng lọc ồn, độ khuếch đại bản thu | `src/audio/audio_dsp.cpp` |
| Kích thước khối ADPCM (đánh đổi chống lỗi ↔ phí 4 byte/khối) | `src/audio/adpcm_codec.h` — `ADPCM_BLOCK_BYTES` |
| Bỏ ADPCM, quay về gửi PCM thô | `encodeRecording()` trong `src/audio/audio_service.cpp` — chỗ duy nhất |
| Mức đệm khi phát, chống hụt | `src/audio/audio_player.cpp` |
| Trình tự một lần bấm nút | `src/audio/audio_service.cpp` |

**Quy ước:** `app_config.h` chỉ chứa thứ dùng chung nhiều khối. Các nút vặn riêng của một khối nằm ngay trong `.cpp` của khối đó, cạnh dòng mã dùng tới nó — đừng kéo lên `app_config.h`.

---

## 5. Ràng buộc đã trả giá để biết — đừng phá

- **Mọi file trong `src/` phải là `.cpp`, không `.ino`.** Arduino tự sinh prototype chèn lên đầu file gộp, trước `#include <WiFiClientSecure.h>` → `redeclared as different kind of entity`. Đổi lại: mỗi `.cpp` phải tự `#include <Arduino.h>`.
- **Arduino chỉ biên dịch đệ quy trong `src/`**, không vào thư mục con khác ở gốc. Module mới bắt buộc nằm trong `src/`.
- **I2S TX và RX dùng CHUNG một khối clock.** Không thể "tắt loa khi thu" bằng cách disable TX. `i2sSetSampleRate()` phải tắt cả hai chiều. Đã thử bật riêng RX trong một cặp full-duplex để né TX: `i2s_channel_read()` **chặn vĩnh viễn**, task đọc mic đứng im, nhìn ngoài giống hệt mic hỏng.
- **Bật I2S TX mà không ghi gì vào = loa RÈ TO liên tục.** Đệm DMA TX sâu `dma_desc_num × dma_frame_num` = 8 × 480 = **240 ms**, và lúc vừa `i2s_channel_enable()` thì nó rỗng; `auto_clear = true` không cứu được. Phải **mồi đầy** ~400 ms im lặng ngay sau khi enable, rồi mới duy trì đều. 🔴 Chỉ làm vế duy trì mà bỏ vế mồi thì **vẫn rè** — ghi bù 32 ms mỗi 32 ms đúng bằng tốc độ DMA tiêu thụ, nên bộ đệm không bao giờ đầy lên, cứ lơ lửng ở mức cạn và liên tục chạm đáy.
- **🔴 `pgm_read_word()` phá nát mọi PCM nhúng trong flash.** Nó trả `uint16_t`, nên mẫu `-1` (`0xFFFF`) thành `65535` — nửa âm của dạng sóng bị lật thành số dương khổng lồ. Triệu chứng: tiếng vừa **rè** vừa **gần như không ra tiếng nói**, dù biên độ trong file hoàn toàn bình thường. Trên ESP32 `PROGMEM` vốn vô nghĩa (flash ánh xạ thẳng vào không gian địa chỉ) — đọc mảng như RAM là đúng: `int32_t v = ARR[i];`. Chỉ chép nguyên mẫu code AVR sang mới dính.
- **Tách lỗi phần cứng khỏi lỗi dữ liệu ở đường loa bằng một sin 1 kHz sinh thẳng ra `int16_t`, biên độ ~28000.** Nghe to và trong thì amp/loa/nguồn đều tốt, lỗi nằm ở dữ liệu hoặc mức; nghe nhỏ hoặc rè thì mới đi soi `SD_MODE`, nguồn amp, dây loa. Không có phép thử này thì "nhỏ" và "hỏng" nhìn giống hệt nhau.
- **Server trả WAV 48 kHz, mic thu 16 kHz** → đổi sample rate hai lượt **mỗi lần bấm**, không phải trường hợp hiếm. Bảo server trả 16 kHz thì bỏ được cả hai lượt.
- **ADPCM: chỉ số bước (`stepIndex`) mang XUYÊN qua các khối lúc NÉN, chỉ `pred` mới đặt lại.** Đặt lại chỉ số về 0 mỗi 505 mẫu nghĩa là cứ 32 ms lại có một đoạn phải leo từ bước 7 lên mức tín hiệu thật — nghe thành tiếng lao xao đều đều. Khối vẫn tự giải mã độc lập được vì bộ giải đọc chỉ số từ phần đầu khối chứ không suy ra từ khối trước. ffmpeg cũng làm đúng vậy.
- **🔴 Bên NÉN phải tái dựng lại mẫu y hệt cách bên GIẢI sẽ làm, rồi lấy kết quả đó làm mốc cho mẫu kế tiếp** — không được lấy mẫu gốc. Lấy mẫu gốc thì sai số mỗi mẫu không bao giờ được bù, nó cộng dồn suốt 505 mẫu của khối và tiếng trôi hẳn khỏi đường bao.
- **`ffmpeg` KHÔNG cắt theo khối `fact`.** Đo được: 32000 mẫu vào → 32320 mẫu ra, tức nó trả nguyên cả phần đệm của khối cuối. Nên phần đệm phải tự nó vô hại: đệm bằng `0x08` (nibble thấp 8 = lùi bước/8, nibble cao 0 = tiến bước/8, triệt tiêu nhau) chứ không phải `0x00` — đệm toàn 0 thì mọi mẫu tiến một chiều, cộng dồn thành một bước DC ở cuối bản thu.
- **`nAvgBytesPerSec` của ta (8110) khác ffmpeg (16000) và ta đúng.** ffmpeg lấy `bit_rate/8` chứ không tính lại. Đã đo: ffmpeg vẫn đọc file của ta bình thường, vì mọi bộ giải mã đều lấy `blockAlign` + `wSamplesPerBlock` chứ không lấy trường này. Đừng "sửa" cho khớp ffmpeg.
- **Không có trình biên dịch host trên máy này** (`gcc`/`g++`/`clang`/`cl` đều không có) — nhưng CÓ `ffmpeg` và `python`. Cách kiểm chứng codec đã dùng, còn dùng lại được: chuyển ngữ thuật toán sang Python rồi đối chiếu hai chiều với ffmpeg. Bộ giải của ta trên bitstream ffmpeg phải **bit-exact** (đo được: 0/32320 mẫu lệch); ffmpeg đọc file ta nén phải ra **SNR ~37 dB**.
- **🔴 Đường phát chọn theo BYTE THẬT của thân, không theo HTTP header.** ADPCM không có mẫu đồng bộ — ranh giới khối hoàn toàn do `blockAlign` và điểm bắt đầu quyết định. Lệch một chút là mọi khối lấy 4 byte giữa dữ liệu làm `pred`/`idx`. Đo được (ffmpeg + Python, [test/adpcm_check.py](test/adpcm_check.py) — chạy lại bằng `python test/adpcm_check.py`): giải đúng đường **SNR 35.1 dB**; đi nhầm nhánh "thô" nên nuốt luôn 60 byte header WAV → **−11.3 dB**; server dùng block 1024 mà board đinh ninh 256 → **−10.7 dB**. Cả hai ca sai đều là **nhiễu thuần**, nghe ra đúng là "loa rè, rất rè", và trước đây **không in ra một dòng nào**. `parseAudioFormat()` đặt `raw = true` chỉ vì THẤY header `x-audio-format` — nó không thể biết thân đóng gói kiểu gì. Nên `sendAndPlay()` ngó 4 byte đầu bằng `bodySniff()`: `"RIFF"` thì đọc như WAV bất kể header nói gì, và kêu lên khi hai bên bất đồng. Đừng đảo lại thành tin header.
- **`BODY_GAP_MS = 30000`, không phải 8000.** Server sinh tiếng theo TỪNG CÂU; khoảng nghỉ giữa hai câu có thể vượt 8 s. Để nhỏ thì firmware tưởng hết luồng và **vứt mất phần còn lại của câu trả lời** — đã xảy ra đúng như vậy.
- **Server xử lý ~40 giây thật.** `NET_TIMEOUT_MS = 90000`; Cloudflare tự ngắt ở 100 s (lỗi 524). Người dùng nhả nút rồi phải đợi ~40 s — giới hạn của server, board không nhanh hơn được.
- **`XCLK = 16 MHz` chứ không 20 MHz** vì cáp FPC 75 mm (dài gấp 3 cáp gốc, 8 đường song song không bọc chống nhiễu). Nhanh quá → vệt sọc ngang, `EV-EOF-OVF`, hoặc `esp_camera_init()` trả 0x105.
- **Không gửi `Expect: 100-continue`.** Server không trả `100 Continue` → treo tới timeout. Board tự dựng header nên không dính; nhưng test bằng `curl` phải thêm `-H "Expect:"`.
- **Không có vòng lặp vô hạn trong task audio.** Từng có chế độ chỉnh nét ống kính chờ ký tự Serial — nó chặn luôn task audio, nút mất tác dụng, nhìn ngoài giống hệt thiết bị hỏng. Đã bỏ.
- **Buffer lớn xin một lần lúc khởi động**, không xin theo lần bấm nút — PSRAM phân mảnh sẽ làm cấp phát thất bại giữa chừng và người dùng chỉ thấy máy im lặng.
- **`startAudio()` gọi SAU `startCameraServer()`** để camera + httpd chiếm RAM trước; phần PSRAM còn lại mới là phần audio thật sự được dùng.
- **Ảnh mờ vì PHÒNG TỐI, không phải cấu hình sai.** Mọi thiết lập phần mềm chỉ dịch cân giữa nhoè-chuyển-động và nhiễu-hạt, không tạo thêm ánh sáng. Lối thoát duy nhất: lắp đèn vào `FLASH_LED_PIN`.
- **🔴 Nhánh IPv6 ĐÃ BỊ BỎ HẲN khỏi `wifiConnect()` (2026-08-19).** Đo từ PC nối vào đúng hotspot đang dùng: IPv4 bắt tay TLS 0.19 s, IPv6 không bao giờ nối nổi TCP (chết ở 21 s). Cả hai mạng thiết bị này chạy trên đều vậy — IPv6 chưa cứu được lần nào mà lần nào cũng phá. Muốn dựng lại thì điều kiện phải là *"phân giải được bản ghi A NHƯNG không bắt tay nổi"*, tuyệt đối không phải *"IPv4 không làm được gì"*: cả hai lần rơi vào nhánh đó đều chỉ vì resolver dở vài chục giây. Ghi chú cũ bên dưới giữ lại vì lý do vẫn đúng:
- **Bật IPv6 lên là mất quyền chọn đường.** `NetworkManager::hostByName()` của core 3.3.x có đoạn workaround: hễ interface mang địa chỉ IPv6 toàn cục thì nó hỏi AAAA trước và dùng luôn, **không bao giờ thử IPv4**. Mà `enableIPv6(false)` chỉ xoá cờ `WANT_IP6`, **không thu hồi địa chỉ đã cấp** — lỡ bật rồi thì phải nối lại mạng mới gỡ được. Đã trả giá cho cả hai chiều: hotspot điện thoại **bắt buộc** phải có IPv6 (IPv4 không ra internet), còn router `Ngoc Phat` quảng bá IPv6 nhưng nhà mạng không định tuyến → bật IPv6 lên là mọi kết nối chết ở đúng 15 s dù IPv4 vẫn tốt. Không chọn cứng bên nào được.
- **Đừng dùng `hostByName()` để kết luận "IPv4 hỏng"** — vì lý do trên, nó chưa từng thử IPv4. Muốn tách hai đường phải ép `ai_family` bằng tay qua `lwip_getaddrinfo()`, đúng như `probeFamily()` trong `wifi_manager.cpp`.
- **Bắt tay TCP xong KHÔNG có nghĩa là đường đó dùng được.** Đo thật trên hotspot 4G: `TCP 443 OK (1340 ms)` nhưng TLS trên đúng socket đó chết sau **124 giây**. Nhà mạng có middlebox/CGNAT tự trả SYN-ACK thay server. Mọi phép thử dùng để *quyết định* đường đi phải bắt tay **TLS trọn vẹn** (`tlsReachable()`), không được dừng ở TCP.
- **`setHandshakeTimeout()` là bắt buộc, đơn vị GIÂY.** Tham số timeout của `connect()` chỉ chặn ở mức socket; bắt tay TLS có đồng hồ riêng và rất dài. Thiếu nó đã đo được **124304 ms cho một lần "timeout 15 giây"** — nhìn ngoài giống hệt treo máy.
- **Gọi `enableIPv6(true)` sau khi đã liên kết thì không có địa chỉ.** SLAAC chỉ chạy lúc mới liên kết; bật cờ sau đó phải ngồi chờ router quảng bá định kỳ. Đã đo: chờ 6 s vẫn `KHONG CO`, còn nối lại mạng thì có địa chỉ trong ~2 s. Nên `wifiConnect()` bật cờ rồi `disconnect()` + `begin()` lại.
- **Phép thử IPv4 chạy HAI lần trước khi bỏ sang IPv6.** Trượt oan một lần là trả giá đắt: bật IPv6 rồi thì không gỡ được trong phiên đó, mà trên mạng chỉ định tuyến IPv4 (router gia đình) thì mọi kết nối sau đều chết ở 15 s.
- **Hotspot điện thoại vừa bật thì chặng TCP/TLS hỏng vài chục giây đầu** dù Wi-Fi và DNS đã xanh — link của nhà mạng chưa ổn định. Đã đo: lần đầu hỏng ở đúng 15002 ms, vài phút sau cùng địa chỉ đó bắt tay xong trong 2585 ms. Đừng đi sửa code vì một lần đo; chạy lại `w` sau 1–2 phút trước đã.
- `setInsecure()` đang được dùng cho TLS (không xác thực chứng chỉ) — đã biết, ghi trong hướng dẫn GĐ 6.

---

## 6. API contract

```
POST https://api.visioncare-host.uk/process        (443, TLS)
Content-Type: multipart/form-data
Accept: audio/wav;codec=ima_adpcm, audio/mpeg, audio/wav   (ưu tiên giảm dần)
  field "image" — capture.jpg,  image/jpeg
  field "audio" — record.adpcm, audio/x-adpcm-ima; rate=16000; channels=1; block=256
                  🔴 IMA/DVI ADPCM 4-bit mono 16 kHz, THÔ — KHÔNG có header
                  RIFF. Header WAV phải đi trước dữ liệu nhưng ba trường của
                  nó (RIFF size, data size, số mẫu `fact`) chỉ biết khi thu
                  xong, tức buộc mọi byte phải đợi nhả nút. Bỏ header đi thì
                  board nén và đẩy từng khối 256 B ngay trong lúc đang nói.
                  Server dựng lại header: `adpcm.ensure_wav()`.

Trả về 200 — board nuốt được cả bốn, xếp theo thứ tự nên dùng:
  audio/wav   fmt 0x0011  IMA ADPCM 4-bit mono   ← nhanh nhất, nên dùng
  audio/mpeg                                     → giải mã bằng Helix
  audio/wav   fmt 0x0001  PCM 16-bit
  PCM/ADPCM thô kèm header:
      x-audio-format: pcm_s16le;rate=16000;channels=1
      x-audio-format: ima_adpcm;rate=16000;channels=1;block=256
```
Nhánh `chunked` vẫn giữ làm dự phòng — Cloudflare có thể đổi bất cứ lúc nào.

### Băng thông từng hướng

| Chặng | Trước | Sau ADPCM |
|---|---|---|
| Bản thu 10 s lên server | 320 044 B | **81 212 B** (25.4%) |
| Câu trả lời về, mỗi giây tiếng | 96 KB/s (WAV 48k) | **8 KB/s** (ADPCM 16k) |

Đường về mới là chỗ ăn tiền: [audio_player.cpp](src/audio/audio_player.cpp) so tốc độ nguồn với tốc độ phát rồi tự chọn mức đệm. Ở 96 KB/s nó thường rơi vào nhánh "nguồn chậm hơn tốc độ phát → đợi nhận xong rồi phát". Ở 8 KB/s biên độ dư lớn tới mức luôn rơi nhánh đệm 400 ms.

### Phía server phải làm gì

Đọc bản thu: `ffmpeg -i record.wav -c:a pcm_s16le out.wav`, hoặc `soundfile.read()`. Không phải viết mã tay.

Trả câu trả lời: `ffmpeg -i tts.wav -ar 16000 -ac 1 -c:a adpcm_ima_wav -block_size 256 reply.wav`.

🔴 **Đừng dùng `audioop.lin2adpcm()` / `adpcm2lin()` của Python.** Nó là biến thể riêng của Python — không có phần đầu khối 4 byte, không tương thích fmt tag 0x0011. Dùng ffmpeg hoặc soundfile.

---

## 7. Lệnh chẩn đoán (gõ qua Serial, 115200)

| Phím | Việc |
|---|---|
| `c` | chụp thử + đo độ nét, không gửi đi đâu |
| `m` | nghe so sánh hai kiểu ghi khe I2S (đoán chế độ SD_MODE của amp) |
| `t` | phát sin sạch tăng dần biên độ, đo méo tiếng bằng chính mic |
| `w` | kiểm tra đường mạng theo chặng: Wi-Fi → DNS → TCP/TLS, rồi ép thử riêng IPv4 và IPv6 (chưa vào được mạng thì tự quét sóng thay) |
| `s` | quét mọi AP 2.4 GHz bắt được, đánh dấu cái trùng `WIFI_SSID` |
| `n` | đo nền nhiễu khi camera BẬT và khi camera TẮT |
| `r` | in bộ nhớ còn lại — RAM nội và PSRAM tách riêng, kèm khối liền lớn nhất |

Ngoài ra **mỗi lần bấm nút** tự in bảng thời gian 9 chặng (chụp → thu → chờ nhả → lọc + nén ADPCM → DNS+TLS → upload → server nghĩ → tiếng đầu ra loa → phát hết) kèm phần trăm của tổng, rồi in bộ nhớ. Cột phần trăm trả lời "nên đi sửa chặng nào"; chặng `server nghĩ` là phần board không can thiệp được.

---

## 8. Quy ước khi viết mã ở project này

- **Comment bằng tiếng Việt KHÔNG dấu** trong mã nguồn (`.ino`, `.h`, `.cpp`) — giữ nguyên phong cách hiện có. File `.md`/`.csv` thì có dấu bình thường.
- Comment giải thích **vì sao**, không phải **cái gì**. Dấu 🔴 đánh dấu chỗ đã sai một lần rồi và lý do không được sửa lại.
- Mỗi header mở đầu bằng một khối `===` nói rõ khối đó **lo gì và KHÔNG lo gì**. Thêm file mới thì giữ đúng khuôn đó.
- Một khối không gọi sang khối ngang hàng — mọi phối hợp đi qua `audio_service`.
