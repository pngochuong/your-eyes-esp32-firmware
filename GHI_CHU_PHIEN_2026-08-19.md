# Ghi chú phiên 2026-08-19 — gửi ≤1 s sau khi nhả nút, và gỡ IPv6

Nhánh: `net-stream-adpcm`. Đọc kèm [PLAN_VIET_LAI_MANG.md](PLAN_VIET_LAI_MANG.md).

---

## 1. Mục tiêu ≤1 s sau khi nhả nút — ĐÃ ĐẠT ở phần board kiểm soát được

Đo thật trên board (lệnh `p`, giả lập giữ nút 3 giây):

```
chup anh                        125 ms    3%
thu am + nen + day song song   3025 ms   76%
nen + day khoi cuoi               1 ms    0%   <-- ke tu luc tha nut
keu tieng da nhan               809 ms   20%
```

Trước phiên này chặng sau khi nhả nút là **toàn bộ 26 KB ADPCM** — trên hotspot
9 KB/s là gần 3 giây ngồi im. Giờ còn **1 khối 256 byte**.

### Làm thế nào

**Bỏ vỏ WAV ở bản thu gửi lên.** Header WAV phải đi trước dữ liệu, mà ba trường
của nó (`RIFF size`, `data size`, số mẫu trong `fact`) chỉ biết được khi đã thu
xong — tức bắt buộc phải đợi nhả nút mới gửi được byte đầu tiên. Bỏ header đi
thì không còn gì phải biết trước.

Hợp đồng mới với server (đã chốt với người dùng):

```
field "audio"  filename="record.adpcm"
Content-Type: audio/x-adpcm-ima; rate=16000; channels=1; block=256
thân: ADPCM 4-bit THÔ, không RIFF
```

Nhãn `Content-Type` do `audio_service` ghép từ chính `SR` và
`ADPCM_BLOCK_BYTES` mà bộ nén đang dùng — không viết tay, để hai bên không lệch.

**Nén và đẩy ngay trong lúc đang thu.** Chuỗi mới:

| File | Thay đổi |
|---|---|
| `adpcm_codec.{h,cpp}` | thêm `AdpcmEnc` + `adpcmEncodeBlock()` nén ĐÚNG một khối; `adpcmEncode()` cũ viết lại thành vòng lặp gọi nó — một cài đặt duy nhất |
| `audio_recorder.{h,cpp}` | `recordWhileHeld()` / `recordFixed()` nhận `RecChunkFn onChunk` — gọi lại ngay giữa hai lần đọc mic |
| `audio_service.cpp` | `onRecChunk()` lọc + nén + giao cho task mạng; `encodeFinish()` nén nốt phần lẻ sau khi nhả nút |
| `api_client.{h,cpp}` | `apiAudioOpen()` mở phần multipart lúc bắt đầu thu; `apiPushAudio(totalBytes, done)` gọi nhiều lần |

Cơ chế đồng bộ giữa hai task: **một biến đếm byte chỉ tăng**. Task audio là
người ghi duy nhất của `g_audFill`, task mạng là người đọc duy nhất và chỉ đẩy
khoảng `[đã_gửi, g_audFill)`. Không hàng đợi, không mutex. Thứ tự đọc/ghi cờ
`done` được ghi rõ trong mã — đọc sai thứ tự là mất khối cuối.

### Đánh đổi đã chấp nhận

Hai bước DSP cần TRỌN bản thu nên phải bỏ:

| Bước | Số phận | Vì sao |
|---|---|---|
| Lọc thông cao 76 Hz | **giữ** | chạy theo luồng chính xác bằng chạy một lần; lệch một chiều ăn mất tầm động của ADPCM |
| Cổng chặn ồn | **bỏ** | nó lọt NGƯỢC để mở cổng trước khi từ bắt đầu — cần biết tương lai |
| Khuếch đại | **đổi** | đỉnh của cả đoạn chưa biết → dùng đỉnh CHẠY, hệ số chỉ giảm, không bao giờ kẹp trần |

ADPCM tự nó đã là một bộ AGC (chỉ số bước bám biên độ), nên mất hai bước trên
chủ yếu là mất một chút lợi cho bộ nhận dạng giọng nói bên server, không phải
mất tỷ số nén.

🔴 **Bỏ qua 100 ms đầu khi đo đỉnh.** INMP441 nhả vài mẫu bão hoà ngay sau khi
bật kênh I2S. Đỉnh chạy thì chỉ tăng, nên một mẫu −32768 lọt vào là hệ số bị
ghim ở 1 suốt cả câu. Đo được đúng như vậy: `Dinh truoc khuech dai = 32768,
he so = 1`. Sau khi sửa: `430 → he so 18`, `5719 → he so 1`, tuỳ mức tiếng.

### Việc còn lại của mục tiêu này

Ảnh đi TRƯỚC tiếng trong multipart, và task mạng phải ghi xong ảnh mới ghi tiếng.
Nếu người dùng chỉ nói 1 giây mà ảnh 65 KB chưa đẩy xong thì phần ảnh còn lại
cộng thẳng vào khoảng sau khi nhả nút. Trên đường nhanh không thành vấn đề; trên
hotspot 9 KB/s thì phải hạ cỡ ảnh. Bảng đánh đổi nằm trong `camera_device.cpp`.

**Chưa đo được đầu-cuối** vì đường TLS trên hotspot đang hỏng — xem mục 3.

---

## 2. Phía server (`d:\Study\innostar\Sever_test`) — đã sửa và đã thử

| File | Thay đổi |
|---|---|
| `pipeline/adpcm.py` | thêm `ensure_wav(data, content_type)`: thân bắt đầu bằng `RIFF` thì trả nguyên; không thì đọc `rate`/`block` từ Content-Type, cắt cho tròn khối, dựng lại 60 byte header |
| `app.py` | `/process` gọi `adpcm.ensure_wav(audio.file.read(), audio.content_type)` |

Cắt cho tròn khối là bắt buộc: ADPCM không có mẫu đồng bộ, một khối cụt làm bộ
giải đọc 4 byte giữa dữ liệu thành predictor/step index và nhả ra nhiễu thuần.

Đã thử vòng tròn PCM → ADPCM thô → `ensure_wav` → `decode_wav`: **SNR 33 dB**,
file WAV cũ đi qua nguyên vẹn, đuôi lẻ bị cắt đúng.

🔴 **Server phải khởi động lại mới nạp** (uvicorn không có `--reload`). Trước khi
khởi động lại, một POST ADPCM thô vẫn trả **HTTP 200 + thân RIFF** — nhưng đó là
câu **báo lỗi đóng sẵn**, không phải câu trả lời thật. Cách phân biệt: xem
`storage/requestNNNN.wav`, nếu nó không bắt đầu bằng `RIFF` thì `ensure_wav`
chưa chạy.

---

## 3. Mạng — bỏ hẳn IPv6, chữa IPv4

### Đo được

Từ PC nối vào **đúng** hotspot iPhone "Huong", cùng thời điểm:

| Đường | Kết quả |
|---|---|
| IPv4 | **200, bắt tay TLS 0.19 s, trọn request 0.39 s** |
| IPv6 | **TCP không bao giờ nối được**, chết ở 21 s |

Cả hai bản ghi A của Cloudflare đều tốt từ PC: `172.67.176.233` tls=0.125 s,
`104.21.67.134` tls=0.137 s. Ghi chú cũ "172.67.176.233 treo vĩnh viễn" **không
còn đúng** — không phải lỗi địa chỉ.

### Đã sửa

**a) DNS: đặt resolver công cộng lên ô 0.** Bản trước chỉ lấp ô trống, danh sách
ra `[0]=172.20.10.1 [1]=8.8.8.8 [2]=1.1.1.1` — nhìn thì đúng, nhưng lwip hỏi ô 0
trước và thử lại vài lượt mới sang ô kế, nên một resolver dở ở ô 0 nuốt trọn
ngân sách. Đo được: hỏi bản ghi A chết ở **18000 rồi 15000 ms**, trong khi cùng
tên miền đó hỏi từ PC mất **17 ms**. Sau khi đẩy 8.8.8.8 lên ô 0: **323–1533 ms**.

Đánh đổi: mất khả năng phân giải tên trong LAN. Board này chỉ phân giải duy nhất
một tên miền công cộng nên không mất gì thật.

**b) Bỏ hẳn nhánh IPv6 dự phòng.** Bật IPv6 lên là đổi chác KHÔNG gỡ lại được
trong phiên: `hostByName()` hỏi AAAA trước khi giao diện có địa chỉ v6 toàn cục,
còn `enableIPv6(false)` chỉ xoá cờ chứ không thu hồi địa chỉ. Board tự đẩy mình
vào đường hố đen chỉ vì resolver dở vài chục giây.

Muốn dựng lại thì điều kiện phải là *"phân giải được bản ghi A NHƯNG không bắt
tay nổi"*, tuyệt đối không phải *"IPv4 không làm được gì"*. Biến `v4DnsOk` trong
`wifi_manager.cpp` giữ đúng phân biệt đó.

**c) Một lần bắt tay cho cả phiên, và GIỮ nó.** `wifiConnect()` trước đây bắt tay
trọn vẹn một lần để "chứng minh đường đi" rồi `stop()` ngay. Đo được: lần bắt tay
ấy **xong trong 5496 ms** rồi bị vứt; đến lúc bấm nút, bắt tay lại trên **đúng
địa chỉ đó** chết ở 8059 ms. Phép thử không hề dự báo được lần thật — nó chỉ
tiêu mất một lần bắt tay đáng lẽ dùng được, và làm phân mảnh RAM nội.

Giờ: `wifiConnect()` chỉ nối Wi-Fi + đặt DNS. `.ino` gọi `apiResolve()` rồi
`apiWarmUp()` — cùng một lần bắt tay, nhưng đánh dấu phiên dùng lại được. Đặt
TRƯỚC `startCameraServer()` vì lúc đó RAM nội thoáng nhất (khối liền lớn nhất
147 KB, sau khi httpd + audio lấy phần của chúng còn 106 KB).

Đã xác nhận chạy: `Bat tay TLS truoc XONG (20587 ms) — giu phien cho lan bam
dau`, rồi lần bấm in `Dung lai phien TLS cu (mo 6 giay truoc) — bo qua bat tay`.

**d) `NET_HANDSHAKE_S` 8 → 15.** Bắt tay thành công đo được trên board: 1321,
2108, 3212, 3321, 5117, 5496, **20586** ms. Ngân sách 8 giây loại oan lần 20 s.
PC làm việc đó trong 0.13 s — board chậm và giật, không phải mạng.

**e) `tlsReachableOn()` in mã lỗi mbedtls.** Trước đó "hỏng" chỉ là một chữ;
`TLS hong (0 ms)` và `TLS hong (8003 ms)` nhìn giống nhau trong khi hai hướng
sửa ngược nhau.

---

## 4. 🔴 Việc CÒN LẠI — lỗi chưa gỡ được

**Bắt tay TLS xong, rồi ghi 0 byte và mất kết nối.**

```
TCP/TLS 172.67.176.233 OK (2108 ms)        <- hoac dung lai phien cu
ghi dung o 0/65955 byte sau 15490 ms
ghi hong: connected=0, mbedtls 54 ... / mbedtls 48 ...
```

Đã loại trừ:

- **Không phải timeout của ta.** `send_ssl_data()` trong core bỏ cuộc theo
  `socket_timeout`, mà `socket_timeout` lấy từ `_timeout` = `NET_TIMEOUT_MS`
  = 60000 ms. Dòng log in ra khi `!connected()`, tức socket đã chết.
- **Không phải cỡ ảnh, không phải RAM.** Hạ ảnh xuống SVGA 39 KB chết y hệt:
  `ghi dung o 0/39691 byte sau 15729 ms`. Khối liền lớn nhất lúc đó 51 KB,
  thừa cho mbedtls (~45 KB).
- **Không phải địa chỉ Cloudflare.** Cả hai bản ghi A đều tốt từ PC cùng mạng.
- **Không phải IPv6.** Đã gỡ hẳn.
- **Không phải phiên mới hay phiên cũ.** Hỏng cả khi vừa bắt tay xong lẫn khi
  dùng lại phiên đã giữ.

Điểm chung mọi lần: **đúng 0 byte qua được**, và khoảng 15.5 giây. Nghi tiếp
theo là middlebox/CGNAT của hotspot 4G đóng luồng ngay sau bắt tay — hợp với
việc bắt tay lúc nhanh (2 s) lúc chậm (20 s) trên cùng địa chỉ, trong khi PC
cùng mạng ổn định 0.13 s.

Phép thử tiếp theo nên làm: chạy lại trên **mạng FTTH có dây (`Ngoc Phat`)** để
tách hẳn "board hỏng" khỏi "hotspot hỏng". Nếu FTTH ghi được byte thì mọi thứ ở
trên đã xong và chỉ còn chuyện đường truyền; nếu FTTH cũng 0 byte thì lỗi nằm
trong cách board ghi lên socket TLS.

---

## 5. Một mất mát trong phiên này

Tôi chạy `git checkout -- src/net/api_client.cpp` để hoàn tác một sửa đổi hỏng
của chính mình, nhưng file đó chưa từng được commit — lệnh đó đưa nó về bản
commit đầu tiên và **xoá mất toàn bộ việc của phiên trước** (pipeline 4 bước,
task `netsend`, keep-alive). Đã dựng lại đầy đủ từ bản in ra trước đó và đối
chiếu lại từng chỗ đã trả giá (`hostByName() == 1`, `setHandshakeTimeout`,
`setConnectionTimeout` trước `connect`, `keep-alive`, `g_taskAlive`).

Vì vậy việc đầu tiên sau khi dựng lại là **commit** — đúng như mục 5.1 của plan
đã dặn mà phiên này làm muộn.
