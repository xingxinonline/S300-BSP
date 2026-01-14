#!/usr/bin/env python3
import argparse
from pathlib import Path
from PIL import Image

TEMPLATE_RGB565 = """
#include "lvgl.h"

static const uint16_t {sym}_pixels[{w} * {h}] = {{
{data}
}};

const lv_image_dsc_t {sym} = {{
    .header = {{
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = {w},
        .h = {h},
        .stride = {w} * 2,
    }},
    .data_size = sizeof({sym}_pixels),
    .data = (const uint8_t*){sym}_pixels,
}};
"""

# RGB565A8: first the color plane (w*h*2 bytes, stride = w*2), then the alpha plane (w*h bytes)
TEMPLATE_RGB565A8 = """
#include "lvgl.h"

static const uint8_t {sym}_data[{data_size}] = {{
{data}
}};

const lv_image_dsc_t {sym} = {{
    .header = {{
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .w = {w},
        .h = {h},
        .stride = {w} * 2,
    }},
    .data_size = sizeof({sym}_data),
    .data = (const uint8_t*){sym}_data,
}};
"""


def to_rgb565(pixel):
    r, g, b, a = pixel if len(pixel) == 4 else (*pixel, 255)
    # premultiply or ignore alpha: here we just ignore alpha (treat as opaque)
    r5 = (r >> 3) & 0x1F
    g6 = (g >> 2) & 0x3F
    b5 = (b >> 3) & 0x1F
    return (r5 << 11) | (g6 << 5) | b5


def convert_png_to_c(png_path: Path, out_dir: Path, symbol: str, fmt: str):
    im = Image.open(png_path).convert("RGBA")
    w, h = im.size
    pixels = list(im.getdata())

    if fmt == "rgb565":
        rgb565 = [to_rgb565(p) for p in pixels]
        # Format as 16 per line
        lines = []
        for i in range(0, len(rgb565), 16):
            chunk = rgb565[i:i+16]
            lines.append(", ".join(f"0x{v:04x}" for v in chunk))
        data = ",\n".join(lines)
        c_code = TEMPLATE_RGB565.format(sym=symbol, w=w, h=h, data=data)
    elif fmt == "rgb565a8":
        # Build single byte array: color plane (little-endian RGB565) then alpha plane (A8)
        color_bytes = bytearray()
        alpha_bytes = bytearray()
        for r, g, b, a in pixels:
            v = to_rgb565((r, g, b, a))
            color_bytes.append(v & 0xFF)
            color_bytes.append((v >> 8) & 0xFF)
            alpha_bytes.append(a)
        combined = bytes(color_bytes + alpha_bytes)
        # format 16 bytes per line
        hex_bytes = [f"0x{b:02x}" for b in combined]
        lines = []
        for i in range(0, len(hex_bytes), 16):
            lines.append(", ".join(hex_bytes[i:i+16]))
        data = ",\n".join(lines)
        data_size = len(combined)
        c_code = TEMPLATE_RGB565A8.format(
            sym=symbol, w=w, h=h, data=data, data_size=data_size)
    else:
        raise ValueError(f"Unsupported format: {fmt}")

    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / f"{symbol}.c"
    out_path.write_text(c_code)
    print(f"Generated {out_path} ({fmt}, {w}x{h})")


def gen_symbol(name: str) -> str:
    # make a safe C symbol
    s = name.lower()
    s = ''.join(ch if ch.isalnum() else '_' for ch in s)
    if s[0].isdigit():
        s = '_' + s
    return s


def main():
    ap = argparse.ArgumentParser(
        description="Convert PNG to LVGL C asset (rgb565 or rgb565a8)")
    ap.add_argument("inputs", nargs="+", help="PNG files to convert")
    ap.add_argument("--out", default="../Assets/generated",
                    help="Output directory")
    ap.add_argument("--prefix", default="img_", help="Symbol name prefix")
    ap.add_argument("--format", choices=["rgb565", "rgb565a8"], default="rgb565",
                    help="Output LVGL color format: rgb565 (no alpha) or rgb565a8 (separate 8-bit alpha)")
    args = ap.parse_args()

    out_dir = Path(args.out).resolve()
    for p in args.inputs:
        png_path = Path(p)
        sym = gen_symbol(args.prefix + png_path.stem)
        convert_png_to_c(png_path, out_dir, sym, args.format)


if __name__ == "__main__":
    main()
