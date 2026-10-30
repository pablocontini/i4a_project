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
        "integration/neighbor_manager/include",
    ):
        path = ROOT / include if include.startswith("tests/") else ROOT / "components" / include
        command += ["-I", str(path)]
    command += [str(ROOT / "tests/native/routing/recovery_test.c")]
    command += [str(ROOT / "components/integration/neighbor_manager/src" / source)
                for source in ("blocks.c", "registry.c")]
    for source in ("forwarder/forwarder.c", "forwarder/peer.c", "forwarder/sibling.c",
                   "root.c", "home.c", "routing.c", "routing_table.c", "utils.c"):
        command += [str(ROOT / "components/routing/routing/src" / source)]
    command += ["-o", str(library)]
    subprocess.run(command, check=True)
    native = ctypes.CDLL(str(library))
    failures = []
    for name in (
        "test_transit_request_preserves_working_parent_and_distance",
        "test_transit_retry_preserves_live_uplink_and_original_deadline",
        "test_confirmation_answers_pending_child_once_without_reelection",
        "test_pending_confirmation_survives_failed_peer_send",
        "test_unsolicited_gateway_winner_does_not_reply_to_parent",
        "test_early_peer_gateway_request_cannot_block_initial_provision",
        "test_better_gateway_still_replaces_live_parent_after_transit_request",
        "test_three_node_transit_confirmation_finishes_without_echo_or_reselection",
        "test_child_restart_cannot_replace_a_live_parent",
        "test_unknown_sibling_distance_cannot_steal_a_live_parent",
        "test_confirmation_dispatch_is_safe_for_root_center_and_home",
        "test_selected_parent_distance_increase_requires_pending_confirmation",
        "test_four_nodes_confirm_only_forwarded_child_then_retry_other_return_path",
        "test_confirmation_preserves_an_unrelated_outbound_request",
        "test_unsolicited_improvement_does_not_confirm_an_unforwarded_child_path",
        "test_gateway_trace_records_request_retry_and_winner_without_changing_deadlines",
        "test_gateway_trace_is_bounded_and_preserves_event_order",
        "test_moved_block_is_not_redelegated_to_a_new_node",
        "test_changed_peer_claim_cannot_steal_a_return_route",
        "test_gateway_messages_cannot_replace_initial_network_provision",
        "test_delayed_provision_cannot_renumber_a_node_during_gateway_recovery",
        "test_initial_uplink_counts_the_node_once",
        "test_home_restores_missing_client_route_without_restarting_ap",
        "test_home_does_not_invent_client_route_before_provision",
        "test_initial_provision_retries_loss_without_renumbering_or_duplicate_ap",
        "test_pending_provision_waits_for_uplink_and_resumes_after_reconnect",
        "test_root_provision_loss_retries_without_panic_or_gateway_change",
        "test_root_gateway_response_retries_loss_without_deadline_starvation",
        "test_gateway_request_keeps_the_root_default_route",
        "test_unprovisioned_peer_handshake_preserves_provider_gateway",
        "test_reconnected_provisioned_node_restores_gateway_without_renumbering",
        "test_sibling_gateway_winner_preserves_node_distance",
        "test_new_radio_restores_missing_default_at_unchanged_distance",
        "test_equal_distance_update_preserves_a_working_gateway",
        "test_equal_gateway_response_restores_only_a_missing_default",
        "test_equal_or_unreachable_gateway_does_not_replace_uplink",
        "test_late_longer_uplink_confirms_path_and_recovers",
        "test_missing_default_tick_recovers_a_lost_sibling_request",
        "test_late_recovery_path_returning_to_origin_is_rejected",
        "test_lost_original_uplink_recovers_through_existing_root_link",
        "test_route_and_destroy_use_mutex_addresses",
        "test_sibling_arrival_during_peer_batch_is_not_erased",
        "test_ticks_run_inside_routing_transaction",
        "test_failed_table_publication_blocks_next_batch_until_retry",
        "test_repeated_requests_do_not_postpone_root_response",
        "test_gateway_request_retries_after_loss_and_stops_on_winner",
        "test_lost_non_gateway_link_reannounces_return_path",
        "test_hag_cycle_does_not_route_own_network_through_peer",
        "test_invalid_distance_cannot_overflow_or_provision",
        "test_link_loss_survives_saturated_peer_queue",
        "test_subnet_validation_rejects_invalid_provisions_before_mutation",
    ):
        test = getattr(native, name)
        test.restype = ctypes.c_int
        line = test()
        if line:
            failures.append(f"{name}: failed at recovery_test.c:{line}")
            print(failures[-1])
        else:
            print(f"{name}: OK")
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
