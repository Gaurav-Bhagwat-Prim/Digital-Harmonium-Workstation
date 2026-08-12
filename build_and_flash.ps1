# =============================================================================
# build_and_flash.ps1 — 1-Click ESP32 VHR Recording Demo: Build, Flash & Capture
#
# What this script does, in order:
#   STEP 1 - Embed Harmonium VHL into C source via embed_vhl.py
#   STEP 2 - Compile firmware with arduino-cli
#   STEP 3 - Flash merged binary to ESP32 via esptool
#   STEP 4 - Check pyserial is installed (install if missing)
#   STEP 5 - Open Builder folder in Explorer so you can find saved .vhr files
#   STEP 6 - Launch vhr_receiver.py in a NEW PowerShell window
#            (The receiver waits for the ESP32 to boot and send the VHR binary)
#
# Usage:
#   .\build_and_flash.ps1              # uses default COM7
#   .\build_and_flash.ps1 -Port COM5   # specify port
#
# After the receiver window opens:
#   - The ESP32 will boot automatically after flashing
#   - Press Switch 1 (GPIO32) to record NOTE_ON events
#   - Press Switch 2 (GPIO33) to record NOTE_OFF events
#   - After 20 seconds the VHR binary is sent and saved as recording_<timestamp>.vhr
#   - Run: python vhr_decompiler.py recording_<timestamp>.vhr --events
# =============================================================================

param (
    [string]$Port = "COM7"
)

$ErrorActionPreference = "Stop"

# ─── Path Configuration ───────────────────────────────────────────────────────
$ProjectDir  = "d:\TYBSC\TYBSC Arduino Project\Esp32 Digital Musical Workstation"
$FirmwareDir = "$ProjectDir\firmware"
$BuilderDir  = "$ProjectDir\Builder"

$ArduinoCli  = "C:\Users\gaura\.vscode\extensions\vscode-arduino.vscode-arduino-community-0.7.2-win32-x64\assets\platform\win32-x64\arduino-cli\arduino-cli.exe"
$Esptool     = "C:\Users\gaura\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.3.1\esptool.exe"
$MergedBin   = "$FirmwareDir\build\esp32.esp32.esp32\firmware.ino.merged.bin"

$EmbedScript    = "$FirmwareDir\embed_vhl.py"
$ReceiverScript = "$BuilderDir\vhr_receiver.py"
$DecompScript   = "$BuilderDir\vhr_decompiler.py"

# ─── Header ──────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "=============================================================" -ForegroundColor Cyan
Write-Host "   Digital Harmonium Workstation - VHR Recording Demo" -ForegroundColor Cyan
Write-Host "   Build, Flash & Capture Pipeline" -ForegroundColor Cyan
Write-Host "=============================================================" -ForegroundColor Cyan
Write-Host "   COM Port : $Port" -ForegroundColor White
Write-Host "   Project  : $ProjectDir" -ForegroundColor White
Write-Host "=============================================================" -ForegroundColor Cyan
Write-Host ""

# =============================================================================
# STEP 1 — Embed Harmonium VHL instrument file into C source
# =============================================================================
Write-Host "-------------------------------------------------------------" -ForegroundColor Yellow
Write-Host " STEP 1/5  Embedding Harmonium VHL into C source..." -ForegroundColor Yellow
Write-Host "-------------------------------------------------------------" -ForegroundColor Yellow

Set-Location $ProjectDir

if (-not (Test-Path $EmbedScript)) {
    Write-Host "[ERROR] embed_vhl.py not found at: $EmbedScript" -ForegroundColor Red
    exit 1
}

python $EmbedScript
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] embed_vhl.py failed (exit code $LASTEXITCODE)" -ForegroundColor Red
    exit 1
}

Write-Host "[OK] Harmonium VHL embedded successfully." -ForegroundColor Green
Write-Host ""

# =============================================================================
# STEP 2 — Compile firmware with arduino-cli
# =============================================================================
Write-Host "`n-------------------------------------------------------------" -ForegroundColor Yellow
Write-Host " STEP 2/5  Stripping UTF-8 BOM from firmware.ino..." -ForegroundColor Yellow
Write-Host "-------------------------------------------------------------" -ForegroundColor Yellow
# PowerShell Set-Content can write a UTF-8 BOM that Arduino-cli rejects.
# Always strip it before compiling.
$inoPath = "$FirmwareDir\firmware.ino"
$bytes = [System.IO.File]::ReadAllBytes($inoPath)
if ($bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
    [System.IO.File]::WriteAllBytes($inoPath, $bytes[3..($bytes.Length-1)])
    Write-Host "[OK] BOM stripped from firmware.ino" -ForegroundColor Green
} else {
    Write-Host "[OK] firmware.ino has no BOM - clean." -ForegroundColor Green
}
Write-Host ""

Write-Host "-------------------------------------------------------------" -ForegroundColor Yellow
Write-Host " STEP 3/5  Compiling firmware for ESP32..." -ForegroundColor Yellow
Write-Host "-------------------------------------------------------------" -ForegroundColor Yellow

if (-not (Test-Path $ArduinoCli)) {
    Write-Host "[ERROR] arduino-cli not found at: $ArduinoCli" -ForegroundColor Red
    exit 1
}

Set-Location $FirmwareDir

& $ArduinoCli compile `
    --fqbn esp32:esp32:esp32:PartitionScheme=huge_app `
    --export-binaries `
    --build-property 'build.extra_flags="-Icomponents/vhl_core/include" "-Icomponents/vhl_hal/include" "-Icomponents/vhl_runtime/include" "-Icomponents/vhl_input/include" "-Icomponents/vhl_storage/include" "-Icomponents/vhl_feature/include" "-Icomponents/vhl_audio/include" "-Icomponents/vhl_ui/include"' `
    "$FirmwareDir"

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Compilation failed (exit code $LASTEXITCODE)" -ForegroundColor Red
    exit 1
}

Write-Host "[OK] Firmware compiled successfully." -ForegroundColor Green
Write-Host ""

# =============================================================================
# STEP 3 — Flash merged binary to ESP32
# =============================================================================
Write-Host "-------------------------------------------------------------" -ForegroundColor Yellow
Write-Host " STEP 3/5  Flashing firmware to ESP32 on $Port..." -ForegroundColor Yellow
Write-Host "-------------------------------------------------------------" -ForegroundColor Yellow

if (-not (Test-Path $Esptool)) {
    Write-Host "[ERROR] esptool not found at: $Esptool" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $MergedBin)) {
    Write-Host "[ERROR] Merged binary not found at: $MergedBin" -ForegroundColor Red
    Write-Host "        Compilation may have failed or used a different output path." -ForegroundColor Red
    exit 1
}

& $Esptool --chip esp32 --port $Port --baud 921600 write_flash 0x0000 "$MergedBin"

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Flash failed (exit code $LASTEXITCODE). Check that $Port is correct and ESP32 is connected." -ForegroundColor Red
    exit 1
}

Write-Host "[OK] Firmware flashed successfully to $Port." -ForegroundColor Green
Write-Host ""

# =============================================================================
# STEP 4 — Ensure pyserial is installed for the receiver script
# =============================================================================
Write-Host "-------------------------------------------------------------" -ForegroundColor Yellow
Write-Host " STEP 4/5  Checking pyserial dependency..." -ForegroundColor Yellow
Write-Host "-------------------------------------------------------------" -ForegroundColor Yellow

$pyserialCheck = python -c "import serial; print('ok')" 2>&1
if ($pyserialCheck -ne "ok") {
    Write-Host "  pyserial not found. Installing via pip..." -ForegroundColor Yellow
    pip install pyserial
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] pip install pyserial failed. Install manually: pip install pyserial" -ForegroundColor Red
        exit 1
    }
    Write-Host "[OK] pyserial installed." -ForegroundColor Green
} else {
    Write-Host "[OK] pyserial is already installed." -ForegroundColor Green
}
Write-Host ""

# =============================================================================
# STEP 5 — Open Builder folder in Explorer (so you can find the .vhr output)
# =============================================================================
Write-Host "-------------------------------------------------------------" -ForegroundColor Yellow
Write-Host " STEP 5/5  Opening Builder folder in Explorer..." -ForegroundColor Yellow
Write-Host "-------------------------------------------------------------" -ForegroundColor Yellow

Start-Process explorer.exe $BuilderDir
Write-Host "[OK] Builder folder opened — your .vhr files will appear here." -ForegroundColor Green
Write-Host ""

# =============================================================================
# STEP 6 — Launch VHR receiver in a new PowerShell window
# =============================================================================
Write-Host "=============================================================" -ForegroundColor Cyan
Write-Host " Launching VHR Receiver in a new window..." -ForegroundColor Cyan
Write-Host "=============================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "  The ESP32 will boot automatically after flashing." -ForegroundColor White
Write-Host "  The new window will wait for the VHR binary." -ForegroundColor White
Write-Host ""
Write-Host "  On the ESP32:" -ForegroundColor White
Write-Host "    LED1 ON      = Recording is active (20 second window)" -ForegroundColor Green
Write-Host "    Switch 1     = NOTE_ON  (press to play and record a note)" -ForegroundColor White
Write-Host "    Switch 2     = NOTE_OFF (press to release the note)" -ForegroundColor White
Write-Host "    LED2 ON      = Note currently held" -ForegroundColor White
Write-Host "    After 20s    = VHR binary is sent automatically" -ForegroundColor White
Write-Host ""
Write-Host "  After capture, run in the Builder folder:" -ForegroundColor White
Write-Host "    python vhr_decompiler.py recording_<timestamp>.vhr --events" -ForegroundColor Cyan
Write-Host ""

# Launch the receiver in a new, persistent PowerShell window.
# The window title shows the port so you know which session it is.
# Build the receiver + auto-decompile command chain.
# The window stays open (-NoExit) showing the decompiler output so you can read it.
$ReceiverCmd   = "python `"$ReceiverScript`" --port $Port --out `"$BuilderDir\recording_demo.vhr`""
$DecompilerCmd = "python `"$DecompScript`" `"$BuilderDir\recording_demo.vhr`" --events"
$FullCmd = @"
`$host.ui.RawUI.WindowTitle = '$WindowTitle'
Set-Location '$BuilderDir'
Write-Host ''
Write-Host '=============================================================' -ForegroundColor Cyan
Write-Host '  VHR Receiver - waiting for ESP32 boot...' -ForegroundColor Cyan
Write-Host '  SW1 (GPIO32) = Sa/C4   PRESS=NOTE_ON  RELEASE=NOTE_OFF' -ForegroundColor White
Write-Host '  SW2 (GPIO33) = Pa/G4   PRESS=NOTE_ON  RELEASE=NOTE_OFF' -ForegroundColor White
Write-Host '  Both notes are independent. Hold one, tap the other!' -ForegroundColor White
Write-Host '=============================================================' -ForegroundColor Cyan
Write-Host ''
$ReceiverCmd
Write-Host ''
Write-Host '=============================================================' -ForegroundColor Green
Write-Host '  VHR file saved! Running decompiler...' -ForegroundColor Green
Write-Host '=============================================================' -ForegroundColor Green
Write-Host ''
$DecompilerCmd
Write-Host ''
Write-Host 'Press Enter to close this window...' -ForegroundColor Yellow
Read-Host
"@

Start-Process powershell.exe -ArgumentList "-NoExit", "-Command", $FullCmd

Write-Host "=============================================================" -ForegroundColor Green
Write-Host " ALL STEPS COMPLETE!" -ForegroundColor Green
Write-Host "=============================================================" -ForegroundColor Green
Write-Host ""
Write-Host "  Receiver window: '$WindowTitle'" -ForegroundColor White
Write-Host "  Output file    : $BuilderDir\recording_demo.vhr" -ForegroundColor White
Write-Host "  Decompile with : python `"$DecompScript`" recording_demo.vhr --events" -ForegroundColor Cyan
Write-Host ""
