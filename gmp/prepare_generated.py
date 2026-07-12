import argparse
from pathlib import Path


def to_crlf(content: str) -> str:
    return content.replace("\r\n", "\n").replace("\r", "\n").replace("\n", "\r\n")


def write_ascii(path: Path, content: str) -> None:
    path.write_bytes(to_crlf(content).encode("ascii", errors="replace"))


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate GMP MSVC compatibility headers.")
    parser.add_argument("-ProjectDir", required=True)
    parser.add_argument("-SourceRoot", default="")
    parser.add_argument("-GeneratedDir", default="")
    args = parser.parse_args()

    project_dir = Path(args.ProjectDir).resolve()
    source_root = Path(args.SourceRoot).resolve() if args.SourceRoot.strip() else project_dir.parent.resolve()
    generated_dir = Path(args.GeneratedDir) if args.GeneratedDir.strip() else source_root / "_Build" / "gmp" / "Generated"
    generated_dir.mkdir(parents=True, exist_ok=True)

    content = """#ifndef GMP_H
#define GMP_H

#include <mini-gmp.h>
#include <mini-mpq.h>

#endif
"""
    write_ascii(generated_dir / "gmp.h", content)


if __name__ == "__main__":
    main()
