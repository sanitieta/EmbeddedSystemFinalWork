# Build driver for TM4C1294 Smart Connected Clock firmware
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File driver.ps1 [build|clean|flash|monitor]
param([string]$Action = "build")

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$UV4 = "D:\Programs\KEIL_V5\UV4\UV4.exe"
$ProjectFile = "$ProjectRoot\main.uvprojx"
$TargetName = "Target 1"
$BuildLog = "$ProjectRoot\build.log"
$HexFile = "$ProjectRoot\Objects\main.hex"
$AxFile = "$ProjectRoot\Objects\main.axf"

# Flash device name (TM4C1294NCPDT via Stellaris ICDI / CMSIS-DAP)
$FlashTool = "D:\Programs\KEIL_V5\UV4\UV4.exe"

function Write-Step {
    param([string]$Message)
    Write-Host "`n==> $Message" -ForegroundColor Cyan
}

function Build-Firmware {
    Write-Step "Building $ProjectFile (target: $TargetName)"

    if (-not (Test-Path $UV4)) {
        Write-Host "ERROR: Keil UV4 not found at $UV4" -ForegroundColor Red
        Write-Host "Install Keil MDK-ARM v5 to D:\Programs\KEIL_V5 or update `$UV4 path." -ForegroundColor Yellow
        exit 1
    }

    # Run Keil build
    $uv4Args = @("-r", $ProjectFile, "-t", "`"$TargetName`"", "-o", $BuildLog)
    $proc = Start-Process -FilePath $UV4 -ArgumentList "-r `"$ProjectFile`" -t `"$TargetName`" -o `"$BuildLog`"" -Wait -NoNewWindow -PassThru

    # Parse build log
    if (-not (Test-Path $BuildLog)) {
        Write-Host "ERROR: Build log not generated" -ForegroundColor Red
        exit 1
    }

    $logContent = Get-Content $BuildLog -Raw
    $errors = ([regex]::Matches($logContent, '(\d+)\s+Error\(s\)')).Value
    $warnings = ([regex]::Matches($logContent, '(\d+)\s+Warning\(s\)')).Value

    Write-Host $logContent

    if ($proc.ExitCode -ne 0 -or $errors -notmatch '0 Error') {
        Write-Host "`nBUILD FAILED: $errors" -ForegroundColor Red
        exit 1
    }

    Write-Host "`nBUILD SUCCESS: $errors, $warnings" -ForegroundColor Green

    # Verify output files
    if (-not (Test-Path $HexFile)) {
        Write-Host "ERROR: Hex file not generated at $HexFile" -ForegroundColor Red
        exit 1
    }

    $hexSize = (Get-Item $HexFile).Length
    $hexLines = (Get-Content $HexFile).Count
    Write-Host "Hex file: $HexFile ($hexLines lines, $hexSize bytes)" -ForegroundColor Green

    # Parse code size from log
    $codeMatch = [regex]::Match($logContent, 'Code=(\d+)')
    $roMatch = [regex]::Match($logContent, 'RO-data=(\d+)')
    $rwMatch = [regex]::Match($logContent, 'RW-data=(\d+)')
    $ziMatch = [regex]::Match($logContent, 'ZI-data=(\d+)')

    if ($codeMatch.Success) {
        $codeSize = [int]$codeMatch.Groups[1].Value
        $roSize = [int]$roMatch.Groups[1].Value
        $rwSize = [int]$rwMatch.Groups[1].Value
        $ziSize = [int]$ziMatch.Groups[1].Value
        $flashUsed = $codeSize + $roSize + $rwSize
        $ramUsed = $rwSize + $ziSize
        Write-Host ("Flash: {0} bytes (max 1048576), RAM: {1} bytes" -f $flashUsed, $ramUsed)
    }
}

function Invoke-Clean {
    Write-Step "Cleaning build artifacts"
    Remove-Item -Path "$ProjectRoot\Objects\*.o" -Force -ErrorAction SilentlyContinue
    Remove-Item -Path "$ProjectRoot\Objects\*.axf" -Force -ErrorAction SilentlyContinue
    Remove-Item -Path "$ProjectRoot\Objects\*.hex" -Force -ErrorAction SilentlyContinue
    Remove-Item -Path $BuildLog -Force -ErrorAction SilentlyContinue
    Remove-Item -Path "$ProjectRoot\*.dep" -Force -ErrorAction SilentlyContinue
    Remove-Item -Path "$ProjectRoot\*.lst" -Force -ErrorAction SilentlyContinue
    Remove-Item -Path "$ProjectRoot\*.crf" -Force -ErrorAction SilentlyContinue
    Remove-Item -Path "$ProjectRoot\*.d" -Force -ErrorAction SilentlyContinue
    Remove-Item -Path "$ProjectRoot\*.o" -Force -ErrorAction SilentlyContinue
    Write-Host "Clean complete." -ForegroundColor Green
}

function Invoke-Flash {
    Write-Step "Flashing firmware to TM4C1294NCPDT"

    if (-not (Test-Path $HexFile)) {
        Write-Host "ERROR: No hex file found. Run build first." -ForegroundColor Red
        exit 1
    }

    # Keil UV4 flash download
    $flashArgs = @("-f", $ProjectFile, "-t", $TargetName)
    Write-Host "Launching: $UV4 -f `"$ProjectFile`" -t `"$TargetName`""
    Write-Host "NOTE: Requires TM4C1294 board connected via Stellaris ICDI debug probe." -ForegroundColor Yellow

    Start-Process -FilePath $UV4 -ArgumentList "-f `"$ProjectFile`" -t `"$TargetName`"" -Wait -NoNewWindow

    if ($LASTEXITCODE -ne 0) {
        Write-Host "Flash failed. Check debug probe connection." -ForegroundColor Red
        exit 1
    }
    Write-Host "Flash complete." -ForegroundColor Green
}

function Invoke-SerialMonitor {
    Write-Step "Opening serial monitor"
    Write-Host "UART0: 115200 baud, 8N1, TM4C1294 UART0 (PA0=RX, PA1=TX)" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Useful UART commands (CR+LF terminated):" -ForegroundColor Cyan
    Write-Host "  *GET:DATE      - Get current date"
    Write-Host "  *GET:TIME      - Get current time"
    Write-Host "  *GET:ALARM     - Get alarm settings"
    Write-Host "  *GET:DISPLAY   - Get display state"
    Write-Host "  *GET:FORMAT    - Get display format"
    Write-Host "  *SET:DISPLAY ON/OFF  - Toggle display"
    Write-Host "  *SET:FORMAT LEFT/RIGHT - Set display format"
    Write-Host "  *SET:MSG <text> - Display message (max 32 bytes)"
    Write-Host "  *SET:LED <hex2>  - LED takeover (00 to release)"
    Write-Host "  *SET:MODE NIGHT/NORMAL - Night mode toggle"
    Write-Host "  *RST           - Reset protocol state"
    Write-Host ""

    # Try common serial terminal tools
    $comPort = $null
    $tools = @(
        @{Path="C:\Program Files\PuTTY\putty.exe"; Args="-serial COM3 -sercfg 115200,8,n,1,N"}
    )

    Write-Host "Connect your serial terminal to the TM4C1294 UART0 port."
    Write-Host "Or use: putty -serial COM3 -sercfg 115200,8,n,1,N"
}

# Main dispatch
switch ($Action) {
    "build"   { Build-Firmware }
    "clean"   { Invoke-Clean }
    "flash"   { Invoke-Flash }
    "monitor" { Invoke-SerialMonitor }
    "all"     { Invoke-Clean; Build-Firmware; Invoke-Flash }
    default {
        Write-Host "Usage: driver.ps1 [build|clean|flash|monitor|all]"
        Write-Host "  build   - Compile firmware via Keil UV4 (default)"
        Write-Host "  clean   - Remove build artifacts"
        Write-Host "  flash   - Flash firmware to TM4C1294 via debug probe"
        Write-Host "  monitor - Show serial connection info and UART commands"
        Write-Host "  all     - Clean, build, and flash"
        exit 1
    }
}
