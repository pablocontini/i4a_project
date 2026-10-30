"""Exercise AP association isolation against the real event handler."""
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
    for include in ("tests/native/access_point/mocks", "components/wireless/access_point",
                    "components/wireless/server"):
        command += ["-I", str(ROOT / include)]
    source = str(ROOT / "tests/native/access_point/association_test.c")
    if Path(args.cc).stem.lower() == "tcc":
        subprocess.run(command + ["-run", source], check=True)
    else:
        executable = build / ("ap_association.exe" if sys.platform == "win32" else "ap_association")
        subprocess.run(command + [source, "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
