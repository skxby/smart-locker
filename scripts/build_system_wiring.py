"""Current overview builder; full atlas generation needs Pillow."""
from build_wiring_guide import build_overview, main


def build() -> str:
    sheet = build_overview()
    return '\n'.join(sheet.svg) + '</svg>'


if __name__ == "__main__":
    main()
