#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
# Build AXIS Micro BBS, the LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM, or both on your own
# Linux PC, and flash them. The installer page fills in this command from your picks:
#
#   curl -fsSL https://revjmoney.github.io/axis-bbs/install.sh | sh -s -- --board no-doors --target cyd --release v0.61 --flash
#
# Options:
#   --board PRESET      a preset from installer/presets.json (everything, no-doors, no-sd, ...)
#   --off a,b           or switch these features off instead (names from installer/features.json)
#   --target cyd|esp32  a CYD, or a bare ESP32 (told to run headless after the flash)
#   --radio v3|v4       build the modem for a Heltec V3 or V4 (alone, or with the board)
#   --flash             flash what was built (otherwise it only builds)
#   --port PORT         the board's serial port, for example /dev/ttyUSB0 (asked when left out)
#   --radio-port PORT   the radio's port when both are flashed (asked when left out)
#   --release TAG       the AXIS Micro BBS release to build (the latest when left out)
#   --source DIR        build from this AXIS Micro BBS source folder instead of the release download
#   --dry-run           print every step, download nothing, build nothing, flash nothing
#
# Tools go into ~/.axis-build (or $AXIS_BUILD_HOME): a Python venv with PlatformIO 6.1.18 and
# esptool 4.9.0, and for the radio rustup with espup. A second run reuses them; a partial download
# is deleted, never trusted. Everything runs inside main, called on the last line, so a cut short
# download of this script runs nothing.

AXIS_REPO="https://github.com/revjmoney/axis-bbs"
AXIS_SITE="https://revjmoney.github.io/axis-bbs/"
LFD_REPO="https://github.com/revjmoney/lfd3k-sm"
PIO_PIN="platformio==6.1.18"
ESPTOOL_PIN="esptool==4.9.0"
MODEM_NAME="LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM"
RADIO_WAIT="Radio images come with the first LFD3K-SM release."

say() { printf '%s\n' "$*"; }
step() { STEP=$((STEP + 1)); say ""; say ">> STEP $STEP: $*"; }
stop() { say "STOPPED: $*" >&2; exit 1; }
dry() { [ "$DRY" = 1 ]; }

# run CMD...: run it (stdin from /dev/null), or print it on a dry run; stop when it fails
run() {
    if dry; then say "DRY RUN, would run: $*"; return 0; fi
    say "> $*"
    "$@" </dev/null || stop "this step failed: $*"
}

# run_tty CMD...: the same, for the steps that ask the person something (they read /dev/tty)
run_tty() {
    if dry; then say "DRY RUN, would run: $*"; return 0; fi
    say "> $*"
    "$@" </dev/null
}

banner() {
    say "=================================================================="
    say " AXIS Micro BBS: build it yourself"
    say " and the $MODEM_NAME"
    say " by Rev. J. Money / jmscnc.com"
    say "=================================================================="
    say ""
    say "  YOU NEED AN SD CARD FOR FULL FUNCTIONS"
    say "  (LORD, scripts, file areas, transfers and nets keep their files on the card."
    say "   For a board without one, build with: --board no-sd)"
    say ""
}

usage() {
    say "Usage: sh install.sh [--board PRESET | --off a,b] [--target cyd|esp32] [--radio v3|v4]"
    say "                     [--flash] [--port PORT] [--radio-port PORT] [--release TAG]"
    say "                     [--source DIR] [--dry-run]"
    say "Linux only. On Windows use install.ps1."
}

need_value() { [ -n "${2:-}" ] || stop "$1 needs a value"; }

parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --board) need_value "$1" "${2:-}"; BOARD="$2"; shift 2 ;;
            --off) need_value "$1" "${2:-}"; OFF_LIST="$2"; shift 2 ;;
            --target) need_value "$1" "${2:-}"; TARGET="$2"; shift 2 ;;
            --radio) need_value "$1" "${2:-}"; RADIO="$2"; shift 2 ;;
            --flash) FLASH=1; shift ;;
            --port) need_value "$1" "${2:-}"; PORT="$2"; shift 2 ;;
            --radio-port) need_value "$1" "${2:-}"; RADIO_PORT="$2"; shift 2 ;;
            --release) need_value "$1" "${2:-}"; RELEASE="$2"; shift 2 ;;
            --source) need_value "$1" "${2:-}"; SOURCE="$2"; shift 2 ;;
            --dry-run) DRY=1; shift ;;
            -h|--help) usage; exit 0 ;;
            *) usage; stop "unknown option: $1" ;;
        esac
    done
}

check_system() {
    case "$(uname -s 2>/dev/null)" in
        Linux) ;;
        Darwin) stop "macOS is not supported. AXIS Micro BBS builds on Windows and Linux only." ;;
        *) stop "This system ($(uname -s 2>/dev/null)) is not supported. Use Linux, or install.ps1 on Windows." ;;
    esac
}

check_picks() {
    [ -n "$BOARD" ] && [ -n "$OFF_LIST" ] && stop "pick a preset with --board or a list with --off, not both"
    if [ -n "$BOARD$OFF_LIST" ] || [ -n "$TARGET" ]; then
        DO_BOARD=1
        [ -n "$BOARD$OFF_LIST" ] || BOARD=everything
        [ -n "$TARGET" ] || TARGET=cyd
    fi
    case "$TARGET" in ""|cyd|esp32) ;; *) stop "--target is cyd or esp32, not $TARGET" ;; esac
    case "$RADIO" in ""|v3|v4) ;; *) stop "--radio is v3 or v4, not $RADIO" ;; esac
    [ -n "$RADIO" ] && DO_RADIO=1
    [ "$DO_BOARD$DO_RADIO" != "00" ] || { usage; stop "nothing to build: pick --board, --off or --radio"; }
    # names reach file paths and URLs: plain characters only (buildopts checks the names themselves)
    printf '%s' "$BOARD$OFF_LIST" | grep -q '[^a-z0-9,-]' && stop "a preset or feature name has characters it cannot have"
    printf '%s' "$RELEASE" | grep -q '[^A-Za-z0-9._-]' && stop "--release is a tag like v0.61"
    return 0
}

# == the tool folder ==

clean_partials() {
    [ -d "$TOOLS" ] || return 0
    for f in $(find "$TOOLS" -maxdepth 3 \( -name '*.part' -o -name '*.tmp' \) 2>/dev/null); do
        if dry; then say "DRY RUN, would delete the partial download $f"
        else say "Deleting the partial download $f"; rm -rf "$f"; fi
    done
}

# fetch URL FILE: download to FILE.part, then move it into place; a failed download is deleted
fetch() {
    if dry; then say "DRY RUN, would download $1"; return 0; fi
    say "Downloading $1"
    rm -f "$2.part"
    if command -v curl >/dev/null 2>&1; then
        curl -fL --retry 3 -o "$2.part" "$1" </dev/null || { rm -f "$2.part"; stop "the download of $1 failed (deleted)"; }
    elif command -v wget >/dev/null 2>&1; then
        wget -q -O "$2.part" "$1" </dev/null || { rm -f "$2.part"; stop "the download of $1 failed (deleted)"; }
    else
        stop "this needs curl or wget to download"
    fi
    mv "$2.part" "$2"
}

# sha256_ok FILE HASH: the file matches the hash the vendor publishes; otherwise it is deleted
sha256_ok() {
    dry && return 0
    have=$(sha256sum "$1" | cut -d ' ' -f 1)
    [ "$have" = "$2" ] || { rm -f "$1"; stop "$1 does not match its published sha256 (deleted)"; }
    say "sha256 checked: $1"
}

find_python() {
    for p in python3 python; do
        if command -v "$p" >/dev/null 2>&1 && "$p" -c 'import sys, venv, ensurepip; sys.exit(sys.version_info < (3, 9))' >/dev/null 2>&1; then
            SYS_PY=$(command -v "$p"); return 0
        fi
    done
    return 1
}

setup_python() {
    step "Python with PlatformIO and esptool in $TOOLS/venv"
    VPY="$TOOLS/venv/bin/python"
    want="$PIO_PIN $ESPTOOL_PIN"
    if [ -f "$TOOLS/venv/.axis-ok" ] && [ "$(cat "$TOOLS/venv/.axis-ok")" = "$want" ] && [ -x "$VPY" ]; then
        say "Reusing $TOOLS/venv ($want)"; return 0
    fi
    if ! find_python; then
        if dry; then say "DRY RUN: no Python 3.9 or newer with venv found; a real run stops here."; return 0; fi
        stop "This needs Python 3.9 or newer with venv. For example: sudo apt install python3 python3-venv"
    fi
    say "Using $SYS_PY"
    if dry; then
        say "DRY RUN, would create the venv $TOOLS/venv and install $want"
        return 0
    fi
    rm -rf "$TOOLS/venv"
    run "$SYS_PY" -m venv "$TOOLS/venv"
    run "$VPY" -m pip install --disable-pip-version-check "$PIO_PIN" "$ESPTOOL_PIN"
    printf '%s' "$want" > "$TOOLS/venv/.axis-ok"
}

# py: the venv's Python, or on a dry run the system one (reading the source's lists only)
py() {
    if [ -x "$VPY" ]; then "$VPY" "$@"; elif [ -n "$SYS_PY" ]; then "$SYS_PY" "$@"; else return 127; fi
}

# get_source NAME URL DIR: unpack a tag's tarball into DIR (reused when complete)
get_source() {
    if [ -f "$3/.axis-ok" ]; then say "Reusing $3"; return 0; fi
    if dry; then fetch "$2" "$3.tar.gz"; say "DRY RUN, would unpack it into $3"; return 0; fi
    mkdir -p "$TOOLS/src"
    rm -rf "$3" "$3.tmp"
    fetch "$2" "$3.tar.gz"
    mkdir -p "$3.tmp"
    tar -xzf "$3.tar.gz" -C "$3.tmp" || { rm -rf "$3.tmp" "$3.tar.gz"; stop "the $1 download is not a complete tarball (deleted)"; }
    top=$(find "$3.tmp" -mindepth 1 -maxdepth 1 -type d | head -n 1)
    [ -n "$top" ] || { rm -rf "$3.tmp"; stop "the $1 tarball is empty"; }
    mv "$top" "$3" && rm -rf "$3.tmp" "$3.tar.gz"
    : > "$3/.axis-ok"
}

latest_release() {
    if dry; then say "DRY RUN, would read the latest release from ${AXIS_SITE}releases/index.json"; RELEASE=LATEST; return 0; fi
    fetch "${AXIS_SITE}releases/index.json" "$TOOLS/index.json"
    RELEASE=$(py -I -c 'import json, sys; sys.stdout.write(json.load(open(sys.argv[1]))["latest"] or "")' "$TOOLS/index.json")
    rm -f "$TOOLS/index.json"
    [ -n "$RELEASE" ] || stop "there is no AXIS Micro BBS release yet"
    printf '%s' "$RELEASE" | grep -q '[^A-Za-z0-9._-]' && stop "the release index names an odd tag"
    say "Latest release: $RELEASE"
}

setup_source() {
    if [ -n "$SOURCE" ]; then
        step "AXIS Micro BBS source: $SOURCE"
        SRC=$(cd "$SOURCE" 2>/dev/null && pwd) || stop "no folder $SOURCE"
        [ -f "$SRC/installer/presets.json" ] || stop "$SRC is not an AXIS Micro BBS source folder"
        return 0
    fi
    [ -n "$RELEASE" ] || latest_release
    step "AXIS Micro BBS source at release $RELEASE"
    SRC="$TOOLS/src/axis-bbs-$RELEASE"
    get_source "AXIS Micro BBS" "$AXIS_REPO/archive/refs/tags/$RELEASE.tar.gz" "$SRC"
}

have_source() { [ -f "$SRC/installer/presets.json" ]; }

check_selection() {
    step "Checking the feature selection"
    if [ -n "$BOARD" ]; then sel="--preset $BOARD"; else sel="--off $OFF_LIST"; fi
    if ! have_source; then
        say "DRY RUN, would check $sel with tools/buildopts.py from the source"
        AXIS_OFF_VALUE="(from tools/buildopts.py)"; return 0
    fi
    # shellcheck disable=SC2086
    AXIS_OFF_VALUE=$(py -I "$SRC/tools/buildopts.py" off $sel) || stop "that selection is refused (the reason is above)"
    AXIS_OFF_VALUE=$(printf '%s' "$AXIS_OFF_VALUE" | tr -d '\r')
    say "AXIS_OFF=$AXIS_OFF_VALUE"
}

check_radio_version() {
    step "Which $MODEM_NAME release goes with it"
    if ! have_source; then
        say "DRY RUN, would read installer/radio.json from the source"; RADIO_VERSION=RADIO; return 0
    fi
    RADIO_VERSION=$(py -I -c 'import json, sys; sys.stdout.write(json.load(open(sys.argv[1]))["version"])' "$SRC/installer/radio.json") \
        || stop "cannot read installer/radio.json"
    [ -n "$RADIO_VERSION" ] || stop "$RADIO_WAIT"
    printf '%s' "$RADIO_VERSION" | grep -q '[^A-Za-z0-9._-]' && stop "installer/radio.json names an odd tag"
    say "LFD3K-SM release $RADIO_VERSION (from installer/radio.json)"
}

# == the jobs ==

# probe: the right chip (and radio model) before a build; sets JOB_PORT and JOB_ENV
probe() { # KIND PORT [MODEL]
    state=$(mktemp 2>/dev/null) || state="$TOOLS/probe-$1.txt"
    pargs="probe --kind $1 --state $state"
    [ -n "$3" ] && pargs="$pargs --radio $3"
    [ -n "$2" ] && pargs="$pargs --port $2"
    dry && pargs="$pargs --dry-run"
    if dry && ! have_source; then
        say "DRY RUN, would run: $VPY -I $SRC/tools/axis_flash.py $pargs"
        JOB_PORT=${2:-PORT}; JOB_ENV=$( [ "$1" = radio ] && echo heltec_v4_single || echo cyd ); return 0
    fi
    # shellcheck disable=SC2086
    py -I "$SRC/tools/axis_flash.py" $pargs </dev/null
    code=$?
    [ "$code" = 0 ] || { [ "$code" = 2 ] && stop "refused before the build (the reason is above)"; stop "could not read the board"; }
    JOB_PORT=$(sed -n 's/^port=//p' "$state")
    JOB_ENV=$(sed -n 's/^env=//p' "$state")
    rm -f "$state"
    return 0
}

flash_images() { # KIND IMAGES PORT [--headless]
    fargs="flash --kind $1 --images $2 --port $3"
    [ -n "${4:-}" ] && fargs="$fargs $4"
    if dry; then say "DRY RUN, would run: $VPY -I $SRC/tools/axis_flash.py $fargs"; return 0; fi
    # shellcheck disable=SC2086
    run_tty "$VPY" -I "$SRC/tools/axis_flash.py" $fargs
    code=$?
    [ "$code" = 2 ] && stop "nothing was written (see above)"
    [ "$code" = 0 ] || stop "the flash did not finish (see above)"
}

board_job() {
    step "AXIS Micro BBS for a $( [ "$TARGET" = esp32 ] && echo 'bare ESP32' || echo CYD )"
    if [ -n "$FLASH" ]; then probe board "$PORT"; fi
    say "Building with AXIS_OFF=$AXIS_OFF_VALUE"
    run env PLATFORMIO_CORE_DIR="$TOOLS/platformio" AXIS_OFF="$AXIS_OFF_VALUE" "$VPY" -m platformio run -d "$SRC" -e cyd
    dry || rm -rf "$TOOLS/out/board"
    run "$VPY" -I "$SRC/tools/axis_flash.py" manifest --kind board --build "$SRC/.pio/build/cyd" --out "$TOOLS/out/board" --release "$RELEASE"
    if [ -n "$FLASH" ]; then
        if [ "$TARGET" = esp32 ]; then flash_images board "$TOOLS/out/board" "$JOB_PORT" --headless
        else flash_images board "$TOOLS/out/board" "$JOB_PORT"; fi
    else
        say "Built. The images are in $TOOLS/out/board. Run again with --flash to flash them."
        dry && say "(On a dry run nothing was built.)"
    fi
}

setup_rust() {
    step "Rust for the ESP32-S3 (rustup and espup) in $TOOLS"
    export RUSTUP_HOME="$TOOLS/rustup" CARGO_HOME="$TOOLS/cargo"
    PATH="$CARGO_HOME/bin:$PATH"; export PATH
    command -v cc >/dev/null 2>&1 || dry || stop "The radio build needs a C compiler and linker. For example: sudo apt install build-essential pkg-config"
    if [ -f "$TOOLS/.rust-ok" ] && [ -x "$CARGO_HOME/bin/espup" ] && [ -f "$TOOLS/export-esp.sh" ]; then
        say "Reusing rustup, espup and the esp toolchain in $TOOLS"; return 0
    fi
    case "$(uname -m)" in
        x86_64) triple=x86_64-unknown-linux-gnu ;;
        aarch64|arm64) triple=aarch64-unknown-linux-gnu ;;
        *) stop "no rustup for this processor ($(uname -m))" ;;
    esac
    url="https://static.rust-lang.org/rustup/dist/$triple/rustup-init"
    if [ ! -x "$CARGO_HOME/bin/rustup" ]; then
        dry || mkdir -p "$TOOLS"
        fetch "$url.sha256" "$TOOLS/rustup-init.sha256"
        fetch "$url" "$TOOLS/rustup-init"
        if ! dry; then
            sha256_ok "$TOOLS/rustup-init" "$(cut -d ' ' -f 1 "$TOOLS/rustup-init.sha256")"
            chmod +x "$TOOLS/rustup-init"
        fi
        run "$TOOLS/rustup-init" -y --no-modify-path --profile minimal --default-toolchain stable
        dry || rm -f "$TOOLS/rustup-init" "$TOOLS/rustup-init.sha256"
    fi
    run "$CARGO_HOME/bin/cargo" install espup --locked
    run "$CARGO_HOME/bin/espup" install --targets esp32s3 --export-file "$TOOLS/export-esp.sh"
    dry || : > "$TOOLS/.rust-ok"
}

build_radio_env() { # ENV
    run env PLATFORMIO_CORE_DIR="$TOOLS/platformio" "$VPY" -m platformio run -d "$LSRC/firmware" -e "$1"
    dry || rm -rf "$TOOLS/out/radio-$1"
    run "$VPY" -I "$SRC/tools/axis_flash.py" manifest --kind radio --env "$1" --build "$LSRC/firmware/.pio/build/$1" --out "$TOOLS/out/radio-$1" --release "$RADIO_VERSION"
}

radio_job() {
    step "The $MODEM_NAME for a Heltec $(printf '%s' "$RADIO" | tr 'v' 'V')"
    rport="$PORT"
    if [ "$DO_BOARD" = 1 ]; then
        rport="$RADIO_PORT"
        if [ -n "$FLASH" ] && ! dry; then
            say "The board is done. Plug in the radio now, then press Enter."
            read -r _ </dev/tty || true
        fi
    fi
    if [ -n "$FLASH" ]; then probe radio "$rport" "$RADIO"; fi
    setup_rust
    step "LFD3K-SM source at $RADIO_VERSION"
    LSRC="$TOOLS/src/lfd3k-sm-$RADIO_VERSION"
    get_source "LFD3K-SM" "$LFD_REPO/archive/refs/tags/$RADIO_VERSION.tar.gz" "$LSRC"
    step "Building the modem's Rust core"
    if dry; then
        say "DRY RUN, would run: . $TOOLS/export-esp.sh; cd $LSRC; RUSTFLAGS='-C panic=abort' cargo +esp build -p lfd3k-ffi --release --target xtensa-esp32s3-none-elf -Zbuild-std=core"
        say "DRY RUN, would copy target/xtensa-esp32s3-none-elf/release/liblfd3k_ffi.a to firmware/lib/"
    else
        ( . "$TOOLS/export-esp.sh" && cd "$LSRC" && RUSTFLAGS='-C panic=abort' cargo +esp build -p lfd3k-ffi --release \
            --target xtensa-esp32s3-none-elf -Zbuild-std=core </dev/null ) || stop "the Rust core did not build"
        mkdir -p "$LSRC/firmware/lib"
        cp "$LSRC/target/xtensa-esp32s3-none-elf/release/liblfd3k_ffi.a" "$LSRC/firmware/lib/liblfd3k_ffi.a" || stop "no Rust core library"
    fi
    step "Building the modem firmware"
    if [ -n "$FLASH" ]; then
        build_radio_env "$JOB_ENV"
        flash_images radio "$TOOLS/out/radio-$JOB_ENV" "$JOB_PORT"
    elif [ "$RADIO" = v3 ]; then
        build_radio_env heltec_v3_single
        say "Built. The images are in $TOOLS/out/radio-heltec_v3_single. Run again with --flash to flash them."
    else
        build_radio_env heltec_v4
        build_radio_env heltec_v4_single
        say "Built. The modem slot for a multimode V4 is in $TOOLS/out/radio-heltec_v4, the modem alone in $TOOLS/out/radio-heltec_v4_single."
        say "Run again with --flash: it reads the V4's table and writes the right one."
    fi
}

main() {
    STEP=0; BOARD=""; OFF_LIST=""; TARGET=""; RADIO=""; FLASH=""; PORT=""; RADIO_PORT=""; RELEASE=""
    SOURCE=""; DRY=0; DO_BOARD=0; DO_RADIO=0; SYS_PY=""; VPY=""; SRC=""; AXIS_OFF_VALUE=""; RADIO_VERSION=""
    banner
    parse_args "$@"
    check_system
    check_picks
    dry && say "DRY RUN: every step is printed; nothing is downloaded, built or flashed."
    TOOLS="${AXIS_BUILD_HOME:-$HOME/.axis-build}"
    step "Tool folder $TOOLS"
    dry || mkdir -p "$TOOLS" || stop "cannot make $TOOLS"
    clean_partials
    setup_python
    dry && [ -z "$SYS_PY" ] && find_python
    setup_source
    [ "$DO_BOARD" = 1 ] && check_selection
    [ "$DO_RADIO" = 1 ] && check_radio_version
    [ "$DO_BOARD" = 1 ] && board_job
    [ "$DO_RADIO" = 1 ] && radio_job
    say ""
    if dry; then say "DRY RUN finished: nothing was downloaded, built or flashed."
    else say "Finished. AXIS Micro BBS by Rev. J. Money / jmscnc.com"; fi
}

main "$@"
