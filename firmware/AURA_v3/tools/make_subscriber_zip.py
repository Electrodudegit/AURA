#!/usr/bin/env python3
"""Build releases/AURA_v3_subscriber.zip with the sketch folder named AURA_v3."""
import os
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "releases" / "AURA_v3_subscriber.zip"
SKIP_DIRS = {".git", "uploads", "releases", "__pycache__", "captures", ".vscode"}
SKIP_SUFFIX = {".bin", ".elf", ".o", ".pyc", ".ppm"}


def skip(path: Path) -> bool:
    rel = path.relative_to(ROOT)
    if any(part in SKIP_DIRS for part in rel.parts):
        return True
    if path.name in {"aura_host"}:
        return True
    if path.suffix.lower() in SKIP_SUFFIX:
        return True
    return False


def main() -> None:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    if OUT.exists():
        OUT.unlink()
    count = 0
    with zipfile.ZipFile(OUT, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for dirpath, dirnames, filenames in os.walk(ROOT):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS and not d.startswith(".git")]
            for name in filenames:
                src = Path(dirpath) / name
                if skip(src):
                    continue
                arc = Path("AURA_v3") / src.relative_to(ROOT)
                zf.write(src, arc.as_posix())
                count += 1
    print(f"wrote {OUT} ({count} files, {OUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
