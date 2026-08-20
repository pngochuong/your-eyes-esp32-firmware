# Port nguyen van bo giai ma ADPCM cua firmware sang Python, roi chay tren
# bitstream do ffmpeg sinh ra. Muc dich: tach "loi bo giai ma" khoi "loi dinh
# tuyen / lech khoi" ma khong can board.
import math, struct, subprocess, sys, os, random, tempfile

STEP = [
      7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
     19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
     50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
    876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
   2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
   5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
  15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
IDX = [-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8]

def clamp_s(v):  return 32767 if v > 32767 else (-32768 if v < -32768 else v)
def clamp_i(i):  return 0 if i < 0 else (88 if i > 88 else i)

def decode_sample(code, pred, idx):
    step = STEP[idx]
    delta = step >> 3
    if code & 4: delta += step
    if code & 2: delta += step >> 1
    if code & 1: delta += step >> 2
    pred = clamp_s(pred - delta if (code & 8) else pred + delta)
    idx = clamp_i(idx + IDX[code])
    return pred, idx

# adpcm_codec.cpp::adpcmDecodeBlock — sao y
def decode_block(src, max_samples):
    if len(src) < 4 or max_samples == 0: return []
    pred = struct.unpack_from('<h', src, 0)[0]
    idx = clamp_i(src[2])
    dst = [pred]
    n = 1
    p = 4
    while p < len(src) and n < max_samples:
        pred, idx = decode_sample(src[p] & 0x0F, pred, idx); dst.append(pred); n += 1
        if n < max_samples:
            pred, idx = decode_sample(src[p] >> 4, pred, idx); dst.append(pred); n += 1
        p += 1
    return dst

# pcm_source.cpp::adpcmNextBlock — gom DU blkBytes roi moi giai.
# Mo phong socket nha du lieu theo mieng ngau nhien de bat loi lech khoi.
def stream_decode(data, blk_bytes, blk_samples, chunk_rng=None):
    out = []
    pos = 0
    buf = bytearray()
    while True:
        while len(buf) < blk_bytes and pos < len(data):
            n = chunk_rng() if chunk_rng else blk_bytes
            n = min(n, blk_bytes - len(buf), len(data) - pos)
            buf += data[pos:pos+n]; pos += n
        if len(buf) == 0: break
        out += decode_block(bytes(buf), blk_samples)
        buf = bytearray()
        if pos >= len(data) and len(buf) == 0: break
    return out

# audio_format.cpp::readWavHeader — duyet khoi RIFF
def read_wav_header(b):
    assert b[0:4] == b'RIFF' and b[8:12] == b'WAVE'
    p = 12
    fmt = {}
    for _ in range(16):
        cid = b[p:p+4]; sz = struct.unpack_from('<I', b, p+4)[0]; p += 8
        if cid == b'fmt ':
            d = b[p:p+sz]
            fmt['tag']   = struct.unpack_from('<H', d, 0)[0]
            fmt['ch']    = struct.unpack_from('<H', d, 2)[0]
            fmt['rate']  = struct.unpack_from('<I', d, 4)[0]
            fmt['block'] = struct.unpack_from('<H', d, 12)[0]
            fmt['bits']  = struct.unpack_from('<H', d, 14)[0]
            fmt['spb']   = struct.unpack_from('<H', d, 18)[0] if sz >= 20 else 0
        elif cid == b'data':
            fmt['data_off'] = p; fmt['data_len'] = sz
            return fmt
        p += sz + (sz & 1)
    raise RuntimeError('khong thay data')

def snr(ref, test):
    n = min(len(ref), len(test))
    if n == 0: return -999.0
    s = sum(float(x)*x for x in ref[:n])
    e = sum((float(ref[i])-test[i])**2 for i in range(n))
    if e == 0: return 999.0
    return 10*math.log10(s/e) if s > 0 else -999.0

# ------------------------------------------------------------------ tin hieu thu
SR = 16000
DUR = 3.0
N = int(SR*DUR)
ref = []
for i in range(N):
    t = i/SR
    env = 0.5 + 0.5*math.sin(2*math.pi*3.0*t)          # bao hinh giong giong noi
    v = (math.sin(2*math.pi*180*t)*0.6
       + math.sin(2*math.pi*520*t)*0.3
       + math.sin(2*math.pi*1400*t)*0.15)
    ref.append(clamp_s(int(v*env*20000)))

# Ghi ra thu muc tam, khong rai file .wav vao repo.
d = tempfile.mkdtemp(prefix='adpcm_check_')
rp = os.path.join(d, 'ref.wav')
with open(rp, 'wb') as f:
    pcm = struct.pack('<%dh' % N, *ref)
    f.write(b'RIFF' + struct.pack('<I', 36+len(pcm)) + b'WAVEfmt ' +
            struct.pack('<IHHIIHH', 16, 1, 1, SR, SR*2, 2, 16) +
            b'data' + struct.pack('<I', len(pcm)) + pcm)

print('=== bo giai ma ADPCM cua firmware, chay tren bitstream ffmpeg ===\n')
for blk in (256, 1024):
    ep = os.path.join(d, 'enc%d.wav' % blk)
    subprocess.run(['ffmpeg','-y','-loglevel','error','-i',rp,
                    '-c:a','adpcm_ima_wav','-block_size',str(blk),ep], check=True)
    b = open(ep,'rb').read()
    h = read_wav_header(b)
    data = b[h['data_off']:h['data_off']+h['data_len']]
    spb = h['spb'] or ((h['block']-4)*2+1)
    print('block_size=%-5d ffmpeg ghi: tag=0x%04X block=%d spb=%d data=%d byte'
          % (blk, h['tag'], h['block'], spb, len(data)))

    got = stream_decode(data, h['block'], spb)
    print('   [dung duong]      %d mau -> SNR %.1f dB' % (len(got), snr(ref, got)))

    random.seed(1)
    got2 = stream_decode(data, h['block'], spb, lambda: random.randint(1,700))
    print('   [socket nha vun]  %d mau -> SNR %.1f dB' % (len(got2), snr(ref, got2)))

    # Loi gia dinh 1: di nham nhanh "ADPCM tho" -> header WAV bi an nhu du lieu
    got3 = stream_decode(b, h['block'], spb)
    print('   [nham nhanh raw, an ca header] SNR %.1f dB' % snr(ref, got3))

    # Loi gia dinh 2: server dung block khac voi con so board tin
    got4 = stream_decode(data, 256, (256-4)*2+1)
    print('   [board tin block=256] SNR %.1f dB' % snr(ref, got4))
    print()
