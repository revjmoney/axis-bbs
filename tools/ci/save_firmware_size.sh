#!/bin/sh
# Record section sizes for one PlatformIO env. Used by CI; safe to run locally after pio run.
set -eu
envname=${1:?env name}
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
elf="$root/.pio/build/$envname/firmware.elf"
mkdir -p "$root/build-size"
out="$root/build-size/firmware-size-$envname.txt"
sizebin="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-size"
if [ ! -f "$elf" ]; then
    echo "missing $elf" >&2
    exit 1
fi
if [ ! -x "$sizebin" ]; then
    echo "missing $sizebin" >&2
    exit 1
fi
{
    echo "env=$envname"
    "$sizebin" -A "$elf" | awk '
        $1 == ".iram0.vectors" || $1 == ".iram0.text" || $1 == ".dram0.data" || $1 == ".dram0.bss" ||
        $1 == ".flash.text" || $1 == ".flash.rodata" || $1 == ".flash.appdesc" || $1 ~ /^\.phyiram[.][0-9]+$/ {
            printf "%s %s\n", $1, $2
            if ($1 == ".iram0.vectors" || $1 == ".iram0.text" || $1 ~ /^\.phyiram[.][0-9]+$/) iram += $2
            if ($1 == ".dram0.data" || $1 == ".dram0.bss") dram += $2
            if ($1 == ".flash.text" || $1 == ".flash.rodata" || $1 == ".flash.appdesc") flash += $2
        }
        END {
            printf "linked_iram_bytes %d\n", iram
            printf "linked_dram_bytes %d\n", dram
            printf "linked_flash_text_rodata_bytes %d\n", flash
        }
    '
} | tee "$out"
# The map stays in .pio/build/$envname/firmware.map. CI uploads it from there.
