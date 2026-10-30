import importlib.util
import argparse
from contextlib import redirect_stderr
import io
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "auxiliar"))
spec = importlib.util.spec_from_file_location("build_link_capture", ROOT / "auxiliar/build_link_capture.py")
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class DiagnosticBuildTests(unittest.TestCase):
    def test_existing_variants_keep_their_ssid_and_manifest_identity(self):
        for variant in ("grillo", "ramona"):
            with self.subTest(variant=variant):
                _, args = build.parse_arguments(["--variant", variant])
                self.assertEqual(args.variant, variant)
                self.assertEqual(args.ssid, "ComNetAR - " + variant.capitalize())

    def test_custom_ssid_keeps_its_name_and_uses_custom_manifest_identity(self):
        _, args = build.parse_arguments([
            "--ssid", "ComNetAR - Nodo del patio", "--output", "firmware/patio"])
        self.assertEqual(args.variant, "custom")
        self.assertEqual(args.ssid, "ComNetAR - Nodo del patio")
        self.assertEqual(args.output, Path("firmware/patio"))

    def test_identity_selection_is_required_and_exclusive(self):
        for argv in ([], ["--variant", "grillo", "--ssid", "Otro nodo"]):
            with self.subTest(argv=argv), redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    build.parse_arguments(argv)
                self.assertEqual(error.exception.code, 2)

    def test_ssid_accepts_normal_names_and_checks_utf8_byte_length(self):
        for value in ("A", "a" * 31, "ComNetAR - Casa de José", "é" * 15 + "a",
                      "Nodo 'El patio'", "Nodo (Norte)"):
            with self.subTest(value=value):
                self.assertEqual(build.validate_ssid(value), value)
        for value in ("", "a" * 32, "é" * 16, "\ud800"):
            with self.subTest(value=repr(value)):
                with self.assertRaises(argparse.ArgumentTypeError):
                    build.validate_ssid(value)

    def test_ssid_rejects_controls_and_cmake_or_c_string_escapes(self):
        for suffix in ("\0", "\n", "\r", "\t", "\x1b", "\x7f", "\x85",
                       '"', "\\", ";", "$<CONFIG:Debug>"):
            with self.subTest(suffix=repr(suffix)), redirect_stderr(io.StringIO()):
                with self.assertRaises(argparse.ArgumentTypeError):
                    build.validate_ssid("Nodo" + suffix)
                with self.assertRaises(SystemExit) as error:
                    build.parse_arguments(["--ssid", "Nodo" + suffix])
                self.assertEqual(error.exception.code, 2)

    def test_enabling_diagnostics_preserves_flash_wifi_and_timing_settings(self):
        original = ("CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y\nCONFIG_LOG_DEFAULT_LEVEL_NONE=y\n"
                    "# CONFIG_I4A_CAPTURE_DIAGNOSTICS is not set\nCONFIG_LWIP_TCPIP_TASK_STACK_SIZE=3072\n")
        updated = build.capture_config(original)
        self.assertIn("CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y\n", updated)
        self.assertIn("CONFIG_LOG_DEFAULT_LEVEL_NONE=y\n", updated)
        self.assertIn("CONFIG_LWIP_TCPIP_TASK_STACK_SIZE=3072\n", updated)
        self.assertEqual(updated.count("CONFIG_I4A_CAPTURE_DIAGNOSTICS"), 1)
        self.assertIn("CONFIG_I4A_CAPTURE_DIAGNOSTICS=y\n", updated)

    def test_rebuilding_does_not_accumulate_duplicate_configuration(self):
        once = build.capture_config("CONFIG_I4A_CAPTURE_DIAGNOSTICS=n\nCONFIG_OTHER=y\n")
        self.assertEqual(build.capture_config(once), once)

    def test_existing_sdkconfig_migrates_small_http_header_limit(self):
        for setting in ("", "CONFIG_HTTPD_MAX_REQ_HDR_LEN=512\n",
                        "# CONFIG_HTTPD_MAX_REQ_HDR_LEN is not set\n"):
            with self.subTest(setting=setting):
                updated = build.capture_config(setting + "CONFIG_HTTPD_MAX_URI_LEN=512\n")
                self.assertEqual(updated.count("CONFIG_HTTPD_MAX_REQ_HDR_LEN"), 1)
                self.assertIn("CONFIG_HTTPD_MAX_REQ_HDR_LEN=4096\n", updated)
                self.assertIn("CONFIG_HTTPD_MAX_URI_LEN=512\n", updated)

    def test_existing_larger_http_header_limit_is_preserved(self):
        updated = build.capture_config("CONFIG_HTTPD_MAX_REQ_HDR_LEN=8192\n")
        self.assertIn("CONFIG_HTTPD_MAX_REQ_HDR_LEN=8192\n", updated)
        self.assertNotIn("CONFIG_HTTPD_MAX_REQ_HDR_LEN=4096\n", updated)


if __name__ == "__main__":
    unittest.main()
