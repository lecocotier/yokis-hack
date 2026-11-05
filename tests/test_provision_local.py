"""Private OTA provisioning never puts authentication in a PlatformIO env dump."""
import configparser
import contextlib
import importlib.util
import io
from pathlib import Path
import re
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("provision_local", ROOT / "tools/provision_local.py")
provision = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provision)


class ProvisionLocalTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / "include").mkdir()
        self.header = self.root / "include/local_secrets.h"
        self.config = self.root / "platformio.local.ini"

    def run_provision(self):
        output = io.StringIO()
        with mock.patch.object(provision, "ROOT", self.root), contextlib.redirect_stdout(output):
            self.assertEqual(provision.main(), 0)
        config = configparser.ConfigParser(interpolation=None)
        config.read(self.config)
        return config, output.getvalue()

    def test_legacy_flags_move_without_replacing_installed_key_or_ports(self):
        future_key = "b" * 32
        installed_key = "installed-fixture-key"
        header = '#define YOKIS_OTA_PASSWORD "' + future_key + '"\n'
        self.header.write_text(header)
        flags = "--auth=" + installed_key + " --host_port=3232 --port=8267"
        self.config.write_text("[env:d1_mini_ota]\nupload_port=192.0.2.5\nupload_flags=" + flags +
                               "\n[custom]\nkeep=value\n")
        config, output = self.run_provision()
        self.assertFalse(config.has_option("env:d1_mini_ota", "upload_flags"))
        self.assertEqual(config.get("ota_private:d1_mini_ota", "upload_flags"), flags)
        self.assertEqual(config.get("env:d1_mini_ota", "upload_port"), "192.0.2.5")
        self.assertEqual(config.get("custom", "keep"), "value")
        self.assertEqual(self.header.read_text(), header)
        self.assertNotIn(installed_key, output)
        self.assertNotIn(future_key, output)

    def test_existing_private_key_is_kept_and_repeated_provision_is_idempotent(self):
        self.header.write_text('#define YOKIS_OTA_PASSWORD "' + "b" * 32 + '"\n')
        self.config.write_text("[env:d1_mini_ota]\nupload_port=192.0.2.5\n"
                               "[ota_private:d1_mini_ota]\nupload_flags=-ainstalled-fixture-key -P3232\n")
        first, output = self.run_provision()
        self.assertFalse(first.has_option("env:d1_mini_ota", "upload_flags"))
        self.assertEqual(first.get("ota_private:d1_mini_ota", "upload_flags"),
                         "-ainstalled-fixture-key -P3232")
        committed = self.config.read_bytes()
        self.run_provision()
        self.assertEqual(self.config.read_bytes(), committed)
        self.assertNotIn("installed-fixture-key", output)

    def test_fresh_credentials_are_independent_private_and_never_printed(self):
        config, output = self.run_provision()
        values = re.findall(r'#define YOKIS_\w+_PASSWORD "([0-9a-f]{32})"', self.header.read_text())
        self.assertEqual(len(set(values)), 4)
        self.assertFalse(config.has_option("env:d1_mini_ota", "upload_flags"))
        self.assertTrue(config.has_option("ota_private:d1_mini_ota", "upload_flags"))
        for value in values:
            self.assertNotIn(value, output)

    def test_conflicting_sections_are_preserved_for_private_review(self):
        self.header.write_text('#define YOKIS_OTA_PASSWORD "' + "b" * 32 + '"\n')
        self.config.write_text("[env:d1_mini_ota]\nupload_flags=--auth=first-fixture\n"
                               "[ota_private:d1_mini_ota]\nupload_flags=--auth=second-fixture\n")
        original = self.config.read_bytes()
        with mock.patch.object(provision, "ROOT", self.root):
            with self.assertRaisesRegex(RuntimeError, "Conflicting"):
                provision.main()
        self.assertEqual(self.config.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
