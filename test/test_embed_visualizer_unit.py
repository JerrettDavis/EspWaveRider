import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

from embed_visualizer_lib import build_cpp_defines, cpp_string, render_header, resolve_firmware_version  # noqa: E402


class EmbedVisualizerUnitTests(unittest.TestCase):
    def test_cpp_string_escapes_quotes_and_backslashes(self):
        self.assertEqual(cpp_string('a\\b"c'), '\\"a\\\\b\\"c\\"')

    def test_resolve_firmware_version_prefers_github_ref_name(self):
        version = resolve_firmware_version(
            Path('.'),
            env_vars={"GITHUB_REF_NAME": "v1.2.3"},
            git_runner=lambda *args: "ignored",
        )
        self.assertEqual(version, "v1.2.3")

    def test_resolve_firmware_version_falls_back_to_git_describe_then_sha(self):
        describe_version = resolve_firmware_version(
            Path('.'),
            env_vars={},
            git_runner=lambda *args: "v1.0.1-3-g1234567" if args[0] == "describe" else "1234567",
        )
        self.assertEqual(describe_version, "v1.0.1-3-g1234567")

        sha_version = resolve_firmware_version(
            Path('.'),
            env_vars={},
            git_runner=lambda *args: "" if args[0] == "describe" else "1234567",
        )
        self.assertEqual(sha_version, "dev-1234567")

    def test_render_header_rejects_delimiter_collision(self):
        with self.assertRaises(RuntimeError):
            render_header("LBHTML")

    def test_build_cpp_defines_emits_expected_macros(self):
        defines = build_cpp_defines(
            Path('.'),
            "esp32-s3-devkitm-1",
            env_vars={"GITHUB_REF_NAME": "v1.2.3"},
            git_runner=lambda *args: "abc1234",
        )
        self.assertEqual(
            defines,
            [
                ("ESPWAVERIDER_FIRMWARE_VERSION", '\\"v1.2.3\\"'),
                ("ESPWAVERIDER_BUILD_TARGET", '\\"esp32-s3-devkitm-1\\"'),
                ("ESPWAVERIDER_GIT_SHA", '\\"abc1234\\"'),
            ],
        )


if __name__ == "__main__":
    unittest.main()