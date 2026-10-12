"""Exercise actual TX/RX SPI-netif output callbacks and failed handoffs."""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--compile-only", action="store_true")
    args = parser.parse_args()
    build = ROOT / "app/build/native-tests"
    build.mkdir(parents=True, exist_ok=True)
    for is_tx in (0, 1):
        name = "tx" if is_tx else "rx"
        executable = build / (f"netif_output_{name}.exe" if sys.platform == "win32"
                              else f"netif_output_{name}")
        command = [args.cc, "-Wall", "-Werror", "-Wno-unused-function",
                   "-Wno-unused-variable", f"-DTEST_TX={is_tx}", "-I",
                   str(ROOT / "tests/native/netif_output/mocks")]
        source = str(ROOT / "tests/native/netif_output/output_test.c")
        if Path(args.cc).stem.lower() == "tcc" and not args.compile_only:
            subprocess.run(command + ["-run", source], check=True)
        else:
            subprocess.run(command + [source, "-o", str(executable)], check=True)
            if not args.compile_only:
                subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
