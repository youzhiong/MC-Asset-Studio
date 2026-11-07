"""Regenerate the Qt icon resources from the rendered SVG master.

`app.svg` is the authoritative artwork. Render it with Qt's QSvgRenderer (or
any high-quality SVG renderer) to `app-1024.png` at 1024x1024, then run:

    python resources/make_icon.py

This writes `app.png` (256 px) and a multi-size `app.ico` for the Win32
executable and QIcon. The old pixel-art placeholder generator has been removed.
"""

from io import BytesIO
from pathlib import Path
import struct

from PIL import Image

SIZES = [16, 24, 32, 48, 64, 128, 256]
ROOT = Path(__file__).resolve().parent
ICONS = ROOT / "icons"
MASTER = ICONS / "app-1024.png"

if not MASTER.exists():
    raise SystemExit(
        f"Missing {MASTER}. Render resources/icons/app.svg to 1024x1024 first."
    )

master = Image.open(MASTER).convert("RGBA")
master.resize((256, 256), Image.Resampling.LANCZOS).save(ICONS / "app.png")

payloads: list[tuple[int, bytes]] = []
for size in SIZES:
    frame = master.resize((size, size), Image.Resampling.LANCZOS)
    buffer = BytesIO()
    frame.save(buffer, format="PNG")
    payloads.append((size, buffer.getvalue()))

header = struct.pack("<HHH", 0, 1, len(payloads))
offset = 6 + 16 * len(payloads)
entries = bytearray()
data = bytearray()
for size, png in payloads:
    width = 0 if size == 256 else size
    height = 0 if size == 256 else size
    entries += struct.pack("<BBBBHHII", width, height, 0, 0, 1, 32, len(png), offset)
    data += png
    offset += len(png)
(ICONS / "app.ico").write_bytes(header + entries + data)

print(ICONS / "app.png")
print(ICONS / "app.ico")
