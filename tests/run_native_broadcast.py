"""Verify physical forwarding precedes local broadcast callbacks."""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    source = ROOT / "tests/native/broadcast/broadcast_test.c"
    command = [args.cc, "-Wall", "-Werror", "-I", str(ROOT / "tests/native/broadcast/mocks")]
    if Path(args.cc).stem.lower() == "tcc":
        subprocess.run(command + ["-run", str(source)], check=True)
    else:
        build = ROOT / "app/build/native-tests"
        build.mkdir(parents=True, exist_ok=True)
        executable = build / ("broadcast.exe" if sys.platform == "win32" else "broadcast")
        subprocess.run(command + [str(source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
