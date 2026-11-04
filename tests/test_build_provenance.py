"""Exercise the production provenance module without PlatformIO or hardware."""

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "build_provenance.py"
SPEC = importlib.util.spec_from_file_location("build_provenance", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
provenance = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(provenance)


class BuildProvenanceTests(unittest.TestCase):
    PUBLIC_FILES = {
        "src/main.cpp": b"int main() { return 0; }\n",
        "include/radio.h": b"#pragma once\n",
        "platformio.ini": b"[env:native]\nplatform = native\n",
        "tools/build_provenance.py": b"# public build script\n",
    }

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="private-local-project-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.populate(self.root)

    @classmethod
    def populate(cls, root, reverse=False):
        files = list(cls.PUBLIC_FILES.items())
        if reverse:
            files.reverse()
        for relative, contents in files:
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(contents)

    def identity(self):
        return provenance.source_identity(self.root)

    def test_fingerprint_is_independent_of_creation_order_and_absolute_path(self):
        with tempfile.TemporaryDirectory(prefix="different-private-location-") as other:
            self.populate(Path(other), reverse=True)
            first = self.identity()
            second = provenance.source_identity(Path(other))
        self.assertEqual(first["source_sha256"], second["source_sha256"])
        self.assertEqual(first["source_file_count"], 4)
        self.assertRegex(first["source_sha256"], r"^[a-f0-9]{64}$")

    def test_exact_source_byte_change_changes_fingerprint(self):
        before = self.identity()["source_sha256"]
        (self.root / "src/main.cpp").write_bytes(b"int main() { return 1; }\r\n")
        self.assertNotEqual(before, self.identity()["source_sha256"])

    def test_platformio_and_build_script_bytes_are_inputs(self):
        before = self.identity()["source_sha256"]
        (self.root / "platformio.ini").write_bytes(b"[env:native]\nplatform = native@1\n")
        after_platform = self.identity()["source_sha256"]
        self.assertNotEqual(before, after_platform)
        (self.root / "tools/build_provenance.py").write_bytes(b"# changed public script\n")
        self.assertNotEqual(after_platform, self.identity()["source_sha256"])

    def test_private_generated_test_and_build_files_are_excluded(self):
        before = self.identity()["source_sha256"]
        excluded = (
            "include/private_config.h",
            "include/config.local.h",
            "include/credentials.h",
            "include/build_provenance.h",
            "src/secrets/network.h",
            "src/generated/automatic.h",
            "src/private_settings/network.h",
            "platformio.local.ini",
            ".pio/build/native/firmware.bin",
            ".tools/compiler/private.txt",
            "tests/test_source.cpp",
            "build/automatic.h",
            "tools/private_settings.py",
            "tools/local/release.py",
        )
        for relative in excluded:
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(b"PASSWORD=do-not-publish-credential; IP=192.0.2.123\n")
        identity = self.identity()
        self.assertEqual(before, identity["source_sha256"])
        self.assertEqual(identity["source_file_count"], 4)
        self.assertIn("private", " ".join(identity["exclusions"]).lower())
        serialized = json.dumps(identity)
        self.assertNotIn("do-not-publish-credential", serialized)
        self.assertNotIn("192.0.2.123", serialized)
        self.assertNotIn("private_config.h", serialized)
        self.assertNotIn(str(self.root), serialized)

    def test_symlink_to_private_file_is_not_a_public_input(self):
        before = self.identity()["source_sha256"]
        private = self.root / "private_config.h"
        private.write_bytes(b"do-not-publish-symlink-credential")
        try:
            (self.root / "include/external.h").symlink_to(private)
        except (OSError, NotImplementedError):
            self.skipTest("Symlink creation is unavailable on this host")
        self.assertEqual(before, self.identity()["source_sha256"])

    def test_header_contains_only_validated_public_identity(self):
        identity = {
            "git_revision": "0123456789abcdef0123456789abcdef01234567",
            "git_dirty": True,
            "source_sha256": "a" * 64,
            "source_file_count": 4,
            "exclusions": ["Private/local configuration is excluded"],
            "untrusted_path": str(self.root / "credentials.h"),
            "untrusted_secret": "do-not-publish-credential",
        }
        header = provenance.render_header(identity)
        self.assertIn('#define YOKIS_BUILD_GIT "0123456789ab-dirty"', header)
        self.assertIn('#define YOKIS_BUILD_SOURCE_SHA256 "' + "a" * 64 + '"', header)
        self.assertIn('#define YOKIS_BUILD_ID "0123456789ab-dirty+src.aaaaaaaaaaaa"', header)
        self.assertNotIn(str(self.root), header)
        self.assertNotIn("do-not-publish-credential", header)

    def test_receipt_hashes_built_bytes_and_filters_metadata(self):
        identity = self.identity()
        builddir = self.root / ".pio/build/native"
        builddir.mkdir(parents=True)
        binary = builddir / "firmware.bin"
        binary.write_bytes(b"actual binary bytes\x00\xff")
        metadata = {
            "platform": {"name": "espressif8266", "version": "4.2.1"},
            "compiler": {"name": "xtensa-lx106-elf-g++", "version": "10.3.0"},
            "dependencies": [{"name": "RF24", "version": "1.4.8"}],
            "compiler_flags": "-DWIFI_PASSWORD=do-not-publish-credential",
            "private_path": str(self.root / "private/credentials.h"),
            "ip_address": "192.0.2.123",
        }
        receipt_path = provenance.write_receipt(
            self.root, builddir, identity, [binary], "native", metadata
        )
        self.assertEqual(receipt_path, builddir / "build-receipt.json")
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        self.assertEqual(receipt["schema_version"], 1)
        self.assertEqual(receipt["environment"], "native")
        self.assertEqual(receipt["source"]["sha256"], identity["source_sha256"])
        self.assertIn("revision", receipt["git"])
        self.assertIn("dirty", receipt["git"])
        self.assertEqual(receipt["artifacts"], [{
            "name": "firmware.bin",
            "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
            "size": len(binary.read_bytes()),
        }])
        self.assertEqual(receipt["metadata"]["platform"], metadata["platform"])
        self.assertEqual(receipt["metadata"]["compiler"], metadata["compiler"])
        self.assertEqual(receipt["metadata"]["dependencies"], metadata["dependencies"])
        serialized = json.dumps(receipt)
        for forbidden in (str(self.root), "credentials.h", "do-not-publish-credential", "192.0.2.123", "compiler_flags"):
            self.assertNotIn(forbidden, serialized)

    def test_receipt_rejects_artifacts_outside_build_directory(self):
        outside = self.root / "private_config.bin"
        outside.write_bytes(b"do-not-read-as-a-build-artifact")
        builddir = self.root / ".pio/build/native"
        builddir.mkdir(parents=True)
        with self.assertRaises(ValueError):
            provenance.write_receipt(self.root, builddir, self.identity(), [outside], "native")


if __name__ == "__main__":
    unittest.main()
