# PlatformIO post-build script: creates
#   firmware/kuehlschrank-full.bin   (bootloader + partitions + app, flash at 0x0)
#   firmware/kuehlschrank-update.bin (app only, for the web update page)
import os
import shutil

Import("env")  # noqa: F821  (provided by PlatformIO)

APP_BIN = "$BUILD_DIR/${PROGNAME}.bin"


def merge_bin(source, target, env):
    out_dir = os.path.join(env.subst("$PROJECT_DIR"), "firmware")
    os.makedirs(out_dir, exist_ok=True)
    full = os.path.join(out_dir, "kuehlschrank-full.bin")
    update = os.path.join(out_dir, "kuehlschrank-update.bin")

    images = []
    for offset, path in env.get("FLASH_EXTRA_IMAGES", []):
        images += [env.subst(offset), env.subst(path)]
    images += [env.subst("$ESP32_APP_OFFSET"), env.subst(APP_BIN)]

    cmd = [
        env.subst("$PYTHONEXE"),
        env.subst("$OBJCOPY"),  # esptool.py on the espressif32 platform
        "--chip", env.BoardConfig().get("build.mcu", "esp32"),
        "merge_bin",
        "-o", full,
        "--flash_mode", "dio",
        "--flash_size", "4MB",
    ] + images
    env.Execute(" ".join('"%s"' % c for c in cmd))
    shutil.copyfile(env.subst(APP_BIN), update)
    print("Komplett-Image: %s" % full)
    print("Update-Image:   %s" % update)


# Only the real firmware goes to firmware/, not the Wokwi simulation build.
if env.subst("$PIOENV") == "esp32":  # noqa: F821
    env.AddPostAction(APP_BIN, merge_bin)  # noqa: F821
