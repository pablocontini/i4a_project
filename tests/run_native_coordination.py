"""Exercise real sync/shared-state code in a five-board ring with dropped sends."""
import argparse
import ctypes
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
    library = build / ("coordination.dll" if sys.platform == "win32" else "libcoordination.so")
    command = [args.cc, "-shared", "-Wall", "-Werror", "-include",
               str(ROOT / "tests/native/routing/mocks/compiler.h")]
    if sys.platform != "win32":
        command += ["-fPIC"]
    for include in ("tests/native/neighbors/mocks", "routing/os/include", "routing/sync/include",
                    "routing/shared_state/include", "routing/ring_share/include",
                    "routing/siblings/include", "routing/routing_config/include"):
        path = ROOT / include if include.startswith("tests/") else ROOT / "components" / include
        command += ["-I", str(path)]
    command += [str(ROOT / "tests/native/coordination/coordination_test.c")]
    for component, source in (("sync", "sync"), ("sync", "leader"), ("sync", "follower"),
                              ("shared_state", "shared_state")):
        command += [str(ROOT / f"components/routing/{component}/src/{source}.c")]
    command += ["-o", str(library)]
    subprocess.run(command, check=True)
    native = ctypes.CDLL(str(library))
    for name in ("test_token_loss_replays_without_duplicate_callbacks",
                 "test_failed_token_send_and_request_coalescing",
                 "test_failed_state_publication_retains_token_until_retry",
                 "test_stale_malformed_and_wrong_owner_state_is_ignored",
                 "test_invalid_and_old_tokens_cannot_execute_callbacks"):
        test = getattr(native, name)
        test.restype = ctypes.c_int
        line = test()
        if line:
            raise AssertionError(f"{name}: coordination_test.c:{line}")
        print(f"{name}: OK", flush=True)

if __name__ == "__main__":
    main()
