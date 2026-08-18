#ifndef ADPCM_CODEC_H
#define ADPCM_CODEC_H

// ============================================================================
// IMA/DVI ADPCM 4-bit — nen 4:1, dung cho CA hai chieu
// ============================================================================
// Khoi nay chi bien doi so: PCM 16-bit <-> nibble 4-bit. No KHONG dung toi
// phan cung, KHONG cap phat, KHONG biet gi ve HTTP hay I2S. Ai goi thi tu lo
// bo dem va tu dong header WAV (audio_format.*).
//
// Vi sao ADPCM chu khong phai MP3/Opus:
//   - Bang thong tut 4 lan (16 kHz mono: 32 KB/s -> 8 KB/s) ma gia phai tra
//     gan bang khong: vai chuc lenh so nguyen cho moi mau, khong dung so thuc,
//     khong bang tra lon, khong RAM trang thai.
//   - KHONG co do tre thuat toan. MP3 phai gom du mot khung 1152 mau (26 ms)
//     truoc khi xa ra byte dau tien, va ben ENCODE cung vay — nhan len ca hai
//     dau cua duong truyen. ADPCM xa ra ngay tung mau. Voi thiet bi ma nguoi
//     dung dang dung cho tieng noi, do tre dang gia hon ty so nen.
//   - Server encode ADPCM ton ~0; encode MP3 tung cau lai cong them thoi gian
//     vao dung cai chang ~40 giay von da la cho lau nhat.
//
// 🔴 Dung dinh dang CHUAN (WAVE_FORMAT_DVI_ADPCM = 0x0011), khong bia bien the
// rieng. Nho vay server giai/ma bang ffmpeg hoac soundfile la xong, khong phai
// viet ma tay. Luu y: audioop.lin2adpcm() cua Python KHONG dung chuan nay —
// no bo phan dau khoi. Dung ffmpeg/soundfile, dung audioop.
//
// Chi ho tro MONO. ADPCM stereo cai rang tung nibble theo kenh; duong nay chi
// bao gio chay mono nen khong viet phan chua bao gio thu duoc.
// ============================================================================
#include <Arduino.h>

// Kich thuoc mot khoi, tinh bang byte. 256 la gia tri quen thuoc nhat cua
// dinh dang nay nen moi bo giai ma deu da gap.
//
// Moi khoi tu mang theo trang thai khoi dau (mau dau + chi so buoc) o 4 byte
// dau, nen mot khoi hong khong keo theo phan con lai — quan trong khi du lieu
// di qua mang. Khoi cang nho thi cang chong loi tot nhung phi 4 byte cang lon:
// 256 byte -> phi 1.6%.
#define ADPCM_BLOCK_BYTES    256

// So mau chua trong mot khoi day: 1 mau nam thang trong phan dau, phan con lai
// moi byte hai nibble.
#define ADPCM_BLOCK_SAMPLES  ((ADPCM_BLOCK_BYTES - 4) * 2 + 1)   // 505

// So byte can de chua `samples` mau sau khi nen (da tinh ca phan dem cho khoi
// cuoi tron khoi). Dung de xin bo dem luc khoi dong.
size_t adpcmEncodedBytes(size_t samples);

// Trang thai ben NEN, mang xuyen qua cac khoi.
//
// 🔴 Chi so buoc PHAI di xuyen khoi. Dat lai ve 0 moi 505 mau nghia la cu
// 32 ms lai co mot doan phai leo tu buoc 7 len muc tin hieu that — nghe thanh
// tieng lao xao deu deu. Khoi van tu giai ma doc lap duoc vi bo giai doc chi so
// tu phan dau khoi chu khong suy ra tu khoi truoc. ffmpeg cung lam vay.
struct AdpcmEnc {
  int idx;      // chi so buoc hien tai
};

// Dat lai trang thai truoc mot ban thu moi.
inline void adpcmEncReset(AdpcmEnc &st) { st.idx = 0; }

// Nen DUNG MOT khoi. `samples` tu 1 toi ADPCM_BLOCK_SAMPLES; thieu thi phan
// con lai duoc dem cho tron khoi. Tra ve so byte da ghi (luon
// ADPCM_BLOCK_BYTES, hoac 0 khi samples == 0).
//
// Ham nay ton tai de nen duoc NGAY trong luc dang thu: cu du 505 mau la sinh
// ra mot khoi 256 byte va day len mang, thay vi doi thu xong het. Nho vay luc
// nguoi dung nha nut chi con mot khoi cuoi phai day.
size_t adpcmEncodeBlock(AdpcmEnc &st, const int16_t *pcm, size_t samples,
                        uint8_t *dst);

// Nen tron `samples` mau mono vao `dst`. Tra ve so byte da ghi — luon la boi
// so cua ADPCM_BLOCK_BYTES. `dst` phai rong it nhat adpcmEncodedBytes(samples).
size_t adpcmEncode(const int16_t *pcm, size_t samples, uint8_t *dst);

// Giai MOT khoi. `srcBytes` co the nho hon kich thuoc khoi day — khoi cuoi
// cua luong thuong bi cat. Tra ve so MAU da ghi vao `dst`.
size_t adpcmDecodeBlock(const uint8_t *src, size_t srcBytes,
                        int16_t *dst, size_t maxSamples);

#endif  // ADPCM_CODEC_H
