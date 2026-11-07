# The Arduino-as-IDF component compiles framework-arduinoespressif32/libraries/SD,
# not this repo's lib/SD. Copy the patched sources over before the build.
# SD_MMC is only in the framework. Its totalBytes() check uses the old FatFs
# name _MAX_SS, which is unset, so it always reads FATFS::ssize. That member
# exists only when the max sector size differs from the min. With both at 512
# the build has to multiply by 512.
Import("env")

import os
import shutil

root = env.subst("$PROJECT_DIR")
pkg = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
src = os.path.join(root, "lib", "SD", "src")
dst = os.path.join(pkg, "libraries", "SD", "src")
for name in ("SD.cpp", "sd_diskio.cpp"):
    shutil.copyfile(os.path.join(src, name), os.path.join(dst, name))

mmc = os.path.join(pkg, "libraries", "SD_MMC", "src", "SD_MMC.cpp")
with open(mmc, "r", newline="") as f:
    text = f.read()
old = "#if _MAX_SS != 512"
new = "#if FF_MAX_SS != FF_MIN_SS"
if old in text:
    with open(mmc, "w", newline="\n") as f:
        f.write(text.replace(old, new))
