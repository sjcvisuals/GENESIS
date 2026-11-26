#!/usr/bin/env python3
"""Generate GENESIS Editor / Player multi-resolution Windows .ico assets.

Brand: dark charcoal plate + soft cyan-teal accent (matches EditorTheme).
Outputs SVG sources + .ico (16/32/48/256) under assets/branding/.
"""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "branding"
SIZES = (16, 32, 48, 256)

# EditorTheme accent: ImVec4(0.34, 0.78, 0.82) on deep charcoal.
BG = (10, 11, 14, 255)
BG_EDGE = (22, 24, 28, 255)
ACCENT = (87, 199, 209, 255)  # ~#57C7D1
ACCENT_HI = (112, 219, 230, 255)
ACCENT_LO = (41, 102, 112, 255)
MUTED = (120, 128, 138, 255)


def _rr(draw: ImageDraw.ImageDraw, box, radius: float, fill) -> None:
    draw.rounded_rectangle(box, radius=radius, fill=fill)


def _draw_plate(draw: ImageDraw.ImageDraw, size: int) -> None:
    pad = max(1, size // 32)
    radius = max(2, size * 0.22)
    _rr(draw, [pad, pad, size - 1 - pad, size - 1 - pad], radius, BG_EDGE)
    inset = max(1, size // 18)
    _rr(
        draw,
        [pad + inset, pad + inset, size - 1 - pad - inset, size - 1 - pad - inset],
        max(1, radius - inset),
        BG,
    )


def _g_path(cx: float, cy: float, r: float) -> list[tuple[float, float]]:
    """Open geometric G: three-quarter ring + inward bar (no fill needed for stroke)."""
    pts: list[tuple[float, float]] = []
    # Arc from ~40° (open gap on the right) clockwise to ~320°
    start = math.radians(42)
    end = math.radians(318)
    steps = 64
    for i in range(steps + 1):
        t = start + (end - start) * (i / steps)
        pts.append((cx + r * math.cos(t), cy + r * math.sin(t)))
    return pts


def _draw_editor_mark(img: Image.Image, size: int) -> None:
    draw = ImageDraw.Draw(img)
    _draw_plate(draw, size)

    cx = cy = size / 2
    stroke = max(1.5, size * 0.11)
    r = size * 0.28

    # Soft outer halo ring (subtle, not neon).
    if size >= 32:
        halo = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        hd = ImageDraw.Draw(halo)
        hd.ellipse(
            [cx - r - stroke, cy - r - stroke, cx + r + stroke, cy + r + stroke],
            outline=(*ACCENT_LO[:3], 70),
            width=max(1, int(stroke * 0.55)),
        )
        img.alpha_composite(halo)

    # Main G ring
    ring = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    rd = ImageDraw.Draw(ring)
    bbox = [cx - r, cy - r, cx + r, cy + r]
    rd.arc(bbox, start=42, end=318, fill=ACCENT, width=int(round(stroke)))

    # Inward bar of the G (horizontal from open gap toward center)
    bar_y0 = cy - stroke * 0.45
    bar_y1 = cy + stroke * 0.45
    bar_x0 = cx + r * 0.05
    bar_x1 = cx + r + stroke * 0.15
    rd.rectangle([bar_x0, bar_y0, bar_x1, bar_y1], fill=ACCENT)

    # Small vertical tip at the bar end (reads as G at tiny sizes)
    tip_h = stroke * 1.15
    rd.rectangle(
        [bar_x1 - stroke * 0.85, cy - tip_h * 0.15, bar_x1, cy + tip_h],
        fill=ACCENT,
    )

    # Inner hairline square — editor / viewport cue (skip at 16px)
    if size >= 32:
        inset = size * 0.30
        hair = max(1, size // 64)
        rd.rounded_rectangle(
            [inset, inset, size - inset, size - inset],
            radius=max(1, size * 0.06),
            outline=(*MUTED[:3], 90 if size < 48 else 120),
            width=hair,
        )

    img.alpha_composite(ring)


def _draw_player_mark(img: Image.Image, size: int) -> None:
    draw = ImageDraw.Draw(img)
    _draw_plate(draw, size)

    cx = cy = size / 2
    # Related plate + play triangle — same accent family as editor G.
    stroke = max(1.5, size * 0.09)

    # Soft ring shared with editor language
    if size >= 32:
        r = size * 0.30
        draw.ellipse(
            [cx - r, cy - r, cx + r, cy + r],
            outline=(*ACCENT_LO[:3], 110),
            width=max(1, int(stroke * 0.55)),
        )

    # Play triangle (slightly optical-centered)
    tri_w = size * 0.34
    tri_h = size * 0.40
    ox = size * 0.04  # nudge right for optical center
    pts = [
        (cx - tri_w * 0.35 + ox, cy - tri_h * 0.5),
        (cx - tri_w * 0.35 + ox, cy + tri_h * 0.5),
        (cx + tri_w * 0.55 + ox, cy),
    ]
    draw.polygon(pts, fill=ACCENT)

    # Tiny highlight edge on the leading face (premium, not glow)
    if size >= 48:
        mid = (
            (pts[0][0] + pts[2][0]) * 0.5,
            (pts[0][1] + pts[2][1]) * 0.5,
        )
        draw.line([pts[0], pts[2]], fill=ACCENT_HI, width=max(1, size // 96))
        _ = mid


def _png_bytes(im: Image.Image) -> bytes:
    import io

    buf = io.BytesIO()
    im.save(buf, format="PNG")
    return buf.getvalue()


def _write_ico(path: Path, frames: list[Image.Image]) -> None:
    """Write a multi-size ICO with embedded PNGs (Vista+ / modern Windows)."""
    import struct

    entries: list[tuple[int, int, bytes]] = []
    for im in frames:
        w, h = im.size
        entries.append((w if w < 256 else 0, h if h < 256 else 0, _png_bytes(im)))

    header = struct.pack("<HHH", 0, 1, len(entries))
    offset = 6 + 16 * len(entries)
    directory = bytearray()
    blobs = bytearray()
    for w, h, data in entries:
        directory += struct.pack(
            "<BBBBHHII",
            w,
            h,
            0,  # color count
            0,  # reserved
            1,  # planes
            32,  # bit count
            len(data),
            offset,
        )
        blobs += data
        offset += len(data)

    path.write_bytes(header + directory + blobs)


def _build_ico(variant: str, painter) -> Path:
    frames: list[Image.Image] = []
    for s in SIZES:
        im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
        painter(im, s)
        frames.append(im)

    ico_path = OUT / f"genesis-{variant}.ico"
    _write_ico(ico_path, frames)
    # Also emit a 256 preview PNG for docs / design review.
    frames[-1].save(OUT / f"genesis-{variant}-256.png", format="PNG")
    return ico_path


def _write_svgs() -> None:
    editor_svg = """<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" role="img" aria-label="GENESIS Editor">
  <defs>
    <style>
      .plate { fill: #16181c; }
      .face { fill: #0a0b0e; }
      .accent { fill: none; stroke: #57c7d1; stroke-width: 28; stroke-linecap: round; stroke-linejoin: round; }
      .bar { fill: #57c7d1; }
      .frame { fill: none; stroke: #78808a; stroke-width: 2; stroke-opacity: 0.45; }
    </style>
  </defs>
  <rect class="plate" x="8" y="8" width="240" height="240" rx="56"/>
  <rect class="face" x="22" y="22" width="212" height="212" rx="44"/>
  <path class="accent" d="M178 86 A72 72 0 1 0 178 170"/>
  <rect class="bar" x="138" y="116" width="58" height="24" rx="4"/>
  <rect class="bar" x="172" y="116" width="24" height="48" rx="4"/>
  <rect class="frame" x="76" y="76" width="104" height="104" rx="14"/>
</svg>
"""
    player_svg = """<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" role="img" aria-label="GENESIS Player">
  <defs>
    <style>
      .plate { fill: #16181c; }
      .face { fill: #0a0b0e; }
      .ring { fill: none; stroke: #296670; stroke-width: 10; stroke-opacity: 0.85; }
      .play { fill: #57c7d1; }
    </style>
  </defs>
  <rect class="plate" x="8" y="8" width="240" height="240" rx="56"/>
  <rect class="face" x="22" y="22" width="212" height="212" rx="44"/>
  <circle class="ring" cx="128" cy="128" r="76"/>
  <path class="play" d="M108 78 L108 178 L188 128 Z"/>
</svg>
"""
    (OUT / "genesis-editor.svg").write_text(editor_svg, encoding="utf-8")
    (OUT / "genesis-player.svg").write_text(player_svg, encoding="utf-8")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    _write_svgs()
    e = _build_ico("editor", _draw_editor_mark)
    p = _build_ico("player", _draw_player_mark)
    print(f"wrote {e}")
    print(f"wrote {p}")
    for path in (e, p):
        with path.open("rb") as f:
            data = f.read(6)
        # ICO magic: reserved=0, type=1
        assert data[0:4] == b"\x00\x00\x01\x00", f"bad ICO header: {path}"
        count = int.from_bytes(data[4:6], "little")
        print(f"  {path.name}: {count} image(s)")


if __name__ == "__main__":
    main()
