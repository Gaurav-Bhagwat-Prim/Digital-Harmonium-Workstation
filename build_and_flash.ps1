# =============================================================================
# build_and_flash.ps1 — 1-Click ESP32 VHL Firmware Build & Flash Script
# Usage in PowerShell: .\build_and_flash.ps1 [COM_PORT]
# Example: .\build_and_flash.ps1 COM7
# =============================================================================

param (
    [string]$Port = "COM7"
)

$ErrorActionPreference = "Stop"

$ProjectDir = "d:\TYBSC\TYBSC Arduino Project\Esp32 Digital Musical Workstation"
$FirmwareDir = "$ProjectDir\firmware"
$ArduinoCli = "C:\Users\gaura\.vscode\extensions\vscode-arduino.vscode-arduino-community-0.7.2-win32-x64\assets\platform\win32-x64\arduino-cli\arduino-cli.exe"
$Esptool = "C:\Users\gaura\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.3.1\esptool.exe"
$MergedBin = "$FirmwareDir\build\esp32.esp32.esp32\firmware.ino.merged.bin"

Write-Host "=============================================================" -ForegroundColor Cyan
Write-Host " STEP 1: Embedding VHL Instrument File into C Source..." -ForegroundColor Yellow
Write-Host "=============================================================" -ForegroundColor Cyan
Set-Location $ProjectDir
python "$FirmwareDir\embed_vhl.py"

Write-Host "`n=============================================================" -ForegroundColor Cyan
Write-Host " STEP 2: Compiling Firmware for ESP32 Target..." -ForegroundColor Yellow
Write-Host "=============================================================" -ForegroundColor Cyan
Set-Location $FirmwareDir
& $ArduinoCli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app --export-binaries --build-property 'build.extra_flags="-Icomponents/vhl_core/include" "-Icomponents/vhl_hal/include" "-Icomponents/vhl_runtime/include" "-Icomponents/vhl_input/include" "-Icomponents/vhl_storage/include" "-Icomponents/vhl_feature/include" "-Icomponents/vhl_audio/include" "-Icomponents/vhl_ui/include"' "$FirmwareDir"

Write-Host "`n=============================================================" -ForegroundColor Cyan
Write-Host " STEP 3: Flashing Firmware to ESP32 on $Port..." -ForegroundColor Yellow
Write-Host "=============================================================" -ForegroundColor Cyan
& $Esptool --chip esp32 --port $Port --baud 921600 write_flash 0x0000 "$MergedBin"

Write-Host "`n=============================================================" -ForegroundColor Green
Write-Host " SUCCESS! Firmware with your VHL file is flashed to $Port." -ForegroundColor Green
Write-Host "=============================================================" -ForegroundColor Green
