"""Bake the LiveArea artwork (bubble icon, background, startup) from the sources.

Run with: ``pip install pillow`` then ``python scripts/prepare_artwork.py``
"""

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(__file__).resolve().parents[2] / "artwork da usare"
OUT = ROOT / "sce_sys"
LIVEAREA = OUT / "livearea" / "contents"
OUT.mkdir(parents=True, exist_ok=True)
LIVEAREA.mkdir(parents=True, exist_ok=True)

# The LiveArea background and the startup image are a wide scene drawn in a
# frame that is not as wide, so the leftover bands are filled with this.
BAND = (16, 18, 28)


def artwork_background(image: Image.Image) -> tuple[int, int, int]:
    """The average of the artwork's four corners, i.e. its own background.

    The bubble icon is a square and `icon0.jfif` is a wide banner with the logo
    across it: the logo cannot be cropped to a square without cutting the words
    in half, so the bands have to be painted. Painting them with the artwork's
    own colour - a fixed dark one left two black stripes across the icon and
    beyond its edges, which is what the home screen showed - lets the logo sit
    on its own background, and the icon reaches every edge with no band.
    """
    pixels = image.convert("RGB").load()
    corners = ((0, 0), (image.width - 1, 0), (0, image.height - 1),
               (image.width - 1, image.height - 1))
    return tuple(sum(pixels[x, y][channel] for x, y in corners) // 4
                 for channel in range(3))


def vita_png(image: Image.Image, destination: Path, size: tuple[int, int],
             background: tuple[int, int, int] = BAND) -> None:
    image = image.convert("RGB")
    image.thumbnail(size, Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", size, background)
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
vita_png(icon, OUT / "icon0.png", (128, 128), artwork_background(icon))
vita_png(background, LIVEAREA / "bg.png", (840, 500))
vita_png(background, LIVEAREA / "startup.png", (280, 158))
