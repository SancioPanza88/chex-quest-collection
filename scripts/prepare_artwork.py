from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(__file__).resolve().parents[2] / "artwork da usare"
OUT = ROOT / "sce_sys"
LIVEAREA = OUT / "livearea" / "contents"
OUT.mkdir(parents=True, exist_ok=True)
LIVEAREA.mkdir(parents=True, exist_ok=True)


def vita_png(image: Image.Image, destination: Path, size: tuple[int, int]) -> None:
    image = image.convert("RGB")
    image.thumbnail(size, Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", size, (16, 18, 28))
    canvas.paste(image, ((size[0] - image.width) // 2, (size[1] - image.height) // 2))
    # Quantize to RGB332 before saving: Vita LiveArea assets need indexed 8-bit PNG.
    palette = []
    for index in range(256):
        red = ((index >> 5) & 7) * 255 // 7
        green = ((index >> 2) & 7) * 255 // 7
        blue = (index & 3) * 255 // 3
        palette.extend((red, green, blue))
    palette_image = Image.new("P", (1, 1))
    palette_image.putpalette(palette)
    indexed = canvas.quantize(palette=palette_image, dither=Image.Dither.NONE)
    indexed.putpalette(palette)
    indexed.save(destination, format="PNG", optimize=False)


icon = Image.open(SOURCE / "icon0.jfif")
background = Image.open(SOURCE / "background e altro.jfif")
vita_png(icon, OUT / "icon0.png", (128, 128))
vita_png(background, LIVEAREA / "bg.png", (840, 500))
vita_png(background, LIVEAREA / "startup.png", (280, 158))
