"""Sinh src/audio/cue_clips_data.h tu thu muc cues/ (bo clip cua server).

Chay lai moi khi server doi bo clip (doi chu, tang CUE_VERSION):

    python tools/gen_cue_clips.py            # mac dinh doc cues/, ghi src/audio/cue_clips_data.h
    python tools/gen_cue_clips.py --cues D:/duong/khac/assets/cues

Viec cua script:
  - doc manifest.json, kiem sha256 + so byte cua TUNG file (spec muc 7);
  - kiem dinh dang: WAV IMA ADPCM (0x0011), mono, 16000 Hz, khoi 256 byte;
  - bo header RIFF, chi nhung khoi "data" + so mau that (khoi "fact"), vi
    firmware giai ma thang tu flash, khong can doc lai header luc chay;
  - ghi CUE_VERSION lay tu manifest — firmware khai so nay trong `hello`.

Sai bat ky dieu gi la dung han, khong sinh file: mot clip hong nam trong
flash thi chi phat hien ra khi nguoi dung nghe tieng re.
"""
import argparse
import hashlib
import io
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def parse_wav(raw, name):
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        sys.exit(f"{name}: khong phai WAV")
    pos, fmt, data, samples = 12, None, None, None
    while pos + 8 <= len(raw):
        cid, size = raw[pos:pos + 4], struct.unpack("<I", raw[pos + 4:pos + 8])[0]
        body = raw[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif cid == b"fact":
            samples = struct.unpack("<I", body[:4])[0]
        elif cid == b"data":
            data = body
        pos += 8 + size + (size & 1)
    if fmt is None or data is None:
        sys.exit(f"{name}: thieu khoi fmt/data")
    tag, ch, rate, _, align, _ = fmt
    if (tag, ch, rate, align) != (0x11, 1, 16000, 256):
        sys.exit(f"{name}: can IMA ADPCM mono 16 kHz khoi 256, gap "
                 f"tag=0x{tag:04X} ch={ch} rate={rate} block={align}")
    if samples is None:
        samples = (len(data) // 256) * 505
    return data, samples


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cues", default=os.path.join(ROOT, "cues"))
    ap.add_argument("--out", default=os.path.join(ROOT, "src", "audio", "cue_clips_data.h"))
    a = ap.parse_args()

    with io.open(os.path.join(a.cues, "manifest.json"), encoding="utf-8") as f:
        man = json.load(f)
    ver = int(man["cue_version"])
    clips = man["clips"]

    out = []
    out.append("// ============================================================================")
    out.append("// SINH BANG MAY bang tools/gen_cue_clips.py — DUNG SUA TAY.")
    out.append("// ============================================================================")
    out.append(f"// Nguon: cues/manifest.json, cue_version = {ver}, {len(clips)} clip.")
    out.append("// Moi mang la khoi \"data\" cua file WAV IMA ADPCM 16 kHz mono, khoi 256 byte,")
    out.append("// da kiem sha256 voi manifest luc sinh. Chi cue_clips.cpp duoc include file nay.")
    out.append("// ============================================================================")
    out.append("#pragma once")
    out.append("#include <Arduino.h>")
    out.append("")
    out.append(f"#define CUE_VERSION {ver}")
    out.append("")

    table, total = [], 0
    for cid, meta in clips.items():
        path = os.path.join(a.cues, meta["file"])
        with open(path, "rb") as f:
            raw = f.read()
        if len(raw) != meta["bytes"]:
            sys.exit(f"{cid}: {len(raw)} byte, manifest ghi {meta['bytes']}")
        if hashlib.sha256(raw).hexdigest() != meta["sha256"]:
            sys.exit(f"{cid}: sha256 khong khop manifest")
        data, samples = parse_wav(raw, cid)
        total += len(data)
        sym = "CUE_" + "".join(ch if ch.isalnum() else "_" for ch in cid)
        out.append(f"static const uint8_t {sym}[{len(data)}] = {{")
        for i in range(0, len(data), 24):
            out.append("  " + ",".join(f"0x{b:02X}" for b in data[i:i + 24]) + ",")
        out.append("};")
        table.append((cid, sym, len(data), samples))

    out.append("")
    out.append("struct CueClipData { const char *id; const uint8_t *data; uint32_t bytes; uint32_t samples; };")
    out.append("static const CueClipData CUE_CLIP_TABLE[] = {")
    for cid, sym, n, s in table:
        out.append(f'  {{ "{cid}", {sym}, {n}, {s} }},')
    out.append("};")
    out.append(f"static const size_t CUE_CLIP_COUNT = {len(table)};")
    out.append("")

    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))
    print(f"OK: {len(table)} clip, {total} byte ADPCM, cue_version {ver} -> {a.out}")


if __name__ == "__main__":
    main()
