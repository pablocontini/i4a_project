#!/usr/bin/env python3
"""Build a diagnostic image of this branch and export a self-contained USB bundle."""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import zipfile

from export_firmware_bundle import export_bundle

ROOT = Path(__file__).resolve().parents[1]


def environment():
    """Use the activated IDF shell, or reuse this checkout's existing tool paths."""
    env = dict(os.environ)
    cache = ROOT / "app/build/CMakeCache.txt"
    entries = {}
    if cache.is_file():
        for line in cache.read_text(encoding="utf-8").splitlines():
            match = re.match(r"([^:#]+):[^=]+=(.*)", line)
            if match:
                entries[match[1]] = match[2]
    commands = ROOT / "app/build/compile_commands.json"
    if not entries.get("CMAKE_C_COMPILER") and commands.is_file():
        compilation = json.loads(commands.read_text(encoding="utf-8"))
        if compilation:
            first = compilation[0]
            arguments = first.get("arguments") or shlex.split(first["command"], posix=False)
            entries["CMAKE_C_COMPILER"] = arguments[0].strip('"')
    idf = env.get("IDF_PATH") or entries.get("IDF_PATH")
    config = ROOT / "app/build/config.env"
    if not idf and config.is_file():
        idf = json.loads(config.read_text(encoding="utf-8")).get("IDF_PATH")
    if not idf or not (Path(idf) / "tools/idf.py").is_file():
        raise ValueError("Activate the ESP-IDF 5.4.1 environment before building")
    env["IDF_PATH"] = idf
    python = entries.get("PYTHON", sys.executable)
    if env.get("IDF_PYTHON_ENV_PATH"):
        candidate = Path(env["IDF_PYTHON_ENV_PATH"]) / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
        if candidate.is_file():
            python = str(candidate)
    if not Path(python).is_file():
        python = sys.executable
    env.setdefault("IDF_PYTHON_ENV_PATH", str(Path(python).parent.parent))
    if "IDF_TOOLS_PATH" not in env and (Path.home() / ".espressif/espidf.constraints.v5.4.txt").is_file():
        env["IDF_TOOLS_PATH"] = str(Path.home() / ".espressif")
    paths = [str(Path(python).parent)]
    for key in ("CMAKE_C_COMPILER", "CMAKE_COMMAND", "CMAKE_MAKE_PROGRAM"):
        if entries.get(key) and Path(entries[key]).is_file():
            paths.append(str(Path(entries[key]).parent))
    compiler = Path(entries.get("CMAKE_C_COMPILER", python))
    if "ESP_ROM_ELF_DIR" not in env:
        for parent in compiler.parents:
            roms = parent / "esp-rom-elfs"
            if roms.is_dir():
                versions = sorted(p for p in roms.iterdir() if p.is_dir())
                if versions:
                    env["ESP_ROM_ELF_DIR"] = str(versions[-1])
                break
    env["PATH"] = os.pathsep.join(paths + [env.get("PATH", "")])
    return python, env


def capture_config(text):
    lines = [line for line in text.splitlines() if not re.match(
        r"(?:# )?CONFIG_I4A_CAPTURE_DIAGNOSTICS(?:=| is not set)", line)]
    return "\n".join(lines) + "\nCONFIG_I4A_CAPTURE_DIAGNOSTICS=y\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--variant", choices=("ramona", "grillo"), required=True)
    parser.add_argument("--base-sdkconfig", type=Path, default=ROOT / "app/sdkconfig")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    work = ROOT / "app/build/link-capture"
    output = (args.output or work / args.variant).resolve()
    if output.exists():
        parser.error("Output already exists; preserve it and choose another --output")
    if not args.base_sdkconfig.is_file():
        parser.error("Base sdkconfig is missing; build/configure the normal firmware first")
    try:
        python, env = environment()
    except ValueError as exc:
        parser.error(str(exc))
    work.mkdir(parents=True, exist_ok=True)
    lock = work / "build.lock"
    try:
        handle = lock.open("x", encoding="ascii")
    except FileExistsError:
        parser.error("Another diagnostic build is active (" + str(lock) + ")")
    try:
        with handle:
            handle.write(str(os.getpid()))
        sdkconfig = work / "sdkconfig"
        contents = capture_config(args.base_sdkconfig.read_text(encoding="utf-8"))
        if not sdkconfig.exists() or sdkconfig.read_text(encoding="utf-8") != contents:
            sdkconfig.write_text(contents, encoding="utf-8")
        ssid = "ComNetAR - " + args.variant.capitalize()
        build = work / "build"
        command = [python, str(Path(env["IDF_PATH"]) / "tools/idf.py"), "-C", str(ROOT / "app"),
                   "-B", str(build), "-D", "SDKCONFIG=" + str(sdkconfig),
                   "-D", "I4A_HOUSE_NETWORK_NAME=" + ssid, "build"]
        print("Building diagnostic variant:", args.variant, "SSID:", ssid, flush=True)
        subprocess.run(command, env=env, check=True)
        export_bundle(build, output)
        for name in ("main.elf", "main.map"):
            shutil.copyfile(build / name, output / name)
        shutil.copyfile(sdkconfig, output / "sdkconfig")
        image = (output / "main.bin").read_bytes()
        sources = []
        for directory in (ROOT / "components", ROOT / "app/main"):
            sources += [p for p in directory.rglob("*") if p.is_file() and
                        (p.suffix in (".c", ".h", ".txt", ".yml", ".yaml", ".defaults") or p.name.startswith("Kconfig"))]
        sources += [ROOT / "app/CMakeLists.txt", ROOT / "app/partitions.csv", ROOT / "app/partitions_2mb.csv"]
        with zipfile.ZipFile(output / "sources.zip", "w", zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(set(sources)):
                archive.write(path, path.relative_to(ROOT).as_posix())
        info = {"built_utc": dt.datetime.now(dt.timezone.utc).isoformat(), "variant": args.variant,
                "ssid": ssid, "diagnostics": True, "source_worktree": str(ROOT),
                "main_bin_sha256": hashlib.sha256(image).hexdigest(), "size": len(image),
                "elf_sha256_reported_by_firmware": image[176:208].hex(),
                "sources_zip_sha256": hashlib.sha256((output / "sources.zip").read_bytes()).hexdigest()}
        (output / "capture-manifest.json").write_text(json.dumps(info, indent=2), encoding="utf-8")
        print("Bundle:", output, "\nELF identity:", info["elf_sha256_reported_by_firmware"], flush=True)
    finally:
        lock.unlink()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
