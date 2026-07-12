import argparse
import os
import platform
import re
import shutil
import subprocess
from pathlib import Path


def to_crlf(content: str) -> str:
    return content.replace("\r\n", "\n").replace("\r", "\n").replace("\n", "\r\n")


def write_ascii(path: Path, content: str) -> None:
    path.write_bytes(to_crlf(content).encode("ascii", errors="replace"))


def get_host_vcvars_arch() -> str:
    arch = platform.machine() or os.environ.get("PROCESSOR_ARCHITEW6432", "") or os.environ.get("PROCESSOR_ARCHITECTURE", "")
    arch = arch.lower()
    if "arm64" in arch or "aarch64" in arch:
        return "arm64"
    if "64" in arch or arch == "amd64":
        return "x64"
    return "x86"


def find_vcvarsall() -> Path | None:
    candidates = []
    vs_install_dir = os.environ.get("VSINSTALLDIR", "").strip()
    if vs_install_dir:
        candidates.append(Path(vs_install_dir) / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat")

    vc_tools_dir = os.environ.get("VCToolsInstallDir", "").strip()
    if vc_tools_dir:
        for parent in Path(vc_tools_dir).resolve().parents:
            if parent.name.lower() == "vc":
                candidates.append(parent / "Auxiliary" / "Build" / "vcvarsall.bat")
                break

    cl_path = shutil.which("cl.exe")
    if cl_path:
        for parent in Path(cl_path).resolve().parents:
            if parent.name.lower() == "vc":
                candidates.append(parent / "Auxiliary" / "Build" / "vcvarsall.bat")
                break

    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def run_host_cl(arguments: list[str]) -> None:
    vcvarsall = find_vcvarsall()
    if vcvarsall is None:
        subprocess.run(arguments, check=True)
        return

    command = f'cmd.exe /d /c call "{vcvarsall}" {get_host_vcvars_arch()} >nul && {subprocess.list2cmdline(arguments)}'
    subprocess.run(command, check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate Nettle MSVC compatibility headers.")
    parser.add_argument("-ProjectDir", required=True)
    parser.add_argument("-SourceRoot", default="")
    parser.add_argument("-GeneratedDir", default="")
    parser.add_argument("-ToolDir", default="")
    args = parser.parse_args()

    project_dir = Path(args.ProjectDir).resolve()
    source_root = Path(args.SourceRoot).resolve() if args.SourceRoot.strip() else project_dir.parent.resolve()
    nettle_dir = source_root / "nettle"
    generated_dir = Path(args.GeneratedDir) if args.GeneratedDir.strip() else source_root / "_Build" / "nettle" / "Generated"
    tool_dir = Path(args.ToolDir) if args.ToolDir.strip() else source_root / "_Build" / "nettle" / "Tools"
    generated_nettle_dir = generated_dir / "nettle"
    generated_nettle_dir.mkdir(parents=True, exist_ok=True)
    tool_dir.mkdir(parents=True, exist_ok=True)

    configure = (nettle_dir / "configure.ac").read_text(encoding="utf-8")
    match = re.search(r"AC_INIT\(\[nettle\], \[([0-9]+)\.([0-9]+)", configure)
    if not match:
        raise RuntimeError("Cannot determine Nettle version from configure.ac")
    major, minor = (int(value) for value in match.groups())

    version_content = f"""/* Generated for the eMuleAI MSVC build. */
#ifndef NETTLE_VERSION_H_INCLUDED
#define NETTLE_VERSION_H_INCLUDED

#ifdef __cplusplus
extern "C" {{
#endif

#define NETTLE_VERSION_MAJOR {major}
#define NETTLE_VERSION_MINOR {minor}
#define NETTLE_USE_MINI_GMP 1
#define GMP_NUMB_BITS 32

int nettle_version_major(void);
int nettle_version_minor(void);

#ifdef __cplusplus
}}
#endif

#endif
"""
    write_ascii(generated_dir / "version.h", version_content)
    write_ascii(generated_nettle_dir / "version.h", version_content)

    config_content = """/* Generated for the eMuleAI MSVC build. */
#ifndef NETTLE_CONFIG_H_INCLUDED
#define NETTLE_CONFIG_H_INCLUDED

#define HAVE_INTTYPES_H 1
#define HAVE_MALLOC_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define STDC_HEADERS 1
#define WITH_HOGWEED 1

#define SIZEOF_LONG 4
#if defined(_WIN64)
#define SIZEOF_SIZE_T 8
#else
#define SIZEOF_SIZE_T 4
#endif

#define HAVE_NATIVE_64_BIT 0
#define NORETURN __declspec(noreturn)
#define PRINTF_STYLE(f, a)
#define UNUSED

#endif
"""
    write_ascii(generated_dir / "config.h", config_content)

    ecc_exe = tool_dir / "eccdata.exe"
    ecc_obj = tool_dir / "eccdata.obj"
    run_host_cl([
        "cl.exe",
        "/nologo",
        "/TC",
        f"/I{nettle_dir}",
        f"/Fo{ecc_obj}",
        f"/Fe{ecc_exe}",
        str(nettle_dir / "eccdata.c"),
    ])

    curves = [
        ("secp192r1", "8", "6", "ecc-secp192r1.h"),
        ("secp224r1", "16", "7", "ecc-secp224r1.h"),
        ("secp256r1", "11", "6", "ecc-secp256r1.h"),
        ("secp384r1", "32", "6", "ecc-secp384r1.h"),
        ("secp521r1", "44", "6", "ecc-secp521r1.h"),
        ("curve25519", "11", "6", "ecc-curve25519.h"),
        ("curve448", "38", "6", "ecc-curve448.h"),
        ("gost_gc256b", "11", "6", "ecc-gost-gc256b.h"),
        ("gost_gc512a", "43", "6", "ecc-gost-gc512a.h"),
    ]
    for curve_name, bits, limbs, filename in curves:
        result = subprocess.run([str(ecc_exe), curve_name, bits, limbs, "32"], check=True, capture_output=True, text=True)
        output = "\r\n".join(result.stdout.splitlines()) + "\r\n"
        (generated_nettle_dir / filename).write_bytes(output.encode("ascii", errors="replace"))


if __name__ == "__main__":
    main()
