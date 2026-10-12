"""Verify pbuf ownership in the real SPI netif input callback."""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--source", type=Path,
                        default=ROOT / "components/internal/ring_link_netif/ring_link_netif_input.c")
    args = parser.parse_args()
    build = ROOT / "app/build/native-tests"
    build.mkdir(parents=True, exist_ok=True)
    for report_errors in (0, 1):
        executable = build / (f"netif_{report_errors}.exe" if sys.platform == "win32"
                              else f"netif_{report_errors}")
        command = [args.cc, "-Wall", "-Werror"]
        command += [f"-DCONFIG_ESP_NETIF_RECEIVE_REPORT_ERRORS={report_errors}",
                    "-I", str(ROOT / "tests/native/netif/mocks"), str(args.source)]
        tests = str(ROOT / "tests/native/netif/input_test.c")
        if Path(args.cc).stem.lower() == "tcc":
            subprocess.run(command + ["-run", tests], check=True)
        else:
            subprocess.run(command + [tests, "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
