"""Exercise real C routing callbacks for uplink loss and gateway recovery."""

import argparse
import ctypes
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc", help="GCC/Clang on Linux, TinyCC on Windows")
    parser.add_argument("--compile-only", action="store_true",
                        help="Compile both harnesses without loading or running their libraries")
    args = parser.parse_args()
    build = ROOT / "app/build/native-tests"
    build.mkdir(parents=True, exist_ok=True)
    library = build / ("routing_recovery.dll" if sys.platform == "win32" else "librouting_recovery.so")
    command = [args.cc, "-shared", "-Wall", "-Werror", "-include",
               str(ROOT / "tests/native/routing/mocks/compiler.h")]
    if sys.platform != "win32":
        command += ["-fPIC"]
    for include in (
        "tests/native/routing/mocks",
        "routing/routing/include", "routing/routing/src", "routing/routing/src/forwarder",
        "routing/routing_config/include", "routing/os/include", "routing/ring_share/include",
        "routing/shared_state/include", "routing/sync/include", "routing/wireless/include",
        "routing/siblings/include",
    ):
        path = ROOT / include if include.startswith("tests/") else ROOT / "components" / include
        command += ["-I", str(path)]
    base_command = command.copy()
    command += [str(ROOT / "tests/native/routing/recovery_test.c")]
    for source in ("forwarder/forwarder.c", "forwarder/peer.c", "forwarder/sibling.c",
                   "root.c", "home.c", "routing.c", "routing_table.c", "utils.c"):
        command += [str(ROOT / "components/routing/routing/src" / source)]
    command += ["-o", str(library)]
    subprocess.run(command, check=True)
    hook_library = build / ("routing_hooks.dll" if sys.platform == "win32" else "librouting_hooks.so")
    hook_command = base_command + [
        "-I", str(ROOT / "components/integration/routing_hooks"),
        str(ROOT / "tests/native/routing/hooks_test.c"),
        str(ROOT / "components/integration/routing_hooks/routing_hooks.c"),
        "-o", str(hook_library),
    ]
    subprocess.run(hook_command, check=True)
    if args.compile_only:
        print("Routing recovery and hook harnesses compiled; tests were not executed.")
        return
    native = ctypes.CDLL(str(library))
    failures = []
    for name in (
        "test_lost_request_retries_pending_loss_without_timer",
        "test_initial_uplink_counts_the_node_once",
        "test_idle_nodes_do_not_circulate_routing_tokens",
        "test_gateway_request_keeps_the_root_default_route",
        "test_sibling_gateway_winner_preserves_node_distance",
        "test_equal_or_unreachable_gateway_does_not_replace_uplink",
        "test_lost_original_uplink_recovers_through_existing_root_link",
        "test_lost_uplink_recovers_via_forwarder_without_renumbering",
        "test_gateway_retry_timeout_and_reply_cancel",
        "test_recovery_does_not_reprovision_or_accept_looped_requests",
        "test_root_requests_do_not_postpone_response",
        "test_tick_waits_for_routing_token_and_no_route_is_explicit",
        "test_handshake_rejects_unreachable_or_invalid_initial_allocation",
        "test_retry_recovers_lost_sibling_request_and_pauses_for_ota",
    ):
        test = getattr(native, name)
        test.restype = ctypes.c_int
        line = test()
        if line:
            failures.append(f"{name}: failed at recovery_test.c:{line}")
            print(failures[-1])
        else:
            print(f"{name}: OK")

    hooks = ctypes.CDLL(str(hook_library))
    name = "test_no_route_hook_blocks_default_fallback_without_freeing_packet"
    test = getattr(hooks, name)
    test.restype = ctypes.c_int
    line = test()
    if line:
        failures.append(f"{name}: failed at hooks_test.c:{line}")
        print(failures[-1])
    else:
        print(f"{name}: OK")
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
