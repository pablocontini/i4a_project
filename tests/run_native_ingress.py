"""Exercise actual ring ingress with a stalled/full application queue."""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    command = [args.cc, "-Wall", "-Werror", "-I", str(ROOT / "tests/native/ingress/mocks")]
    source = str(ROOT / "tests/native/ingress/ingress_test.c")
    if Path(args.cc).stem.lower() == "tcc":
        subprocess.run(command + ["-run", source], check=True)
    else:
        build = ROOT / "app/build/native-tests"
        build.mkdir(parents=True, exist_ok=True)
        executable = build / ("ingress.exe" if sys.platform == "win32" else "ingress")
        subprocess.run(command + [source, "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
