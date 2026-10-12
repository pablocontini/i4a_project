"""Compile and run the real discovery arbiter/RPC without ESP hardware."""

import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc", help="GCC/Clang or TinyCC")
    parser.add_argument("--run-in-memory", action="store_true", help="Use TinyCC -run without a DLL/executable artifact")
    args = parser.parse_args()
    build = ROOT / "app/build/native-tests"
    build.mkdir(parents=True, exist_ok=True)
    command = [args.cc, "-Wall", "-Werror", "-DDISCOVERY_NATIVE_MAIN"]
    for include in (
        ROOT / "tests/native/discovery/mocks",
        ROOT / "components/integration/discovery_manager/include",
        ROOT / "components/integration/neighbor_manager/include",
        ROOT / "tests/native/neighbors/mocks",
        ROOT / "tests/native/mocks",
        ROOT / "components/routing/os/include",
    ):
        command += ["-I", str(include)]
    source = ROOT / "tests/native/discovery/manager_test.c"
    if args.run_in_memory:
        subprocess.run(command + ["-run", str(source)], check=True)
    else:
        executable = build / ("discovery_tests.exe" if sys.platform == "win32" else "discovery_tests")
        subprocess.run(command + [str(source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
