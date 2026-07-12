import argparse
import re
from pathlib import Path


def to_crlf(content: str) -> str:
    return content.replace("\r\n", "\n").replace("\r", "\n").replace("\n", "\r\n")


def write_ascii(path: Path, content: str) -> None:
    path.write_bytes(to_crlf(content).encode("ascii", errors="replace"))


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate ngtcp2 MSVC compatibility headers.")
    parser.add_argument("-ProjectDir", required=True)
    parser.add_argument("-SourceRoot", default="")
    parser.add_argument("-GeneratedDir", default="")
    args = parser.parse_args()

    project_dir = Path(args.ProjectDir).resolve()
    source_root = Path(args.SourceRoot).resolve() if args.SourceRoot.strip() else project_dir.parent.resolve()
    ngtcp2_dir = source_root / "ngtcp2"
    generated_dir = Path(args.GeneratedDir) if args.GeneratedDir.strip() else source_root / "_Build" / "ngtcp2" / "Generated"
    generated_ngtcp2_dir = generated_dir / "ngtcp2"
    generated_ngtcp2_dir.mkdir(parents=True, exist_ok=True)

    version = (ngtcp2_dir / ".ngtcp2_version.txt").read_text(encoding="ascii").strip()
    match = re.match(r"^([0-9]+)\.([0-9]+)\.([0-9]+)$", version)
    if not match:
        raise RuntimeError("Cannot determine ngtcp2 version")

    version_num = "0x{0:02x}{1:02x}{2:02x}".format(*(int(part) for part in match.groups()))
    version_template = (ngtcp2_dir / "lib" / "includes" / "ngtcp2" / "version.h.in").read_text(encoding="ascii")
    version_header = version_template.replace("@PACKAGE_VERSION@", version).replace("@PACKAGE_VERSION_NUM@", version_num)
    write_ascii(generated_ngtcp2_dir / "version.h", version_header)

    config_content = """/* Generated for the eMuleAI MSVC build. */
#ifndef NGTCP2_CONFIG_H_INCLUDED
#define NGTCP2_CONFIG_H_INCLUDED

#define HAVE_DECL_BE64TOH 0
#define HAVE_DECL_BSWAP_64 0
#define HAVE_DECL_NTOHLL 0
#define HAVE_DECL___BUILTIN_BSWAP64 0
#define HAVE_MEMSET_S 0
#define HAVE_ARPA_INET_H 0
#define HAVE_NETINET_IN_H 0
#define HAVE_SYS_SOCKET_H 0
#define HAVE_UNISTD_H 0
#define HAVE_WS2TCPIP_H 1

#endif
"""
    write_ascii(generated_dir / "config.h", config_content)


if __name__ == "__main__":
    main()
