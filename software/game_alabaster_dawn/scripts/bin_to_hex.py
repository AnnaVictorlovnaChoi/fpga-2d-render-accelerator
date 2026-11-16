"""Stream raw BIN to byte-per-line hex (not Intel HEX); no padding/header."""
import argparse
from pathlib import Path


def convert(source: Path, target: Path, limit: int = 2 * 1024 * 1024) -> int:
    size = source.stat().st_size
    if not 0 < size <= limit <= 32 * 1024 * 1024:
        raise ValueError(f"BIN size {size} is outside 1..{limit} bytes")
    lut = tuple(f"{i:02X}\n".encode("ascii") for i in range(256))
    temp = target.with_suffix(target.suffix + ".tmp")
    try:
        with source.open("rb") as inp, temp.open("wb") as out:
            while chunk := inp.read(65536):
                out.write(b"".join(lut[b] for b in chunk))
        temp.replace(target)
    finally:
        temp.unlink(missing_ok=True)
    return size


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("target", type=Path)
    parser.add_argument('--limit', type=int, default=2*1024*1024, help='Explicit full flash limit, at most 33554432')
    args = parser.parse_args()
    print(f"HEX: {convert(args.source, args.target, args.limit)} byte lines")
