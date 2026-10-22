"""Exercise production startup and station RSSI code without ESP hardware."""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--run", action="store_true",
                        help="Use TinyCC -run instead of loading DLLs or executables")
    args = parser.parse_args()
    build = ROOT / "app/build/native-tests"
    build.mkdir(parents=True, exist_ok=True)
    for suite in ("reset", "station", "settings", "button"):
        command = [
            args.cc, "-Wall", "-Werror", "-include",
            str(ROOT / "tests/native/routing/mocks/compiler.h"),
            "-I", str(ROOT / "tests/native/portal/mocks"),
            "-I", str(ROOT / "components/integration/reset_manager/include"),
            "-I", str(ROOT / "components/integration/portal_settings/include"),
            "-I", str(ROOT / "components/wireless/station"),
        ]
        source = str(ROOT / f"tests/native/portal/{suite}_test.c")
        if args.run:
            command += ["-run", source]
            subprocess.run(command, check=True)
        else:
            executable = build / (f"portal_{suite}.exe" if sys.platform == "win32"
                                  else f"portal_{suite}")
            subprocess.run(command + [source, "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
