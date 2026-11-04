"""Private-config ESP8266 OTA wrapper.

Public test seams: load_settings(path, environment), load_sdk(path),
run_upload(..., sdk_module=None, socket_module=None), and main(argv=None).
No authentication value belongs in the wrapper's command line.
"""

import argparse
import configparser
import contextlib
import errno
import importlib.util
import logging
import shlex
import sys


class UploadError(Exception):
    """An error safe to report without exposing private configuration."""

    def __init__(self, message, error_number=None):
        self.errno = error_number
        if error_number is not None:
            name = errno.errorcode.get(error_number, "SOCKET_ERROR")
            message += " (errno {}: {})".format(error_number, name)
        super().__init__(message)


class UploadSettings:
    def __init__(self, options):
        self.password = options.password
        self.host_ip = options.host_ip
        self.host_port = options.host_port
        self.remote_port = options.remote_port
        self.command = 100 if options.spiffs else 0
        self.progress = options.progress
        self.verbose = options.verbose

    def __repr__(self):
        return "UploadSettings(<private configuration>)"


class _PrivateParser(argparse.ArgumentParser):
    def error(self, _message):
        # argparse's default errors include invalid argument values.
        raise UploadError("Invalid OTA arguments; private values were omitted")


class _AuthenticationOption(argparse.Action):
    def __call__(self, _parser, namespace, values, _option_string=None):
        if namespace.password is not None:
            raise UploadError("Duplicate OTA authentication option")
        namespace.password = values


def load_settings(config_path, environment):
    configuration = configparser.ConfigParser(interpolation=None)
    try:
        with open(config_path, "r", encoding="utf-8") as source:
            configuration.read_file(source)
        section = "env:" + environment
        if not configuration.has_section(section):
            raise UploadError("OTA environment is missing from private configuration")
        # PlatformIO prints every ENV option in verbose mode before hooks run.
        # A separate non-inherited section keeps authentication out of that dump.
        private_section = "ota_private:" + environment
        if configuration.has_section(private_section):
            section = private_section
        flags = configuration.get(section, "upload_flags", fallback="")
        tokens = shlex.split(flags, posix=True)
    except UploadError:
        raise
    except Exception:
        # ConfigParser exceptions may contain whole private source lines.
        raise UploadError("Cannot read private OTA configuration") from None

    parser = _PrivateParser(add_help=False, allow_abbrev=False)
    parser.add_argument("-a", "--auth", dest="password", default=None,
                        action=_AuthenticationOption)
    parser.add_argument("-I", "--host_ip", default="0.0.0.0")
    parser.add_argument("-P", "--host_port", type=int, default=0)
    parser.add_argument("-p", "--port", dest="remote_port", type=int, default=8266)
    parser.add_argument("-s", "--spiffs", action="store_true")
    parser.add_argument("-r", "--progress", action="store_true")
    parser.add_argument("-d", "--debug", dest="verbose", action="store_true")
    options = parser.parse_args(tokens)
    if not options.password:
        raise UploadError("Private OTA authentication is required")
    if not 0 <= options.host_port <= 65535 or not 1 <= options.remote_port <= 65535:
        raise UploadError("Invalid OTA port in private configuration")
    if not options.host_ip or any(character in options.host_ip for character in "\x00\r\n"):
        raise UploadError("Invalid OTA bind address in private configuration")
    return UploadSettings(options)


def load_sdk(script_path):
    try:
        specification = importlib.util.spec_from_file_location("_yokis_installed_espota", script_path)
        if specification is None or specification.loader is None:
            raise UploadError("Cannot load installed ESP8266 OTA SDK")
        module = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(module)
        return module
    except UploadError:
        raise
    except Exception:
        raise UploadError("Cannot load installed ESP8266 OTA SDK") from None


class _PreparedListener:
    def __init__(self, listener, expected_address):
        self._listener = listener
        self._expected_address = expected_address

    def bind(self, address):
        if tuple(address) != self._expected_address:
            raise UploadError("SDK requested an unexpected listener address", errno.EINVAL)
        # The exact same socket is already bound and listening. Never release it.

    def __getattr__(self, name):
        return getattr(self._listener, name)


class _SocketAdapter:
    def __init__(self, original, listener, address):
        self._original = original
        self._listener = _PreparedListener(listener, address)
        self._claimed = False

    def socket(self, *arguments, **keywords):
        family = arguments[0] if arguments else keywords.get("family", self.AF_INET)
        kind = arguments[1] if len(arguments) > 1 else keywords.get("type", self.SOCK_STREAM)
        descriptor = arguments[3] if len(arguments) > 3 else keywords.get("fileno")
        if (not self._claimed and descriptor is None and
                family == self.AF_INET and kind == self.SOCK_STREAM):
            self._claimed = True
            return self._listener
        return self._original.socket(*arguments, **keywords)

    def __getattr__(self, name):
        return getattr(self._original, name)


def _reserve_listener(sockets, host_ip, host_port):
    listener = None
    try:
        listener = sockets.socket(sockets.AF_INET, sockets.SOCK_STREAM)
        # No SO_REUSEADDR: a reserved explicit port must not silently be shared.
        listener.bind((host_ip, host_port))
        listener.listen(1)
        selected_port = listener.getsockname()[1]
        return listener, selected_port
    except OSError as failure:
        if listener is not None:
            listener.close()
        raise UploadError("Cannot reserve OTA callback listener", failure.errno) from None
    except Exception:
        if listener is not None:
            listener.close()
        raise UploadError("Cannot reserve OTA callback listener") from None


class _SecretLogFilter(logging.Filter):
    def __init__(self, password):
        super().__init__()
        self._password = password

    def filter(self, record):
        record.msg = record.getMessage().replace(self._password, "[redacted]")
        record.args = ()
        # An SDK traceback could include a private exception message.
        record.exc_info = None
        record.exc_text = None
        return True


@contextlib.contextmanager
def _private_sdk_logging(password, verbose):
    logger = logging.getLogger()
    previous_level = logger.level
    secret_filter = _SecretLogFilter(password)
    handlers = list(logger.handlers)
    logger.addFilter(secret_filter)
    for handler in handlers:
        handler.addFilter(secret_filter)
    if verbose:
        logger.setLevel(logging.DEBUG)
    try:
        yield
    finally:
        logger.setLevel(previous_level)
        logger.removeFilter(secret_filter)
        for handler in handlers:
            handler.removeFilter(secret_filter)


def run_upload(config_path, environment, espota_script, remote_addr, filename,
               progress=False, verbose=False, sdk_module=None, socket_module=None):
    settings = load_settings(config_path, environment)
    sdk = sdk_module if sdk_module is not None else load_sdk(espota_script)
    try:
        original_socket = sdk.socket
        sockets = socket_module if socket_module is not None else original_socket
    except Exception:
        raise UploadError("Installed OTA SDK has no socket interface") from None
    listener, selected_port = _reserve_listener(sockets, settings.host_ip, settings.host_port)
    try:
        sdk.socket = _SocketAdapter(sockets, listener, (settings.host_ip, selected_port))
        sdk.PROGRESS = bool(progress or settings.progress)
        with _private_sdk_logging(settings.password, verbose or settings.verbose):
            # Calling main() would log its options, including the password.
            result = sdk.serve(remote_addr, settings.host_ip, settings.remote_port,
                               selected_port, settings.password, filename, settings.command)
        if not isinstance(result, int):
            raise UploadError("Installed OTA SDK returned an invalid transfer result")
        return result
    except UploadError:
        raise
    except Exception:
        raise UploadError("OTA SDK transfer failed; private details were omitted") from None
    finally:
        sdk.socket = original_socket
        listener.close()


def main(argv=None):
    parser = _PrivateParser(description="Upload through the installed ESP8266 SDK using private INI authentication",
                            allow_abbrev=False)
    parser.add_argument("--espota-script", required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--environment", required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("-f", "--file", dest="filename", required=True)
    parser.add_argument("--progress", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    try:
        options = parser.parse_args(argv)
        result = run_upload(options.config, options.environment, options.espota_script,
                            options.target, options.filename, options.progress, options.verbose)
        return 0 if result == 0 else 1
    except UploadError as failure:
        print("OTA upload failed: " + str(failure), file=sys.stderr)
        return 1
    except Exception:
        print("OTA upload failed; private details were omitted", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
