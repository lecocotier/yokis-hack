"""PlatformIO post hook for the private OTA wrapper."""

from pathlib import Path


def configure_upload(env):
    environment = str(env["PIOENV"])
    protocol = str(env.GetProjectOption("upload_protocol", ""))
    if environment != "d1_mini_ota" and protocol != "espota":
        return
    project = Path(env.subst("$PROJECT_DIR"))
    framework = env.PioPlatform().get_package_dir("framework-arduinoespressif8266")
    if not framework:
        raise RuntimeError("Installed ESP8266 framework package is unavailable")
    env.Replace(
        UPLOADER=str(project / "tools" / "ota_client.py"),
        UPLOADERFLAGS=[
            "--espota-script", str(Path(framework) / "tools" / "espota.py"),
            "--config", str(project / "platformio.local.ini"),
            "--environment", environment,
            "--progress", "--target", "$UPLOAD_PORT",
        ],
        UPLOADCMD='"$PYTHONEXE" "$UPLOADER" $UPLOADERFLAGS -f "$SOURCE"',
    )


try:
    _import_environment = Import
except NameError:
    _import_environment = None

if _import_environment is not None:
    _import_environment("env")
    configure_upload(env)
