from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    ROOT / "doomgeneric_vita.c",
    ROOT / "doomgeneric/doomgeneric/d_main.c",
    ROOT / "doomgeneric/doomgeneric/d_iwad.c",
    ROOT / "doomgeneric/doomgeneric/d_mode.c",
    ROOT / "doomgeneric/doomgeneric/m_config.c",
    ROOT / "doomgeneric/doomgeneric/m_menu.c",
]


def strip_comments_and_strings(source: str) -> str:
    pattern = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
    return pattern.sub(" ", source)


def check_braces(path: Path) -> list[str]:
    source = strip_comments_and_strings(path.read_text(encoding="utf-8"))
    balance = 0
    for number, line in enumerate(source.splitlines(), 1):
        for char in line:
            if char == "{":
                balance += 1
            elif char == "}":
                balance -= 1
                if balance < 0:
                    return [f"{path.relative_to(ROOT)}:{number}: unexpected closing brace"]
    if balance:
        return [f"{path.relative_to(ROOT)}: unbalanced braces ({balance})"]
    return []


def check_duplicate_main(path: Path) -> list[str]:
    if path.name != "doomgeneric_vita.c":
        return []
    source = strip_comments_and_strings(path.read_text(encoding="utf-8"))
    mains = re.findall(r"\bint\s+main\s*\(", source)
    if len(mains) != 1:
        return [f"{path.relative_to(ROOT)}: expected one main(), found {len(mains)}"]
    return []


errors = []
for source in SOURCES:
    errors.extend(check_braces(source))
    errors.extend(check_duplicate_main(source))
if errors:
    print("\n".join(errors))
    sys.exit(1)
print(f"C structural checks passed for {len(SOURCES)} source files.")
