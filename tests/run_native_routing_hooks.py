"""Verify explicit no-route decisions cannot fall back to lwIP's SPI route."""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    source = ROOT / "tests/native/routing_hooks/hook_test.c"
    command = [args.cc, "-Wall", "-Werror", "-Wno-unused-variable", "-I",
               str(ROOT / "tests/native/routing_hooks/mocks")]
    if Path(args.cc).stem.lower() == "tcc":
        subprocess.run(command + ["-run", str(source)], check=True)
    else:
        build = ROOT / "app/build/native-tests"
        build.mkdir(parents=True, exist_ok=True)
        executable = build / ("routing_hooks.exe" if sys.platform == "win32" else "routing_hooks")
        subprocess.run(command + [str(source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
