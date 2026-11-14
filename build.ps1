# Builds Melody (the sysmodule and its overlay) with devkitPro's bundled msys2
# and lays them out in dist/ as they go on the SD card (copy what's in dist to
# the root of the card).
# Usage: ./build.ps1 [all|clean]
param(
    [string]$Target = "all"
)

# toolchain.local.ps1 (next to build.ps1, not committed) can point at your
# toolchain: $env:DEVKITPRO_WIN = "D:\devkitPro"
$local = Join-Path $PSScriptRoot "toolchain.local.ps1"
if (Test-Path $local) { . $local }
$dkp = $env:DEVKITPRO_WIN
if (-not $dkp) { $dkp = "C:\devkitPro" }
$bash = Join-Path $dkp "msys2\usr\bin\bash.exe"
if (-not (Test-Path $bash)) { throw "devkitPro msys2 not found at $dkp (set DEVKITPRO_WIN)" }

function ToMsys([string]$p) { "/" + $p.Substring(0, 1).ToLower() + ($p.Substring(2) -replace '\\', '/') }
$root = ToMsys $PSScriptRoot
$env:DEVKITPRO_DIR = ToMsys $dkp

function Invoke-Make([string]$dir, [string]$makeArgs) {
    & $bash -lc "source '$root/scripts/devkitpro-env.sh' && cd '$root/$dir' && make -j8 $makeArgs"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if ($Target -eq "clean") {
    Invoke-Make "sysmodule" "clean"
    Invoke-Make "overlay" "clean"
    if (Test-Path (Join-Path $PSScriptRoot "dist")) { Remove-Item -Recurse -Force (Join-Path $PSScriptRoot "dist") }
    exit 0
}

Invoke-Make "sysmodule" ""
Invoke-Make "overlay" ""

# the sysmodule: starts with every boot (boot2.flag), quiet until something is played
$sys = Join-Path $PSScriptRoot "dist\atmosphere\contents\420000000000AD10"
New-Item -ItemType Directory -Force (Join-Path $sys "flags") | Out-Null
Copy-Item (Join-Path $PSScriptRoot "sysmodule\melody.nsp") (Join-Path $sys "exefs.nsp")
New-Item -ItemType File -Force (Join-Path $sys "flags\boot2.flag") | Out-Null
# (for sysmodule managers such as Ultrahand's ovl-sysmodules: its name, and that it can be started without a reboot)
Set-Content -Encoding ascii (Join-Path $sys "toolbox.json") '{"name": "Melody", "tid": "420000000000AD10", "requires_reboot": false}'
Write-Host "the sysmodule is in $sys"

# keeps Game Volume where it's set when the HOME button's overlay fades the sound
# (firmware 23.5.0's audio sysmodule only: on another firmware Atmosphère skips it)
$ips = Join-Path $PSScriptRoot "dist\atmosphere\exefs_patches\melody_audio_disable_attenuation"
New-Item -ItemType Directory -Force $ips | Out-Null
Copy-Item (Join-Path $PSScriptRoot "extras\audio_disable_attenuation\*.ips") $ips
Write-Host "the audio patch is in $ips"

# the overlay, for Ultrahand's menu
$ovl = Join-Path $PSScriptRoot "dist\switch\.overlays"
New-Item -ItemType Directory -Force $ovl | Out-Null
Copy-Item (Join-Path $PSScriptRoot "overlay\melody.ovl") (Join-Path $ovl "melody.ovl")
Write-Host "the overlay is in $ovl"
