"""Exercise lease renewal loss against the actual peer session and registry C code."""
import argparse
import ctypes
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
TESTS=(
    "test_transient_renew_failure_preserves_transport_and_retries",
    "test_persistent_rpc_loss_expires_without_extending_lease_or_spinning",
    "test_slow_failed_rpc_keeps_valid_lease_and_rate_limits_from_completion",
    "test_expiry_during_rpc_prevents_buffer_delivery",
    "test_replaced_owner_during_rpc_prevents_buffer_delivery",
    "test_shutdown_during_rpc_prevents_buffer_delivery",
    "test_partial_frame_still_has_bounded_completion_time",
    "test_lost_ownership_and_explicit_stop_prevent_send",
    "test_lost_renew_reply_does_not_allow_another_radio_to_replace_active_root",
)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc",default="cc",help="GCC/Clang, or TinyCC on Windows")
    parser.add_argument("--source",type=Path,default=ROOT/"components/wireless/peer_session/peer_session.c")
    parser.add_argument("--output",type=Path,default=ROOT/"app/build/native-peer-renew")
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    library=args.output/("peer_renew.dll" if sys.platform=="win32" else "libpeer_renew.so")
    command=[args.cc,"-shared","-Wall","-Werror"]
    if sys.platform!="win32":command+=["-fPIC"]
    if sys.platform.startswith("linux"):command+=["-Wl,-Bsymbolic"]
    for include in ("components/integration/neighbor_manager/include","tests/native/neighbors/mocks",
                    "tests/native/mocks","components/routing/os/include","components/wireless/peer_session/include"):
        command+=["-I",str(ROOT/include)]
    command+=[str(ROOT/"tests/native/peer_renew/session_renew_test.c"),str(args.source),
              str(ROOT/"components/integration/neighbor_manager/src/registry.c"),
              str(ROOT/"components/integration/neighbor_manager/src/blocks.c"),"-o",str(library)]
    subprocess.run(command,check=True)
    native=ctypes.CDLL(str(library.resolve()))
    results={}
    for name in TESTS:
        function=getattr(native,name);function.restype=ctypes.c_int
        line=function();results[name]={"passed":line==0,"failure_line":line}
        print(f"{name}: {'OK' if not line else f'FAILED at C line {line}'}")
    (args.output/"results.json").write_text(json.dumps(results,indent=2)+"\n",encoding="utf-8")
    return 0 if all(result["passed"] for result in results.values()) else 1

if __name__=="__main__":sys.exit(main())
