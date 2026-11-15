# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
# Build AXIS Micro BBS, the LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM, or both on your own
# Windows PC (Windows PowerShell 5.1), and flash them. The installer page fills in this command:
#
#   & ([scriptblock]::Create((irm https://revjmoney.github.io/axis-bbs/install.ps1))) -Board no-doors -Target cyd -Release v0.61 -Flash
#
# Parameters:
#   -Board PRESET      a preset from installer/presets.json (everything, no-doors, no-sd, ...)
#   -Off a,b           or switch these features off instead (names from installer/features.json)
#   -Target cyd|esp32  a CYD, or a bare ESP32 (told to run headless after the flash)
#   -Radio v3|v4       build the modem for a Heltec V3 or V4 (alone, or with the board)
#   -Flash             flash what was built (otherwise it only builds)
#   -Port COMx         the board's serial port (asked when left out)
#   -RadioPort COMx    the radio's port when both are flashed (asked when left out)
#   -Release TAG       the AXIS Micro BBS release to build (the latest when left out)
#   -Source DIR        build from this AXIS Micro BBS source folder instead of the release download
#   -DryRun            print every step, download nothing, build nothing, flash nothing
#
# Tools go into %LOCALAPPDATA%\axis-build (or $env:AXIS_BUILD_HOME): Python (yours in a venv, or a
# private copy from nuget.org), PlatformIO 6.1.18 and esptool 4.9.0, and for the radio rustup with
# espup. A second run reuses them; a partial download is deleted, never trusted. This script never
# calls exit when it runs from irm, so it cannot close your window.
param(
    [string]$Board = "",
    [string[]]$Off = @(),
    [string]$Target = "",
    [string]$Radio = "",
    [switch]$Flash,
    [string]$Port = "",
    [string]$RadioPort = "",
    [string]$Release = "",
    [string]$Source = "",
    [switch]$DryRun,
    [switch]$Help
)

$AxisRepo = "https://github.com/revjmoney/axis-bbs"
$AxisSite = "https://revjmoney.github.io/axis-bbs/"
$LfdRepo = "https://github.com/revjmoney/lfd3k-sm"
$PioPin = "platformio==6.1.18"
$EsptoolPin = "esptool==4.9.0"
$NugetPython = "3.12.10"
$ModemName = "LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM"
$RadioWait = "Radio images come with the first LFD3K-SM release."
# Tool versions: one place (install.sh holds the same values)
$PyVersions = @("3.12", "3.13")
$EspupVersion = "0.19.0"
$RustVersion = "1.100.0"
$EspToolchainVersion = "1.97.0.0"

$script:StepNo = 0
$script:Py = ""
$script:SysPy = ""
$script:SysPyVersion = ""
$script:Src = ""
$script:Tools = ""
$script:AxisOffValue = ""
$script:RadioVersion = ""
$script:JobPort = ""
$script:JobEnv = ""

function Say([string]$Text) { Write-Host $Text }
function Step([string]$Text) { $script:StepNo++; Write-Host ""; Write-Host (">> STEP " + $script:StepNo + ": " + $Text) }
function Stop-Axis([string]$Text) { throw (New-Object System.InvalidOperationException ("AXIS:" + $Text)) }
function Test-Dry { return [bool]$DryRun }

function Show-Banner {
    Say "=================================================================="
    Say " AXIS Micro BBS: build it yourself"
    Say " and the $ModemName"
    Say " by Rev. J. Money / jmscnc.com"
    Say "=================================================================="
    Say ""
    Say "  YOU NEED AN SD CARD FOR FULL FUNCTIONS"
    Say "  (LORD, scripts, file areas, transfers and nets keep their files on the card."
    Say "   For a board without one, build with: -Board no-sd)"
    Say ""
}

function Show-Usage {
    Say "Usage: install.ps1 [-Board PRESET | -Off a,b] [-Target cyd|esp32] [-Radio v3|v4] [-Flash]"
    Say "                   [-Port COMx] [-RadioPort COMx] [-Release TAG] [-Source DIR] [-DryRun]"
    Say "Windows only. On Linux use install.sh."
}

function Format-Command([string]$Exe, [string[]]$ArgList) {
    $parts = @($Exe) + $ArgList | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }
    return ($parts -join " ")
}

# Run a program (its output shown as it comes), or print it on a dry run; stop when it fails.
function Invoke-Native([string]$Exe, [string[]]$ArgList, [hashtable]$Environment = @{}, [switch]$AllowRefusal) {
    $shown = Format-Command $Exe $ArgList
    foreach ($k in $Environment.Keys) { $shown = "$k=" + $Environment[$k] + " " + $shown }
    if (Test-Dry) { Say ("DRY RUN, would run: " + $shown); return 0 }
    Say ("> " + $shown)
    $saved = @{}
    foreach ($k in $Environment.Keys) {
        $saved[$k] = [Environment]::GetEnvironmentVariable($k, "Process")
        [Environment]::SetEnvironmentVariable($k, $Environment[$k], "Process")
    }
    $ErrorActionPreference = "Continue"
    try {
        & $Exe @ArgList | Out-Host
        $code = $LASTEXITCODE
    } finally {
        foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k], "Process") }
    }
    if ($AllowRefusal -and $code -eq 2) { return 2 }
    if ($code -ne 0) { Stop-Axis ("this step failed (exit " + $code + "): " + $shown) }
    return 0
}

# Run a program and return its output as one string (stderr kept, so a refusal shows its reason).
function Get-Output([string]$Exe, [string[]]$ArgList) {
    $ErrorActionPreference = "Continue"
    $text = (& $Exe @ArgList 2>&1 | ForEach-Object { "$_" }) -join "`n"
    return @{ Code = $LASTEXITCODE; Text = $text.Trim() }
}

function Read-Args {
    if ($Board -and $Off.Count) { Stop-Axis "pick a preset with -Board or a list with -Off, not both" }
    # -Off a,b arrives as an array from irm and as one string from -File: both become "a,b"
    $script:OffList = (($Off -join ",") -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_ }) -join ","
    $script:DoBoard = [bool]($Board -or $script:OffList -or $Target)
    $script:DoRadio = [bool]$Radio
    if ($script:DoBoard) {
        if (-not ($Board -or $script:OffList)) { $script:BoardPick = "everything" } else { $script:BoardPick = $Board }
        if (-not $Target) { $script:TargetPick = "cyd" } else { $script:TargetPick = $Target }
    }
    if ($Target -and @("cyd", "esp32") -notcontains $Target) { Stop-Axis ("-Target is cyd or esp32, not " + $Target) }
    if ($Radio -and @("v3", "v4") -notcontains $Radio) { Stop-Axis ("-Radio is v3 or v4, not " + $Radio) }
    if (-not ($script:DoBoard -or $script:DoRadio)) { Show-Usage; Stop-Axis "nothing to build: pick -Board, -Off or -Radio" }
    if (($script:BoardPick + $script:OffList) -match '[^a-z0-9,-]') { Stop-Axis "a preset or feature name has characters it cannot have" }
    if ($Release -match '[^A-Za-z0-9._-]') { Stop-Axis "-Release is a tag like v0.61" }
}

function Test-System {
    if ((Test-Path variable:IsMacOS) -and $IsMacOS) { Stop-Axis "macOS is not supported. AXIS Micro BBS builds on Windows and Linux only." }
    if ((Test-Path variable:IsLinux) -and $IsLinux) { Stop-Axis "On Linux, use install.sh." }
    if ($PSVersionTable.PSVersion.Major -lt 5) { Stop-Axis "This needs Windows PowerShell 5.1 or newer." }
}

# == the tool folder ==

function Remove-Partials {
    if (-not (Test-Path $script:Tools)) { return }
    Get-ChildItem -Path $script:Tools -Recurse -Depth 2 -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "*.part" -or $_.Name -like "*.tmp" } | ForEach-Object {
            if (Test-Dry) { Say ("DRY RUN, would delete the partial download " + $_.FullName) }
            else { Say ("Deleting the partial download " + $_.FullName); Remove-Item -Recurse -Force $_.FullName }
        }
}

# Download to FILE.part, then move it into place; a failed or wrong download is deleted.
function Get-Download([string]$Url, [string]$Dest, [long]$Size = -1) {
    if (Test-Dry) { Say ("DRY RUN, would download " + $Url); return }
    Say ("Downloading " + $Url)
    $part = $Dest + ".part"
    if (Test-Path $part) { Remove-Item -Force $part }
    try {
        Invoke-WebRequest -Uri $Url -OutFile $part -UseBasicParsing
    } catch {
        if (Test-Path $part) { Remove-Item -Force $part }
        Stop-Axis ("the download of " + $Url + " failed (deleted): " + $_.Exception.Message)
    }
    if ($Size -ge 0 -and (Get-Item $part).Length -ne $Size) {
        Remove-Item -Force $part
        Stop-Axis ("the download of " + $Url + " is not the size its publisher gives (deleted)")
    }
    Move-Item -Force $part $Dest
}

function Test-Hash([string]$File, [string]$Algorithm, [string]$Want) {
    if (Test-Dry) { return }
    $have = (Get-FileHash -Algorithm $Algorithm -Path $File).Hash.ToLower()
    if ($have -ne $Want.ToLower()) { Remove-Item -Force $File; Stop-Axis ($File + " does not match its published " + $Algorithm + " (deleted)") }
    Say ($Algorithm + " checked: " + $File)
}

# Python 3.12, else 3.13 (never a newer one: CI and the releases build with 3.12).
function Find-Python {
    foreach ($v in $PyVersions) {
        foreach ($cand in @(@("py", ("-" + $v)), @("python"))) {
            $exe = $cand[0]
            $cmd = Get-Command $exe -ErrorAction SilentlyContinue
            if (-not $cmd) { continue }
            if ($cmd.Source -like "*\WindowsApps\*") { continue }  # the Microsoft Store placeholder
            $check = "import sys, venv, ensurepip; print(sys.executable) if '%d.%d' % sys.version_info[:2] == '" + $v + "' else sys.exit(1)"
            $probe = @($cand | Select-Object -Skip 1) + @("-c", $check)
            $r = Get-Output $exe $probe
            if ($r.Code -eq 0 -and $r.Text -and (Test-Path $r.Text)) { $script:SysPy = $r.Text; $script:SysPyVersion = $v; return $true }
        }
    }
    return $false
}

# A private Python from nuget.org (the Python Software Foundation's package), checked against the
# size and SHA512 that nuget.org publishes for it.
function Get-NugetPython {
    $dir = Join-Path $script:Tools "python"
    $exe = Join-Path $dir "tools\python.exe"
    if ((Test-Path (Join-Path $dir ".axis-ok")) -and (Test-Path $exe)) { Say ("Reusing " + $dir); return $exe }
    $leaf = "https://api.nuget.org/v3/registration5-semver1/python/" + $NugetPython + ".json"
    if (Test-Dry) {
        Say ("DRY RUN, would read the size and SHA512 of Python " + $NugetPython + " from " + $leaf)
        Say ("DRY RUN, would download https://api.nuget.org/v3-flatcontainer/python/" + $NugetPython + "/python." + $NugetPython + ".nupkg and unpack it into " + $dir)
        return $exe
    }
    $reg = Invoke-RestMethod -Uri $leaf -UseBasicParsing
    $cat = Invoke-RestMethod -Uri $reg.catalogEntry -UseBasicParsing
    if ($cat.packageHashAlgorithm -ne "SHA512") { Stop-Axis "nuget.org gives no SHA512 for Python" }
    $want = ([BitConverter]::ToString([Convert]::FromBase64String($cat.packageHash)) -replace "-", "").ToLower()
    $pkg = Join-Path $script:Tools ("python." + $NugetPython + ".zip")
    Get-Download $reg.packageContent $pkg ([long]$cat.packageSize)
    Test-Hash $pkg "SHA512" $want
    $tmp = $dir + ".tmp"
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    Expand-Archive -Path $pkg -DestinationPath $tmp
    Remove-Item -Force $pkg
    Move-Item $tmp $dir
    Set-Content -Path (Join-Path $dir ".axis-ok") -Value $NugetPython -Encoding ASCII
    return $exe
}

function Initialize-Python {
    Step ("Python with PlatformIO and esptool in " + $script:Tools)
    $venv = Join-Path $script:Tools "venv"
    $marker = Join-Path $venv ".axis-ok"
    $want = "$PioPin $EsptoolPin"
    $script:Py = Join-Path $venv "Scripts\python.exe"
    if ((Test-Path $marker) -and ((Get-Content -Raw $marker).Trim() -eq $want) -and (Test-Path $script:Py)) {
        Say ("Reusing " + $venv + " (" + $want + ")"); return
    }
    if (Find-Python) {
        Say ("Using Python " + $script:SysPyVersion + ": " + $script:SysPy)
        if (Test-Dry) { Say ("DRY RUN, would create the venv " + $venv + " and install " + $want); return }
        if (Test-Path $venv) { Remove-Item -Recurse -Force $venv }
        Invoke-Native $script:SysPy @("-m", "venv", $venv) | Out-Null
    } else {
        Say ("No Python 3.12 or 3.13 here: using Python " + $NugetPython + " from nuget.org, a private copy in the tool folder.")
        $private = Get-NugetPython
        if (Test-Dry) { Say ("DRY RUN, would create the venv " + $venv + " with it and install " + $want); return }
        if (Test-Path $venv) { Remove-Item -Recurse -Force $venv }
        Invoke-Native $private @("-m", "venv", $venv) | Out-Null
    }
    Invoke-Native $script:Py @("-m", "pip", "install", "--disable-pip-version-check", $PioPin, $EsptoolPin) | Out-Null
    Set-Content -Path $marker -Value $want -Encoding ASCII
}

# The venv's Python, or on a dry run the system one (reading the source's lists only).
function Get-Py {
    if ($script:Py -and (Test-Path $script:Py)) { return $script:Py }
    if ($script:SysPy) { return $script:SysPy }
    return $null
}

function Get-Source([string]$Name, [string]$Url, [string]$Dir) {
    if (Test-Path (Join-Path $Dir ".axis-ok")) { Say ("Reusing " + $Dir); return }
    $tgz = $Dir + ".tar.gz"
    if (Test-Dry) { Get-Download $Url $tgz; Say ("DRY RUN, would unpack it into " + $Dir); return }
    $tar = Join-Path $env:SystemRoot "System32\tar.exe"
    if (-not (Test-Path $tar)) { Stop-Axis "This needs tar.exe (Windows 10 1803 or newer)." }
    New-Item -ItemType Directory -Force (Split-Path -Parent $Dir) | Out-Null
    foreach ($p in @($Dir, ($Dir + ".tmp"))) { if (Test-Path $p) { Remove-Item -Recurse -Force $p } }
    Get-Download $Url $tgz
    New-Item -ItemType Directory -Force ($Dir + ".tmp") | Out-Null
    $r = Get-Output $tar @("-xzf", $tgz, "-C", ($Dir + ".tmp"))
    if ($r.Code -ne 0) {
        Remove-Item -Recurse -Force ($Dir + ".tmp"), $tgz
        Stop-Axis ("the " + $Name + " download is not a complete tarball (deleted)")
    }
    $top = Get-ChildItem -Directory ($Dir + ".tmp") | Select-Object -First 1
    if (-not $top) { Remove-Item -Recurse -Force ($Dir + ".tmp"); Stop-Axis ("the " + $Name + " tarball is empty") }
    Move-Item $top.FullName $Dir
    Remove-Item -Recurse -Force ($Dir + ".tmp"), $tgz
    Set-Content -Path (Join-Path $Dir ".axis-ok") -Value "ok" -Encoding ASCII
}

function Initialize-Source {
    if ($Source) {
        Step ("AXIS Micro BBS source: " + $Source)
        if (-not (Test-Path (Join-Path $Source "installer\presets.json"))) { Stop-Axis ($Source + " is not an AXIS Micro BBS source folder") }
        $script:Src = (Resolve-Path $Source).Path
        Test-SourceFiles
        return
    }
    $rel = $Release
    if (-not $rel) {
        if (Test-Dry) { Say ("DRY RUN, would read the latest release from " + $AxisSite + "releases/index.json"); $rel = "LATEST" }
        else {
            $idx = Invoke-RestMethod -Uri ($AxisSite + "releases/index.json") -UseBasicParsing
            $rel = [string]$idx.latest
            if (-not $rel) { Stop-Axis "there is no AXIS Micro BBS release yet" }
            if ($rel -match '[^A-Za-z0-9._-]') { Stop-Axis "the release index names an odd tag" }
            Say ("Latest release: " + $rel)
        }
    }
    $script:ReleasePick = $rel
    Step ("AXIS Micro BBS source at release " + $rel)
    $script:Src = Join-Path $script:Tools ("src\axis-bbs-" + $rel)
    Get-Source "AXIS Micro BBS" ($AxisRepo + "/archive/refs/tags/" + $rel + ".tar.gz") $script:Src
    if (-not (Test-Dry)) { Test-SourceFiles }
}

# A release older than these scripts has no lists or flasher to build and flash with.
function Test-SourceFiles {
    foreach ($f in @("installer\presets.json", "installer\features.json", "installer\radio.json", "tools\buildopts.py", "tools\axis_flash.py")) {
        if (-not (Test-Path (Join-Path $script:Src $f))) {
            $rel = $script:ReleasePick
            if (-not $rel) { $rel = "here" }
            Stop-Axis ("This AXIS Micro BBS source has no " + $f + ": release " + $rel + " predates the build-it-yourself scripts. Pick a newer release.")
        }
    }
}

# The source is here (always on a real run; a dry run without a source only prints).
function Test-HaveSource { return [bool]($script:Src -and (Test-Path (Join-Path $script:Src "tools\axis_flash.py"))) }

function Test-Selection {
    Step "Checking the feature selection"
    if ($script:BoardPick) { $sel = @("--preset", $script:BoardPick) } else { $sel = @("--off", $script:OffList) }
    if ((Test-Dry) -and -not (Test-HaveSource)) {
        Say ("DRY RUN, would check " + ($sel -join " ") + " with tools/buildopts.py from the source")
        $script:AxisOffValue = "(from tools/buildopts.py)"; return
    }
    $r = Get-Output (Get-Py) (@("-I", (Join-Path $script:Src "tools\buildopts.py"), "off") + $sel)
    if ($r.Code -ne 0) { Say $r.Text; Stop-Axis "that selection is refused (the reason is above)" }
    $script:AxisOffValue = $r.Text
    Say ("AXIS_OFF=" + $script:AxisOffValue)
}

function Test-RadioVersion {
    Step ("Which " + $ModemName + " release goes with it")
    if ((Test-Dry) -and -not (Test-HaveSource)) { Say "DRY RUN, would read installer/radio.json from the source"; $script:RadioVersion = "RADIO"; return }
    $j = Get-Content -Raw (Join-Path $script:Src "installer\radio.json") | ConvertFrom-Json
    $script:RadioVersion = [string]$j.version
    if (-not $script:RadioVersion) { Stop-Axis $RadioWait }
    if ($script:RadioVersion -match '[^A-Za-z0-9._-]') { Stop-Axis "installer/radio.json names an odd tag" }
    Say ($ModemName + " release " + $script:RadioVersion + " (from installer/radio.json)")
}

# == the jobs ==

function Get-FlashTool { return (Join-Path $script:Src "tools\axis_flash.py") }

# The right chip (and radio model) before a build; sets JobPort and JobEnv.
function Invoke-Probe([string]$Kind, [string]$PortName, [string]$Model) {
    $state = Join-Path ([System.IO.Path]::GetTempPath()) ("axis-probe-" + [guid]::NewGuid().ToString("N") + ".txt")
    $a = @("-I", (Get-FlashTool), "probe", "--kind", $Kind, "--state", $state)
    if ($Model) { $a += @("--radio", $Model) }
    if ($PortName) { $a += @("--port", $PortName) }
    if (Test-Dry) {
        $a += "--dry-run"
        if (-not (Test-HaveSource)) {
            Say ("DRY RUN, would run: " + (Format-Command (Join-Path $script:Tools "venv\Scripts\python.exe") $a))
            if ($PortName) { $script:JobPort = $PortName } else { $script:JobPort = "PORT" }
            if ($Kind -eq "radio") { $script:JobEnv = "heltec_v4_single" } else { $script:JobEnv = "cyd" }
            return
        }
        $exe = Get-Py
        $ErrorActionPreference = "Continue"
        & $exe @a | Out-Host
        $code = $LASTEXITCODE
    } else {
        $ErrorActionPreference = "Continue"
        & $script:Py @a | Out-Host
        $code = $LASTEXITCODE
    }
    $lines = @()
    if (Test-Path $state) { $lines = Get-Content $state; Remove-Item -Force $state }
    if ($code -eq 2) { Stop-Axis "refused before the build (the reason is above)" }
    if ($code -ne 0) { Stop-Axis "could not read the board" }
    foreach ($l in $lines) {
        if ($l -like "port=*") { $script:JobPort = $l.Substring(5) }
        if ($l -like "env=*") { $script:JobEnv = $l.Substring(4) }
    }
}

function Invoke-Flash([string]$Kind, [string]$Images, [string]$PortName, [switch]$Headless) {
    $a = @("-I", (Get-FlashTool), "flash", "--kind", $Kind, "--images", $Images, "--port", $PortName)
    if ($Headless) { $a += "--headless" }
    $code = Invoke-Native $script:Py $a -AllowRefusal
    if ($code -eq 2) { Stop-Axis "nothing was written (see above)" }
}

# PowerShell 5.1 drops an empty string argument, so the release is passed only when there is one.
function Get-ReleaseArg {
    if ($script:ReleasePick) { return @("--release", [string]$script:ReleasePick) }
    return @()
}

function Invoke-BoardJob {
    if ($script:TargetPick -eq "esp32") { Step "AXIS Micro BBS for a bare ESP32" } else { Step "AXIS Micro BBS for a CYD" }
    if ($Flash) { Invoke-Probe "board" $Port "" }
    Say ("Building with AXIS_OFF=" + $script:AxisOffValue)
    Invoke-Native $script:Py @("-m", "platformio", "run", "-d", $script:Src, "-e", "cyd") @{
        PLATFORMIO_CORE_DIR = (Join-Path $script:Tools "platformio"); AXIS_OFF = $script:AxisOffValue } | Out-Null
    $out = Join-Path $script:Tools "out\board"
    if (-not (Test-Dry) -and (Test-Path $out)) { Remove-Item -Recurse -Force $out }
    Invoke-Native $script:Py (@("-I", (Get-FlashTool), "manifest", "--kind", "board", "--build",
        (Join-Path $script:Src ".pio\build\cyd"), "--out", $out) + @(Get-ReleaseArg)) | Out-Null
    if ($Flash) {
        Invoke-Flash "board" $out $script:JobPort -Headless:($script:TargetPick -eq "esp32")
    } else {
        Say ("Built. The images are in " + $out + ". Run again with -Flash to flash them.")
        if (Test-Dry) { Say "(On a dry run nothing was built.)" }
    }
}

function Initialize-Rust {
    Step ("Rust for the ESP32-S3 (rustup and espup) in " + $script:Tools)
    $env:RUSTUP_HOME = Join-Path $script:Tools "rustup"
    $env:CARGO_HOME = Join-Path $script:Tools "cargo"
    $bin = Join-Path $env:CARGO_HOME "bin"
    if (($env:PATH -split ";") -notcontains $bin) { $env:PATH = $bin + ";" + $env:PATH }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $vc = $null
    if (Test-Path $vswhere) {
        $vc = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if (-not $vc -and -not (Test-Dry)) {
        Stop-Axis "The radio build needs the Visual Studio Build Tools with the C++ workload (the linker Rust uses on Windows). Install them, then run this again."
    }
    $export = Join-Path $script:Tools "export-esp.ps1"
    $rustPins = "rust $RustVersion espup $EspupVersion esp $EspToolchainVersion"
    $rustOk = Join-Path $script:Tools ".rust-ok"
    if ((Test-Path $rustOk) -and ((Get-Content -Raw $rustOk).Trim() -eq $rustPins) -and (Test-Path (Join-Path $bin "espup.exe")) -and (Test-Path $export)) {
        Say ("Reusing rustup, espup and the esp toolchain in " + $script:Tools + " (" + $rustPins + ")"); return
    }
    $url = "https://static.rust-lang.org/rustup/dist/x86_64-pc-windows-msvc/rustup-init.exe"
    if (-not (Test-Path (Join-Path $bin "rustup.exe"))) {
        $init = Join-Path $script:Tools "rustup-init.exe"
        if (Test-Dry) { Say ("DRY RUN, would download " + $url + ".sha256"); Get-Download $url $init }
        else {
            $body = (Invoke-WebRequest -Uri ($url + ".sha256") -UseBasicParsing).Content
            if ($body -is [byte[]]) { $body = [System.Text.Encoding]::ASCII.GetString($body) }
            $sha = ([string]$body).Trim() -split "\s+" | Select-Object -First 1
            if ($sha -notmatch '^[0-9a-fA-F]{64}$') { Stop-Axis "rustup publishes no sha256 we can read" }
            Get-Download $url $init
            Test-Hash $init "SHA256" $sha
        }
        Invoke-Native $init @("-y", "--no-modify-path", "--profile", "minimal", "--default-toolchain", $RustVersion) | Out-Null
        if (-not (Test-Dry)) { Remove-Item -Force $init }
    }
    Invoke-Native (Join-Path $bin "rustup.exe") @("toolchain", "install", $RustVersion, "--profile", "minimal") | Out-Null
    Invoke-Native (Join-Path $bin "cargo.exe") @(("+" + $RustVersion), "install", "espup", "--version", $EspupVersion, "--locked") | Out-Null
    Invoke-Native (Join-Path $bin "espup.exe") @("install", "--targets", "esp32s3", "--toolchain-version", $EspToolchainVersion, "--export-file", $export) | Out-Null
    if (-not (Test-Dry)) { Set-Content -Path $rustOk -Value $rustPins -Encoding ASCII }
}

function Invoke-RadioEnvBuild([string]$EnvName) {
    Invoke-Native $script:Py @("-m", "platformio", "run", "-d", (Join-Path $script:LSrc "firmware"), "-e", $EnvName) @{
        PLATFORMIO_CORE_DIR = (Join-Path $script:Tools "platformio") } | Out-Null
    $out = Join-Path $script:Tools ("out\radio-" + $EnvName)
    if (-not (Test-Dry) -and (Test-Path $out)) { Remove-Item -Recurse -Force $out }
    Invoke-Native $script:Py @("-I", (Get-FlashTool), "manifest", "--kind", "radio", "--env", $EnvName, "--build",
        (Join-Path $script:LSrc ("firmware\.pio\build\" + $EnvName)), "--out", $out, "--release", $script:RadioVersion) | Out-Null
    return $out
}

function Invoke-RadioJob {
    Step ("The " + $ModemName + " for a Heltec " + $Radio.ToUpper())
    $rport = $Port
    if ($script:DoBoard) {
        $rport = $RadioPort
        if ($Flash -and -not (Test-Dry)) { Read-Host "The board is done. Plug in the radio now, then press Enter" | Out-Null }
    }
    if ($Flash) { Invoke-Probe "radio" $rport $Radio }
    Initialize-Rust
    Step ($ModemName + " source at " + $script:RadioVersion)
    $script:LSrc = Join-Path $script:Tools ("src\lfd3k-sm-" + $script:RadioVersion)
    Get-Source $ModemName ($LfdRepo + "/archive/refs/tags/" + $script:RadioVersion + ".tar.gz") $script:LSrc
    Step "Building the modem's Rust core"
    $cargoArgs = @("+esp", "build", "-p", "lfd3k-ffi", "--release", "--target", "xtensa-esp32s3-none-elf", "-Zbuild-std=core")
    $export = Join-Path $script:Tools "export-esp.ps1"
    if (Test-Dry) {
        Say ("DRY RUN, would load " + $export + " and run in " + $script:LSrc + ": RUSTFLAGS=-C panic=abort " + (Format-Command "cargo" $cargoArgs))
        Say "DRY RUN, would copy target\xtensa-esp32s3-none-elf\release\liblfd3k_ffi.a to firmware\lib\"
    } else {
        # loaded as text: the execution policy may forbid running a .ps1 file
        . ([scriptblock]::Create((Get-Content -Raw $export)))
        Push-Location $script:LSrc
        try {
            Invoke-Native (Join-Path $env:CARGO_HOME "bin\cargo.exe") $cargoArgs @{ RUSTFLAGS = "-C panic=abort" } | Out-Null
        } finally { Pop-Location }
        $lib = Join-Path $script:LSrc "firmware\lib"
        New-Item -ItemType Directory -Force $lib | Out-Null
        Copy-Item -Force (Join-Path $script:LSrc "target\xtensa-esp32s3-none-elf\release\liblfd3k_ffi.a") (Join-Path $lib "liblfd3k_ffi.a")
    }
    Step "Building the modem firmware"
    if ($Flash) {
        $out = Invoke-RadioEnvBuild $script:JobEnv
        Invoke-Flash "radio" $out $script:JobPort
    } elseif ($Radio -eq "v3") {
        $out = Invoke-RadioEnvBuild "heltec_v3_single"
        Say ("Built. The images are in " + $out + ". Run again with -Flash to flash them.")
    } else {
        $mm = Invoke-RadioEnvBuild "heltec_v4"
        $single = Invoke-RadioEnvBuild "heltec_v4_single"
        Say ("Built. The modem slot for a multimode V4 is in " + $mm + ", the modem alone in " + $single + ".")
        Say "Run again with -Flash: it reads the V4's table and writes the right one."
    }
}

function Invoke-AxisInstall {
    Show-Banner
    if ($Help) { Show-Usage; return }
    Test-System
    Read-Args
    if (Test-Dry) { Say "DRY RUN: every step is printed; nothing is downloaded, built or flashed." }
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $ProgressPreference = "SilentlyContinue"
    if ($env:AXIS_BUILD_HOME) { $script:Tools = $env:AXIS_BUILD_HOME } else { $script:Tools = Join-Path $env:LOCALAPPDATA "axis-build" }
    Step ("Tool folder " + $script:Tools)
    if (-not (Test-Dry)) { New-Item -ItemType Directory -Force $script:Tools | Out-Null }
    Remove-Partials
    Initialize-Python
    if ((Test-Dry) -and -not $script:SysPy) { Find-Python | Out-Null }
    $script:ReleasePick = $Release
    Initialize-Source
    if ($script:DoBoard) { Test-Selection }
    if ($script:DoRadio) { Test-RadioVersion }
    if ($script:DoBoard) { Invoke-BoardJob }
    if ($script:DoRadio) { Invoke-RadioJob }
    Say ""
    if (Test-Dry) { Say "DRY RUN finished: nothing was downloaded, built or flashed." }
    else { Say "Finished. AXIS Micro BBS by Rev. J. Money / jmscnc.com" }
}

$ErrorActionPreference = "Stop"
$script:Failed = $false
$env:PYTHONIOENCODING = "utf-8"
try {
    Invoke-AxisInstall
} catch {
    $m = $_.Exception.Message
    if ($m.StartsWith("AXIS:")) { Write-Host ("STOPPED: " + $m.Substring(5)) -ForegroundColor Red }
    else { Write-Host ("STOPPED: " + $m) -ForegroundColor Red }
    $script:Failed = $true
}
# Run as a file (-File), a failure gives exit code 1; run from irm, never exit (it would close the window).
if ($script:Failed -and $PSCommandPath) { exit 1 }
