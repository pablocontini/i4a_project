"""Exercise production ICMP diagnostic parsing, bounded copies and rate limits."""
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
    command = [args.cc, "-Wall", "-Werror",
               "-I", str(ROOT / "tests/native/ip_diagnostics/mocks"),
               "-I", str(ROOT / "components/internal/ip_diagnostics/include"),
               str(ROOT / "components/internal/ip_diagnostics/src/ip_diagnostics.c")]
    tests = str(ROOT / "tests/native/ip_diagnostics/diagnostics_test.c")
    if Path(args.cc).stem.lower() == "tcc":
        subprocess.run(command + ["-run", tests], check=True)
    else:
        executable = build / ("ip_diagnostics.exe" if sys.platform == "win32" else "ip_diagnostics")
        subprocess.run(command + [tests, "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)

if __name__ == "__main__":
    main()
