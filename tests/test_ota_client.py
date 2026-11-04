"""No hardware or UDP sends: only local TCP reservation sockets are real."""

import contextlib
import errno
import importlib.util
import io
from pathlib import Path
import socket
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]


def import_tool(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ota = import_tool("ota_client")
hook = import_tool("ota_upload")
FAKE_KEY = "test-only-installed-key-7d95"


class FakeDatagram:
    def close(self):
        pass


class SocketModule:
    """Keep SDK UDP construction inert while exercising real TCP reservation."""
    def __init__(self):
        self.udp_count = 0
        self.tcp_sockets = []

    def __getattr__(self, name):
        return getattr(socket, name)

    def socket(self, family=socket.AF_INET, kind=socket.SOCK_STREAM, *args, **kwargs):
        if kind == socket.SOCK_DGRAM:
            self.udp_count += 1
            return FakeDatagram()
        result = socket.socket(family, kind, *args, **kwargs)
        self.tcp_sockets.append(result)
        return result


class FakeSdk:
    def __init__(self):
        self.socket = SocketModule()
        self.original_socket = self.socket
        self.PROGRESS = False
        self.calls = []
        self.listener = None
        self.result = 0
        self.raise_after_bind = False
        self.reserved_before_bind = False
        self.reservation_exclusive = False

    def main(self, *_args):
        raise AssertionError("SDK main must never receive raw parsed options")

    def serve(self, remote, local, remote_port, local_port, password, filename, command=0):
        self.calls.append((remote, local, remote_port, local_port, password, filename, command))
        self.listener = self.socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.reserved_before_bind = self.listener.getsockname()[1] == local_port and local_port > 0
        self.listener.bind((local, local_port))
        self.listener.listen(1)
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            probe.bind((local, local_port))
        except OSError as failure:
            self.reservation_exclusive = failure.errno in (errno.EADDRINUSE, errno.EACCES)
        finally:
            probe.close()
        datagram = self.socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        datagram.close()
        if self.raise_after_bind:
            raise RuntimeError("untrusted SDK failure " + password)
        return self.result


class FakePlatform:
    def get_package_dir(self, name):
        if name != "framework-arduinoespressif8266":
            raise AssertionError(name)
        return str(ROOT / "fake-framework")


class FakeEnv(dict):
    def __init__(self, environment="d1_mini_ota", protocol="espota"):
        super().__init__(PIOENV=environment)
        self.protocol = protocol
        self.updates = {}

    def GetProjectOption(self, name, default=None):
        return self.protocol if name == "upload_protocol" else default

    def subst(self, expression):
        return str(ROOT) if expression == "$PROJECT_DIR" else expression

    def PioPlatform(self):
        return FakePlatform()

    def Replace(self, **kwargs):
        self.updates.update(kwargs)


class OtaClientTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.config = Path(self.temporary.name) / "platformio.local.ini"
        self.sdk = FakeSdk()
        self.write_config("--auth=" + FAKE_KEY)

    def write_config(self, flags):
        self.config.write_text(
            "[env:d1_mini_ota]\nupload_flags =\n" +
            "\n".join("    " + line for line in flags.splitlines()) + "\n",
            encoding="utf-8")

    def upload(self, **kwargs):
        return ota.run_upload(str(self.config), "d1_mini_ota", "unused-sdk.py",
                              "192.0.2.5", "firmware.bin", sdk_module=self.sdk, **kwargs)

    def arguments(self):
        return ["--espota-script", "unused-sdk.py", "--config", str(self.config),
                "--environment", "d1_mini_ota", "--target", "192.0.2.5",
                "-f", "firmware.bin", "--verbose"]

    def test_auth_variants_are_private_and_explicit_port_is_preserved(self):
        for auth in ("--auth=" + FAKE_KEY, "--auth " + FAKE_KEY,
                     "-a " + FAKE_KEY, "-a=" + FAKE_KEY, "-a" + FAKE_KEY):
            with self.subTest(auth_form=auth.split(FAKE_KEY)[0]):
                self.write_config(auth + "\n--host_port=3232")
                settings = ota.load_settings(str(self.config), "d1_mini_ota")
                self.assertEqual(settings.password, FAKE_KEY)
                self.assertEqual(settings.host_port, 3232)
                self.assertNotIn(FAKE_KEY, repr(settings))

    def test_extra_valid_flags_are_applied(self):
        self.write_config("--auth=" + FAKE_KEY +
                          "\n--host_ip=127.0.0.1\n--host_port=0\n--port=8267"
                          "\n--spiffs\n--progress\n--debug")
        self.assertEqual(self.upload(), 0)
        args = self.sdk.calls[0]
        self.assertEqual(args[1], "127.0.0.1")
        self.assertEqual(args[2], 8267)
        self.assertEqual(args[6], 100)
        self.assertTrue(self.sdk.PROGRESS)

    def test_ini_auth_wins_during_local_header_rotation(self):
        header = Path(self.temporary.name) / "local_secrets.h"
        header.write_text('#define YOKIS_OTA_PASSWORD "future-new-key"\n', encoding="utf-8")
        self.assertEqual(self.upload(), 0)
        self.assertEqual(self.sdk.calls[0][4], FAKE_KEY)

    def test_default_port_is_reserved_once_and_never_released_before_sdk(self):
        self.write_config("--auth=" + FAKE_KEY + "\n--host_ip=127.0.0.1")
        self.assertEqual(self.upload(progress=True), 0)
        self.assertTrue(self.sdk.reserved_before_bind)
        self.assertTrue(self.sdk.reservation_exclusive)
        self.assertGreater(self.sdk.calls[0][3], 0)
        self.assertEqual(len(self.sdk.original_socket.tcp_sockets), 1)
        self.assertEqual(self.sdk.original_socket.udp_count, 1)
        self.assertIs(self.sdk.socket, self.sdk.original_socket)
        self.assertEqual(self.sdk.listener.fileno(), -1)
        self.assertTrue(self.sdk.PROGRESS)

    def test_explicit_occupied_port_reports_errno_and_sends_no_invitation(self):
        occupied = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.addCleanup(occupied.close)
        occupied.bind(("127.0.0.1", 0))
        occupied.listen(1)
        port = occupied.getsockname()[1]
        self.write_config("--auth=" + FAKE_KEY +
                          "\n--host_ip=127.0.0.1\n--host_port=" + str(port))
        with self.assertRaises(ota.UploadError) as raised:
            self.upload()
        self.assertIn(raised.exception.errno, (errno.EADDRINUSE, errno.EACCES))
        self.assertNotIn(FAKE_KEY, str(raised.exception))
        self.assertEqual(self.sdk.calls, [])
        self.assertEqual(self.sdk.original_socket.udp_count, 0)
        self.assertIs(self.sdk.socket, self.sdk.original_socket)

    def test_missing_or_empty_auth_fails_closed(self):
        for flags in ("--host_port=0", "--auth=", "-a ''"):
            with self.subTest(flags=flags):
                self.write_config(flags)
                with self.assertRaises(ota.UploadError):
                    self.upload()
                self.assertEqual(self.sdk.calls, [])

    def test_invalid_private_flags_do_not_echo_values(self):
        for flags in ("--auth='" + FAKE_KEY,
                      "--auth=" + FAKE_KEY + "\n--host_port=" + FAKE_KEY,
                      "--auth=" + FAKE_KEY + "\n--unknown=" + FAKE_KEY,
                      "--auth=" + FAKE_KEY + "\n--auth=other-key"):
            with self.subTest(case=flags.splitlines()[-1].split("=")[0]):
                self.write_config(flags)
                with self.assertRaises(ota.UploadError) as raised:
                    self.upload()
                self.assertNotIn(FAKE_KEY, str(raised.exception))
                self.assertEqual(self.sdk.calls, [])

    def test_percent_characters_in_password_are_not_interpolated(self):
        self.write_config("--auth='test-only-100%-%(missing)s'")
        self.assertEqual(self.upload(), 0)
        self.assertEqual(self.sdk.calls[0][4], "test-only-100%-%(missing)s")

    def test_signed_image_selection_remains_sdk_responsibility(self):
        image = str(Path(self.temporary.name) / "firmware.bin.signed")
        self.assertEqual(ota.run_upload(str(self.config), "d1_mini_ota", "unused-sdk.py",
                                       "192.0.2.5", image, sdk_module=self.sdk), 0)
        self.assertEqual(self.sdk.calls[0][5], image)

    def test_failure_restores_sdk_socket_and_closes_reserved_listener(self):
        self.sdk.raise_after_bind = True
        with self.assertRaises(ota.UploadError) as raised:
            self.upload()
        self.assertNotIn(FAKE_KEY, str(raised.exception))
        self.assertIs(self.sdk.socket, self.sdk.original_socket)
        self.assertEqual(self.sdk.listener.fileno(), -1)

    def test_main_never_passes_auth_in_argv_or_logs_it(self):
        stdout, stderr = io.StringIO(), io.StringIO()
        arguments = self.arguments()
        self.assertNotIn(FAKE_KEY, " ".join(arguments))
        with mock.patch.object(ota, "load_sdk", return_value=self.sdk), \
                contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            result = ota.main(arguments)
        self.assertEqual(result, 0)
        self.assertNotIn(FAKE_KEY, stdout.getvalue() + stderr.getvalue())
        self.assertEqual(self.sdk.calls[0][4], FAKE_KEY)

    def test_main_malformed_secret_argument_is_not_echoed(self):
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            result = ota.main(self.arguments() + ["--auth=" + FAKE_KEY])
        self.assertNotEqual(result, 0)
        self.assertNotIn(FAKE_KEY, stdout.getvalue() + stderr.getvalue())

    def test_main_untrusted_sdk_error_does_not_expose_auth(self):
        self.sdk.raise_after_bind = True
        stdout, stderr = io.StringIO(), io.StringIO()
        with mock.patch.object(ota, "load_sdk", return_value=self.sdk), \
                contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            result = ota.main(self.arguments())
        self.assertNotEqual(result, 0)
        self.assertNotIn(FAKE_KEY, stdout.getvalue() + stderr.getvalue())

    def test_post_hook_uses_installed_sdk_and_replaces_secret_cli_flags(self):
        env = FakeEnv()
        hook.configure_upload(env)
        self.assertEqual(Path(env.updates["UPLOADER"]), ROOT / "tools" / "ota_client.py")
        flags = env.updates["UPLOADERFLAGS"]
        self.assertIn("--espota-script", flags)
        self.assertIn(str(ROOT / "fake-framework" / "tools" / "espota.py"), flags)
        self.assertIn("--config", flags)
        self.assertIn(str(ROOT / "platformio.local.ini"), flags)
        self.assertIn("--environment", flags)
        self.assertIn("d1_mini_ota", flags)
        self.assertIn("--target", flags)
        self.assertIn("$UPLOAD_PORT", flags)
        self.assertIn("--progress", flags)
        self.assertFalse(any(str(flag).startswith(("--auth", "-a")) for flag in flags))
        self.assertNotIn("--debug", flags)

    def test_post_hook_leaves_serial_environment_alone(self):
        env = FakeEnv("d1_mini", "esptool")
        hook.configure_upload(env)
        self.assertEqual(env.updates, {})

    def test_post_hook_only_configures_environment_never_opens_network_or_uploads(self):
        env = FakeEnv()
        with mock.patch.object(socket, "socket", side_effect=AssertionError("Configuration must not open sockets")), \
                mock.patch.object(ota, "load_sdk", side_effect=AssertionError("Configuration must not load uploader SDK")), \
                mock.patch.object(ota, "run_upload", side_effect=AssertionError("Configuration must not upload")):
            hook.configure_upload(env)
        self.assertIn("UPLOADER", env.updates)
        self.assertIn("UPLOADCMD", env.updates)

    def test_post_hook_handles_other_explicit_espota_environment(self):
        env = FakeEnv("other_ota", "espota")
        hook.configure_upload(env)
        self.assertIn("other_ota", env.updates["UPLOADERFLAGS"])


if __name__ == "__main__":
    unittest.main()
