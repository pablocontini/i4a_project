"""Exercise the logical offer policy against initial, redundant and restored links."""
import argparse
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--run", action="store_true", help="Use TinyCC's native source runner")
    args = parser.parse_args()
    command = [args.cc, "-Wall", "-Werror"]
    source = str(ROOT / "tests/native/discovery/offer_test.c")
    if args.run:
        subprocess.run(command + ["-run", source], check=True)
    else:
        build = ROOT / "app/build/native-tests"
        build.mkdir(parents=True, exist_ok=True)
        binary = build / ("offer_tests.exe" if sys.platform == "win32" else "offer_tests")
        subprocess.run(command + [source, "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
if __name__ == "__main__":
    main()
