"""Render the README screenshots of the native UI screens from the port itself.

The glyphs, colours and coordinates are read back from ``doomgeneric_vita.c``,
so a screenshot cannot drift away from what the console actually draws: change
the screen and this script redraws a new image (or, when the change broke the
layout, refuses to write it).

Run with: ``pip install pillow`` then ``python scripts/prepare_screenshots.py``
"""

from pathlib import Path
import re

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "doomgeneric_vita.c").read_text(encoding="utf-8")
OUTPUT = ROOT / "docs/screenshots"
WIDTH, HEIGHT = 960, 544
FONT_ROWS = 9  # MENU_FONT_ROWS in the port

BG = (22, 26, 42)
PANEL = (32, 38, 60)
BAR = (12, 14, 24)
GOLD = (255, 200, 72)
GOLD_DIM = (170, 132, 48)
TEXT = (226, 228, 240)
DIM = (150, 154, 174)
READY = (104, 224, 128)
WHITE = (255, 255, 255)


def parse_font() -> tuple[str, list[list[int]]]:
    """Read menu_glyphs/menu_font out of the port, the way ui_text walks them."""
    glyphs = re.search(r'static const char menu_glyphs\[\] =\s*"([^"]+)"', SOURCE)
    body = re.search(r"static const unsigned short menu_font\[\]\[5\] = \{(.*?)\n\};", SOURCE, re.S)
    if not glyphs or not body:
        raise SystemExit("cannot find the menu font in doomgeneric_vita.c")
    rows = re.findall(r"\{([^}]*)\}", body.group(1))
    if len(rows) != len(glyphs.group(1)):
        raise SystemExit(f"font has {len(rows)} glyphs for {len(glyphs.group(1))} characters")
    return glyphs.group(1), [[int(value, 16) for value in row.split(",")] for row in rows]


GLYPHS, FONT = parse_font()


def width_of(text: str, scale: int) -> int:
    return (len(text) * 6 - 1) * scale if text else 0


class Canvas:
    """Mirrors ui_rect/ui_text and remembers the box every draw claims."""

    def __init__(self) -> None:
        self.image = Image.new("RGB", (WIDTH, HEIGHT), BG)
        self.pen = ImageDraw.Draw(self.image)
        self.texts: list[tuple[int, int, int, int, str]] = []

    def rect(self, x: int, y: int, width: int, height: int, colour) -> None:
        if x < 0:
            width, x = width + x, 0
        if y < 0:
            height, y = height + y, 0
        width = min(width, WIDTH - x)
        height = min(height, HEIGHT - y)
        self.pen.rectangle([x, y, x + width - 1, y + height - 1], fill=colour)

    def text(self, x: int, y: int, string: str, colour, scale: int = 2) -> None:
        start = x
        for character in string:
            # Like ui_text: anything the font has no glyph for (a space) only
            # advances the cursor.
            index = GLYPHS.find(character)
            if index >= 0:
                for column in range(5):
                    for row in range(FONT_ROWS):
                        if FONT[index][column] & (1 << row):
                            self.pen.rectangle(
                                [x + column * scale, y + row * scale,
                                 x + column * scale + scale - 1, y + row * scale + scale - 1],
                                fill=colour)
            x += 6 * scale
        self.texts.append((start, y, width_of(string, scale), FONT_ROWS * scale, string))

    def text_right(self, right: int, y: int, string: str, colour, scale: int = 2) -> None:
        self.text(right - width_of(string, scale), y, string, colour, scale)


def setting_value(row: int) -> str:
    """The strings options_value() builds for the row's current setting."""
    if row == 0:
        return "60 FPS  SMOOTH (INTERPOLATED)"
    return "OFF  KEEPS THE SCREEN CLEAN"


def options_screen() -> Canvas:
    """Draws the OPTIONS screen with the same calls and coordinates as the port."""
    canvas = Canvas()
    canvas.rect(0, 0, WIDTH, HEIGHT, BG)
    canvas.rect(0, 0, WIDTH, 64, BAR)
    canvas.rect(0, 63, WIDTH, 2, GOLD)
    canvas.text(28, 18, "OPTIONS", GOLD, 3)
    canvas.text_right(WIDTH - 28, 26, "CHEX QUEST COLLECTION", DIM)

    for x, y, label, colour in [
        (48, 96, "IN GAME", GOLD), (48, 122, None, GOLD_DIM),
        (48, 140, "LEFT STICK: MOVE", TEXT), (48, 164, "RIGHT STICK: TURN", TEXT),
        (48, 188, "X: USE", TEXT), (48, 212, "SQUARE OR R: FIRE", TEXT),
        (48, 236, "L: RUN", TEXT), (48, 260, "TRIANGLE: AUTOMAP", TEXT),
        (48, 284, "START: MENU", TEXT), (48, 320, "UP: QUICK SAVE", TEXT),
        (48, 344, "DOWN: QUICK LOAD", TEXT), (48, 368, "LEFT/RIGHT: WEAPONS", TEXT),
        (520, 96, "IN MENUS", GOLD), (520, 122, None, GOLD_DIM),
        (520, 140, "UP/DOWN: MOVE", TEXT), (520, 164, "LEFT/RIGHT: CHANGE", TEXT),
        (520, 188, "X: SELECT", TEXT), (520, 212, "START: CLOSE", TEXT),
        (520, 236, "SAVES AND LOADS", DIM), (520, 260, "WORK IN GAME ONLY", DIM),
        (520, 320, "BACK TO LAUNCHER", GOLD), (520, 344, "HOLD L+R+SELECT", TEXT),
        (520, 368, "FOR ONE SECOND", DIM),
    ]:
        if label is None:
            canvas.rect(x, y, 400, 2, colour)
        else:
            canvas.text(x, y, label, colour)

    canvas.text(48, 386, "PERFORMANCE", GOLD)
    canvas.text_right(WIDTH - 48, 386, "FRAMERATE CHANGES WHEN A GAME STARTS", DIM)

    for index, (label, x, y, width, height) in enumerate([
        ("FRAMERATE", 48, 412, 864, 34), ("FRAME COUNTER", 48, 454, 864, 34),
    ]):
        selected = index == 0
        border = GOLD if selected else GOLD_DIM
        canvas.rect(x, y, width, height, PANEL if selected else BAR)
        canvas.rect(x, y, width, 2, border)
        canvas.rect(x, y + height - 2, width, 2, border)
        canvas.rect(x, y, 2, height, border)
        canvas.rect(x + width - 2, y, 2, height, border)
        canvas.text(x + 20, y + 10, label, WHITE if selected else TEXT)
        canvas.text(x + 250, y + 10, setting_value(index), READY if selected else DIM)
        if selected:
            canvas.text_right(x + width - 20, y + 10, "X: CHANGE", GOLD_DIM)

    canvas.rect(0, 496, WIDTH, HEIGHT - 496, BAR)
    canvas.rect(0, 494, WIDTH, 2, GOLD_DIM)
    canvas.text(28, 514, "UP/DOWN: CHOOSE   X OR LEFT/RIGHT: CHANGE", WHITE)
    canvas.text_right(WIDTH - 28, 514, "START: GO BACK", DIM)
    return canvas


def check_layout(canvas: Canvas) -> None:
    """Refuse to write a screenshot whose texts overlap each other."""
    problems = []
    for i, (x1, y1, w1, h1, first) in enumerate(canvas.texts):
        if x1 < 0 or y1 < 0 or x1 + w1 > WIDTH or y1 + h1 > HEIGHT:
            problems.append(f"{first!r} leaves the screen")
        if y1 < 496 <= y1 + h1:
            problems.append(f"{first!r} crosses the footer bar")
        for x2, y2, w2, h2, second in canvas.texts[i + 1:]:
            if x1 < x2 + w2 and x2 < x1 + w1 and y1 < y2 + h2 and y2 < y1 + h1:
                problems.append(f"{first!r} overlaps {second!r}")
    for index, (label, x, y, width, height) in enumerate([
        ("FRAMERATE", 48, 412, 864, 34), ("FRAME COUNTER", 48, 454, 864, 34),
    ]):
        for x1, y1, w1, h1, text in canvas.texts:
            if y1 >= y and y1 < y + height and y1 + h1 > y + height - 2:
                problems.append(f"{text!r} touches the border of the {label} box")
    if problems:
        raise SystemExit("layout problems:\n  " + "\n  ".join(problems))


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    canvas = options_screen()
    check_layout(canvas)
    destination = OUTPUT / "options.png"
    canvas.image.save(destination, format="PNG", optimize=True)
    print(f"wrote {destination.relative_to(ROOT)}: {len(canvas.texts)} texts, no overlaps")


if __name__ == "__main__":
    main()
