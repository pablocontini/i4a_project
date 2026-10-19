"""Verify STA discovery cancellation and DHCP retries against the real C code."""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    build = ROOT / "app/build/native-tests"
    build.mkdir(parents=True, exist_ok=True)
    command = [args.cc, "-Wall", "-Werror"]
    for include in ("tests/native/wireless/mocks", "components/wireless/station",
                    "components/wireless/access_point", "components/wireless/device",
                    "components/wireless/client", "components/wireless/server"):
        command += ["-I", str(ROOT / include)]
    tests = str(ROOT / "tests/native/wireless/lifecycle_test.c")
    if Path(args.cc).stem.lower() == "tcc":
        subprocess.run(command + ["-run", tests], check=True)
    else:
        executable = build / ("wireless_lifecycle.exe" if sys.platform == "win32"
                              else "wireless_lifecycle")
        subprocess.run(command + [tests, "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
