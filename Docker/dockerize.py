#!/usr/bin/env python3
"""Build and optionally publish the eMule AI Docker image."""

from __future__ import annotations

import base64
import ctypes
import datetime as dt
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import traceback
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any, Iterable


DOCKERIZE_REVISION = "2026-07-15.6"
IMAGE_NAME = "emuleai/emuleai"
KEEP_STABLE_RELEASES = 3
KEEP_PRERELEASE_RELEASES = 2
DEFAULT_EXE_RELATIVE = Path("..") / "_Build" / "eMuleAI" / "Release" / "x64" / "eMuleAI.exe"
STABLE_VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")
PRERELEASE_VERSION_RE = re.compile(r"^\d+\.\d+\.\d+-[0-9A-Za-z][0-9A-Za-z.-]*$")
DOCKER_HUB_SERVERS = (
    "https://index.docker.io/v1/",
    "https://index.docker.io/v1",
    "registry-1.docker.io",
    "docker.io",
)


class DockerizeError(RuntimeError):
    """Expected user-facing failure."""


class RetentionError(RuntimeError):
    """Non-fatal Docker Hub retention failure."""


def pause_on_error() -> None:
    if not sys.stdin.isatty():
        return
    try:
        input("\nPress Enter to close this window.")
    except (EOFError, KeyboardInterrupt):
        pass


def quote_command(command: Iterable[str]) -> str:
    values = [str(value) for value in command]
    if os.name == "nt":
        return subprocess.list2cmdline(values)
    return " ".join(subprocess.list2cmdline([value]) for value in values)


def run_command(
    command: list[str],
    *,
    capture_output: bool = False,
    input_text: str | None = None,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    try:
        result = subprocess.run(
            command,
            input=input_text,
            text=True,
            capture_output=capture_output,
            check=False,
        )
    except OSError as exc:
        raise DockerizeError(f"Could not start command: {quote_command(command)}\n{exc}") from exc

    if check and result.returncode != 0:
        detail = ""
        if capture_output:
            detail = (result.stderr or result.stdout or "").strip()
        message = f"Command failed with exit code {result.returncode}:\n{quote_command(command)}"
        if detail:
            message += f"\n{detail}"
        raise DockerizeError(message)
    return result


def find_executable(name: str) -> str:
    executable = shutil.which(name)
    if executable is None:
        raise DockerizeError(f"Required executable was not found in PATH: {name}")
    return executable


def read_windows_product_version(executable_path: Path) -> str:
    if os.name != "nt":
        raise DockerizeError("dockerize.py must be run on Windows to read eMuleAI.exe ProductVersion.")

    from ctypes import wintypes

    version = ctypes.WinDLL("version", use_last_error=True)
    version.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    version.GetFileVersionInfoSizeW.restype = wintypes.DWORD
    version.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID]
    version.GetFileVersionInfoW.restype = wintypes.BOOL
    version.VerQueryValueW.argtypes = [wintypes.LPCVOID, wintypes.LPCWSTR, ctypes.POINTER(wintypes.LPVOID), ctypes.POINTER(wintypes.UINT)]
    version.VerQueryValueW.restype = wintypes.BOOL

    handle = wintypes.DWORD(0)
    size = version.GetFileVersionInfoSizeW(str(executable_path), ctypes.byref(handle))
    if size == 0:
        error = ctypes.get_last_error()
        raise DockerizeError(f"ProductVersion could not be read from:\n       {executable_path}\nWindows error: {error}")

    buffer = ctypes.create_string_buffer(size)
    if not version.GetFileVersionInfoW(str(executable_path), 0, size, buffer):
        error = ctypes.get_last_error()
        raise DockerizeError(f"ProductVersion could not be read from:\n       {executable_path}\nWindows error: {error}")

    translations: list[tuple[int, int]] = []
    translation_ptr = wintypes.LPVOID()
    translation_size = wintypes.UINT(0)
    if version.VerQueryValueW(buffer, r"\VarFileInfo\Translation", ctypes.byref(translation_ptr), ctypes.byref(translation_size)):
        word_count = translation_size.value // ctypes.sizeof(wintypes.WORD)
        words = ctypes.cast(translation_ptr, ctypes.POINTER(wintypes.WORD))
        for index in range(0, word_count - 1, 2):
            translations.append((int(words[index]), int(words[index + 1])))

    for fallback in ((0x0409, 0x04B0), (0x0409, 0x04E4)):
        if fallback not in translations:
            translations.append(fallback)

    for language, codepage in translations:
        query = rf"\StringFileInfo\{language:04x}{codepage:04x}\ProductVersion"
        value_ptr = wintypes.LPVOID()
        value_size = wintypes.UINT(0)
        if not version.VerQueryValueW(buffer, query, ctypes.byref(value_ptr), ctypes.byref(value_size)):
            continue
        value = ctypes.wstring_at(value_ptr, value_size.value).rstrip("\x00").strip()
        if value:
            return value

    raise DockerizeError(f"ProductVersion was not found in:\n       {executable_path}")


def validate_version(raw_version: str) -> tuple[str, bool]:
    version = raw_version.strip()
    if STABLE_VERSION_RE.fullmatch(version):
        return version, True
    if PRERELEASE_VERSION_RE.fullmatch(version):
        return version, False
    raise DockerizeError(
        "The executable ProductVersion has an unsupported eMule AI release format:\n"
        f"       {raw_version}\n"
        "       Expected format: 1.5.1 or 1.6.0-beta1\n"
        "       Update EMULEAI_VERSION in srchybrid\\Opcodes.h and rebuild the executable."
    )


def trim_release_readme(readme_path: Path) -> None:
    try:
        raw = readme_path.read_bytes()
        text = raw.decode("utf-8-sig")
    except (OSError, UnicodeError) as exc:
        raise DockerizeError(f"README.md could not be read as UTF-8:\n       {readme_path}\n{exc}") from exc

    lines = text.splitlines()
    if len(lines) <= 4:
        raise DockerizeError("README.md must contain more than four lines before release trimming.")

    had_final_newline = text.endswith(("\n", "\r"))
    output = "\r\n".join(lines[4:])
    if had_final_newline:
        output += "\r\n"

    try:
        readme_path.write_bytes(output.encode("utf-8"))
    except OSError as exc:
        raise DockerizeError(f"The release README.md could not be updated:\n       {readme_path}\n{exc}") from exc


def update_release_template_documents(repository_dir: Path, template_dir: Path) -> None:
    source_readme = repository_dir / "README.md"
    source_release_notes = repository_dir / "Release_Notes.txt"
    target_readme = template_dir / "README.md"
    target_release_notes = template_dir / "Release_Notes.txt"

    for source in (source_readme, source_release_notes):
        if not source.is_file():
            raise DockerizeError(f"Required release document was not found:\n       {source}")

    print("\nUpdating release template documents...")
    try:
        shutil.copy2(source_readme, target_readme)
        trim_release_readme(target_readme)
        shutil.copy2(source_release_notes, target_release_notes)
    except OSError as exc:
        raise DockerizeError(f"Release template documents could not be updated:\n{exc}") from exc


def prepare_build_context(
    script_dir: Path,
    template_dir: Path,
    executable_path: Path,
    build_context: Path,
) -> None:
    app_dir = build_context / "app"
    app_dir.mkdir(parents=True, exist_ok=False)

    required_docker_files = (
        "Dockerfile",
        "entrypoint.sh",
        "configure-settings.py",
        ".dockerignore",
    )
    for filename in required_docker_files:
        source = script_dir / filename
        if not source.is_file():
            raise DockerizeError(f"Required Docker file was not found:\n       {source}")
        shutil.copy2(source, build_context / filename)

    shutil.copytree(template_dir, app_dir, dirs_exist_ok=True, copy_function=shutil.copy2)
    shutil.copy2(executable_path, app_dir / "eMuleAI.exe")


def parse_mode(value: str) -> bool:
    normalized = value.strip().lower()
    if normalized in {"y", "yes", "publish"}:
        return True
    if normalized in {"", "n", "no", "local"}:
        return False
    raise DockerizeError("The second parameter must be publish or local.")


def get_user_inputs(script_dir: Path, arguments: list[str]) -> tuple[Path, bool, str, bool]:
    if any(argument in {"-h", "--help", "/?"} for argument in arguments):
        print(
            "Usage: python dockerize.py [path-to-eMuleAI.exe] [publish|local]\n\n"
            "With no parameters, the script asks for the executable path and publication mode."
        )
        raise SystemExit(0)
    if len(arguments) > 2:
        raise DockerizeError("Too many parameters. Use --help for usage information.")

    default_executable = (script_dir / DEFAULT_EXE_RELATIVE).resolve()
    executable_input = arguments[0].strip().strip('"') if arguments else ""
    if not executable_input:
        print(f"\nDefault: {DEFAULT_EXE_RELATIVE}")
        executable_input = input("Enter the path to the x64 eMuleAI.exe or press Enter for the default: ").strip().strip('"')
    executable_path = Path(executable_input).expanduser().resolve() if executable_input else default_executable

    if not executable_path.is_file():
        raise DockerizeError(f"eMuleAI executable was not found:\n       {executable_path}")
    if executable_path.name.lower() != "emuleai.exe":
        raise DockerizeError("The executable path must point to the x64 eMuleAI.exe file.")

    raw_version = read_windows_product_version(executable_path)
    version, stable = validate_version(raw_version)
    print(f"\nDetected eMule AI version: {version}")

    mode_input = arguments[1] if len(arguments) == 2 else input(f"Publish eMule AI {version} to Docker Hub? [y/N]: ")
    publish = parse_mode(mode_input)
    return executable_path, publish, version, stable


def verify_docker_environment(docker: str) -> None:
    run_command([docker, "info"], capture_output=True)
    docker_os = run_command([docker, "info", "--format", "{{.OSType}}"], capture_output=True).stdout.strip()
    if docker_os.lower() != "linux":
        raise DockerizeError("Docker Desktop must use Linux containers.")
    run_command([docker, "buildx", "version"], capture_output=True)


def get_docker_config_path() -> Path:
    configured = os.environ.get("DOCKER_CONFIG")
    if configured:
        return Path(configured).expanduser() / "config.json"
    return Path.home() / ".docker" / "config.json"


def load_docker_config() -> dict[str, Any]:
    config_path = get_docker_config_path()
    if not config_path.is_file():
        raise RetentionError("Docker login credentials were not found. Run docker login first.")
    try:
        with config_path.open("r", encoding="utf-8") as stream:
            data = json.load(stream)
    except (OSError, json.JSONDecodeError) as exc:
        raise RetentionError(f"Docker configuration could not be read: {config_path}\n{exc}") from exc
    if not isinstance(data, dict):
        raise RetentionError(f"Docker configuration is invalid: {config_path}")
    return data


def resolve_credential_helper(helper_name: str, docker_executable: str) -> str:
    command_name = f"docker-credential-{helper_name}"
    helper = shutil.which(command_name)
    if helper:
        return helper

    docker_dir = Path(docker_executable).resolve().parent
    candidates = (docker_dir / f"{command_name}.exe", docker_dir / command_name)
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    raise RetentionError(f"Docker credential helper was not found: {command_name}")


def credential_from_helper(helper_path: str, server: str) -> tuple[str, str] | None:
    try:
        result = run_command(
            [helper_path, "get"],
            capture_output=True,
            input_text=server + "\n",
            check=False,
        )
    except DockerizeError as exc:
        raise RetentionError(str(exc)) from exc
    if result.returncode != 0 or not result.stdout.strip():
        return None
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError:
        return None
    username = str(payload.get("Username", ""))
    secret = str(payload.get("Secret", ""))
    return (username, secret) if secret else None


def get_docker_hub_credential(docker_executable: str) -> tuple[str, str]:
    config = load_docker_config()
    credential_helpers = config.get("credHelpers")
    helper_name = ""
    if isinstance(credential_helpers, dict):
        for server in DOCKER_HUB_SERVERS:
            value = credential_helpers.get(server)
            if isinstance(value, str) and value:
                helper_name = value
                break
    if not helper_name:
        value = config.get("credsStore")
        if isinstance(value, str):
            helper_name = value

    if helper_name:
        helper_path = resolve_credential_helper(helper_name, docker_executable)
        for server in DOCKER_HUB_SERVERS:
            credential = credential_from_helper(helper_path, server)
            if credential:
                return credential

    auths = config.get("auths")
    if isinstance(auths, dict):
        for server in DOCKER_HUB_SERVERS:
            record = auths.get(server)
            if not isinstance(record, dict):
                continue
            encoded = record.get("auth")
            if not isinstance(encoded, str) or not encoded:
                continue
            try:
                decoded = base64.b64decode(encoded).decode("utf-8")
                username, secret = decoded.split(":", 1)
            except (ValueError, UnicodeError):
                continue
            if secret:
                return username, secret

    raise RetentionError("Docker Hub credentials could not be read. Run docker login and try again.")


def http_json(
    method: str,
    url: str,
    *,
    headers: dict[str, str] | None = None,
    body: dict[str, Any] | None = None,
    expected_statuses: tuple[int, ...] = (200,),
) -> tuple[int, Any]:
    request_headers = {"Accept": "application/json", "User-Agent": "eMuleAI-dockerize.py"}
    if headers:
        request_headers.update(headers)
    data = None
    if body is not None:
        data = json.dumps(body, separators=(",", ":")).encode("utf-8")
        request_headers["Content-Type"] = "application/json"

    request = urllib.request.Request(url, data=data, headers=request_headers, method=method)
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            status = response.status
            raw = response.read()
    except urllib.error.HTTPError as exc:
        raw = exc.read()
        detail = raw.decode("utf-8", errors="replace").strip()
        raise RetentionError(f"Docker Hub API returned HTTP {exc.code} for {method} {url}\n{detail}") from exc
    except urllib.error.URLError as exc:
        raise RetentionError(f"Docker Hub API request failed for {method} {url}\n{exc.reason}") from exc

    if status not in expected_statuses:
        raise RetentionError(f"Docker Hub API returned unexpected HTTP {status} for {method} {url}")
    if not raw:
        return status, None
    try:
        return status, json.loads(raw.decode("utf-8"))
    except json.JSONDecodeError as exc:
        raise RetentionError(f"Docker Hub API returned invalid JSON for {method} {url}") from exc


def get_docker_hub_access_token(namespace: str, username: str, secret: str) -> str:
    identifier = namespace if username == "<token>" else username
    _, payload = http_json(
        "POST",
        "https://hub.docker.com/v2/auth/token",
        body={"identifier": identifier, "secret": secret},
    )
    if not isinstance(payload, dict):
        raise RetentionError("Docker Hub did not return an authentication response.")
    token = payload.get("access_token") or payload.get("token")
    if not isinstance(token, str) or not token:
        raise RetentionError("Docker Hub did not return an access token.")
    return token


def get_all_docker_hub_tags(namespace: str, repository: str, token: str) -> list[dict[str, Any]]:
    encoded_namespace = urllib.parse.quote(namespace, safe="")
    encoded_repository = urllib.parse.quote(repository, safe="")
    url = f"https://hub.docker.com/v2/namespaces/{encoded_namespace}/repositories/{encoded_repository}/tags?page_size=100"
    headers = {"Authorization": f"Bearer {token}"}
    tags: list[dict[str, Any]] = []

    while url:
        _, payload = http_json("GET", url, headers=headers)
        if not isinstance(payload, dict):
            raise RetentionError("Docker Hub returned an invalid tag list.")
        results = payload.get("results")
        if not isinstance(results, list):
            raise RetentionError("Docker Hub tag list does not contain results.")
        tags.extend(item for item in results if isinstance(item, dict))
        next_url = payload.get("next")
        url = next_url if isinstance(next_url, str) else ""
    return tags


def parse_updated_time(tag: dict[str, Any]) -> dt.datetime:
    value = tag.get("last_updated")
    if not isinstance(value, str):
        return dt.datetime.min.replace(tzinfo=dt.timezone.utc)
    try:
        return dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return dt.datetime.min.replace(tzinfo=dt.timezone.utc)


def remove_docker_hub_tag(namespace: str, repository: str, tag: str, token: str) -> None:
    encoded_namespace = urllib.parse.quote(namespace, safe="")
    encoded_repository = urllib.parse.quote(repository, safe="")
    encoded_tag = urllib.parse.quote(tag, safe="")
    headers = {"Authorization": f"Bearer {token}"}
    urls = (
        f"https://hub.docker.com/v2/namespaces/{encoded_namespace}/repositories/{encoded_repository}/tags/{encoded_tag}",
        f"https://hub.docker.com/v2/repositories/{encoded_namespace}/{encoded_repository}/tags/{encoded_tag}/",
    )

    errors: list[str] = []
    for url in urls:
        try:
            http_json("DELETE", url, headers=headers, expected_statuses=(202, 204))
            return
        except RetentionError as exc:
            errors.append(str(exc))
    raise RetentionError("\n".join(errors))


def apply_retention_policy(
    docker_executable: str,
    image_name: str,
    current_version: str,
    keep_stable: int,
    keep_prerelease: int,
) -> None:
    if "/" not in image_name:
        raise RetentionError("Image must use namespace/repository format.")
    namespace, repository = image_name.split("/", 1)

    username, secret = get_docker_hub_credential(docker_executable)
    token = get_docker_hub_access_token(namespace, username, secret)
    tags = get_all_docker_hub_tags(namespace, repository, token)

    stable = sorted(
        (tag for tag in tags if isinstance(tag.get("name"), str) and STABLE_VERSION_RE.fullmatch(str(tag["name"]))),
        key=parse_updated_time,
        reverse=True,
    )
    prerelease = sorted(
        (tag for tag in tags if isinstance(tag.get("name"), str) and PRERELEASE_VERSION_RE.fullmatch(str(tag["name"]))),
        key=parse_updated_time,
        reverse=True,
    )

    keep = {current_version.lower()}
    keep.update(str(tag["name"]).lower() for tag in stable[:keep_stable])
    keep.update(str(tag["name"]).lower() for tag in prerelease[:keep_prerelease])

    delete_tags = [tag for tag in stable + prerelease if str(tag["name"]).lower() not in keep]
    if not delete_tags:
        print("No old full-version tags need to be deleted.")
        return

    failures: list[str] = []
    for tag_info in delete_tags:
        tag = str(tag_info["name"])
        try:
            remove_docker_hub_tag(namespace, repository, tag, token)
            print(f"Deleted old Docker Hub tag: {tag}")
        except RetentionError as exc:
            failures.append(f"{tag}: {exc}")

    if failures:
        raise RetentionError("One or more old Docker Hub tags could not be removed:\n" + "\n".join(failures))


def build_tags(version: str, stable: bool, publish: bool) -> list[str]:
    tags = [f"{IMAGE_NAME}:{version}"]
    if publish and stable:
        major, minor, _patch = version.split(".", 2)
        tags.extend((f"{IMAGE_NAME}:{major}.{minor}", f"{IMAGE_NAME}:latest"))
    elif not publish:
        tags.append(f"{IMAGE_NAME}:local")
    return tags


def run_build(
    docker: str,
    build_context: Path,
    version: str,
    tags: list[str],
    publish: bool,
) -> None:
    command = [
        docker,
        "buildx",
        "build",
        "--platform",
        "linux/amd64",
        "--pull",
        "--build-arg",
        f"EMULEAI_VERSION={version}",
    ]
    for tag in tags:
        command.extend(("--tag", tag))
    command.append("--push" if publish else "--load")
    command.append(str(build_context))
    run_command(command)


def main(arguments: list[str]) -> int:
    print(f"Dockerize script revision: {DOCKERIZE_REVISION}")

    script_dir = Path(__file__).resolve().parent
    repository_dir = script_dir.parent
    template_dir = repository_dir.parent / "eMuleAI_Releases" / "_Template"

    executable_path, publish, version, stable = get_user_inputs(script_dir, arguments)

    docker = find_executable("docker")
    verify_docker_environment(docker)

    if not template_dir.is_dir():
        raise DockerizeError(f"Release template directory was not found:\n       {template_dir}")

    update_release_template_documents(repository_dir, template_dir)
    tags = build_tags(version, stable, publish)

    print(f"\nDocker image:    {IMAGE_NAME}:{version}")
    print(f"Source template: {template_dir}")
    print(f"Executable:      {executable_path}")
    print(f"Tags:            {', '.join(tags)}")
    print(f"Mode:            {'Publish to Docker Hub' if publish else 'Local test build'}\n")

    with tempfile.TemporaryDirectory(prefix="eMuleAI-Docker-") as temporary_dir:
        build_context = Path(temporary_dir)
        try:
            prepare_build_context(script_dir, template_dir, executable_path, build_context)
        except OSError as exc:
            raise DockerizeError(f"Could not prepare the temporary Docker build context.\n{exc}") from exc
        try:
            run_build(docker, build_context, version, tags, publish)
        except DockerizeError as exc:
            if publish:
                raise DockerizeError(
                    "Docker image build or Docker Hub push failed.\n"
                    f"Verify that the signed-in Docker Hub account can push to {IMAGE_NAME}.\n{exc}"
                ) from exc
            raise DockerizeError(f"Local Docker image build failed.\n{exc}") from exc

    if publish:
        print("\nPublished Docker Hub image details:")
        inspect = run_command(
            [docker, "buildx", "imagetools", "inspect", f"{IMAGE_NAME}:{version}"],
            check=False,
        )
        if inspect.returncode != 0:
            print("WARNING: The image was pushed, but its published details could not be inspected.")

        print("\nApplying Docker Hub retention policy...")
        print(
            f"Keeping the newest {KEEP_STABLE_RELEASES} stable full-version tags and "
            f"{KEEP_PRERELEASE_RELEASES} prerelease full-version tags."
        )
        try:
            apply_retention_policy(
                docker,
                IMAGE_NAME,
                version,
                KEEP_STABLE_RELEASES,
                KEEP_PRERELEASE_RELEASES,
            )
        except RetentionError as exc:
            print("WARNING: The image was published, but old Docker Hub tags could not be fully cleaned up.")
            print(f"         {exc}")
            print("         The publication remains valid. Old tags can be deleted from Docker Hub manually.")

        print("\nDocker Hub publication completed successfully.")
    else:
        print("\nLocal test image:")
        listing = run_command([docker, "image", "ls", IMAGE_NAME], check=False)
        if listing.returncode != 0:
            print("WARNING: The local image was built, but its details could not be displayed.")
        print("NOTE: Existing local version tags are preserved. Docker Desktop may still show older tags.")
        print("\nLocal Docker image build completed successfully.")
        print(f"Test tag: {IMAGE_NAME}:local")

    return 0


def entrypoint() -> int:
    try:
        return main(sys.argv[1:])
    except SystemExit as exc:
        return int(exc.code or 0)
    except KeyboardInterrupt:
        print("\nERROR: Operation cancelled.", file=sys.stderr)
        pause_on_error()
        return 130
    except DockerizeError as exc:
        print(f"\nERROR: {exc}", file=sys.stderr)
        pause_on_error()
        return 1
    except Exception as exc:  # pragma: no cover - unexpected failure diagnostics
        print(f"\nERROR: Unexpected failure: {exc}", file=sys.stderr)
        traceback.print_exc()
        pause_on_error()
        return 1


if __name__ == "__main__":
    raise SystemExit(entrypoint())
