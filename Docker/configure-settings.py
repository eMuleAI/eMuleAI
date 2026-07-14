#!/usr/bin/env python3
import os
import sys
from collections import OrderedDict
from pathlib import Path

SECTION = "eMule"


def fail(message: str) -> None:
    print(message, file=sys.stderr)
    raise SystemExit(1)


def parse_port(value: str, allow_zero: bool) -> int:
    try:
        port = int(value, 10)
    except ValueError:
        fail(f"Invalid port: {value}")
    minimum = 0 if allow_zero else 1
    if port < minimum or port > 65535:
        fail(f"Port must be between {minimum} and 65535: {value}")
    return port


def update_section(path: Path, forced: OrderedDict[str, str], defaults: OrderedDict[str, str]) -> None:
    raw = path.read_bytes() if path.exists() else b""
    bom = b"\xef\xbb\xbf" if raw.startswith(b"\xef\xbb\xbf") else b""
    if bom:
        raw = raw[len(bom):]

    newline = "\r\n" if b"\r\n" in raw else "\n"
    text = raw.decode("latin-1")
    lines = text.replace("\r\n", "\n").replace("\r", "\n").split("\n")
    if lines and lines[-1] == "":
        lines.pop()

    section_start = None
    section_end = len(lines)
    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            name = stripped[1:-1].strip()
            if section_start is None and name.casefold() == SECTION.casefold():
                section_start = index
            elif section_start is not None:
                section_end = index
                break

    if section_start is None:
        if lines and lines[-1].strip():
            lines.append("")
        section_start = len(lines)
        lines.append(f"[{SECTION}]")
        section_end = len(lines)

    forced_keys = {key.casefold(): key for key in forced}
    default_keys = {key.casefold(): key for key in defaults}
    seen: set[str] = set()
    output = lines[: section_start + 1]

    for line in lines[section_start + 1 : section_end]:
        stripped = line.lstrip()
        if not stripped or stripped.startswith(";") or stripped.startswith("#") or "=" not in stripped:
            output.append(line)
            continue

        raw_key = stripped.split("=", 1)[0].strip()
        folded = raw_key.casefold()
        if folded in forced_keys:
            if folded not in seen:
                key = forced_keys[folded]
                output.append(f"{key}={forced[key]}")
                seen.add(folded)
            continue
        if folded in default_keys:
            output.append(line)
            seen.add(folded)
            continue
        output.append(line)

    for key, value in forced.items():
        if key.casefold() not in seen:
            output.append(f"{key}={value}")
    for key, value in defaults.items():
        if key.casefold() not in seen:
            output.append(f"{key}={value}")

    output.extend(lines[section_end:])
    result = bom + (newline.join(output) + newline).encode("latin-1")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".docker.tmp")
    temporary.write_bytes(result)
    os.replace(temporary, path)


def main() -> None:
    if len(sys.argv) != 4:
        fail("Usage: configure-settings.py <preferences.ini> <tcp-port> <udp-port>")

    path = Path(sys.argv[1])
    tcp_port = parse_port(sys.argv[2], allow_zero=False)
    udp_port = parse_port(sys.argv[3], allow_zero=True)

    forced = OrderedDict(
        [
            ("Port", str(tcp_port)),
            ("UDPPort", str(udp_port)),
            ("RandomizePortsOnStartup", "0"),
            ("IncomingDir", r"Z:\data\Incoming"),
            ("TempDir", r"Z:\data\Temp"),
            ("TempDirs", ""),
            ("MigrationWizardHandled", "1"),
            ("MigrationWizardRunOnNextStart", "0"),
        ]
    )
    defaults = OrderedDict([("Ui.Language", "system")])
    update_section(path, forced, defaults)


if __name__ == "__main__":
    main()
