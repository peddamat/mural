[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet("build-image", "build", "upload")]
    [string]$Action,

    [Parameter(Position = 1)]
    [ValidateSet("all", "hw649_uno", "hw649_esp8266", "hw649_esp8266-fs", "hw649_esp8266-all", "esp32dev")]
    [string]$Target = "all",

    [string]$Port = "",

    [switch]$NoCache
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ImageName = "mural-devcontainer:latest"
$PioVolume = "mural-pio-core"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$DevcontainerDir = $PSScriptRoot
$Containerfile = Join-Path $DevcontainerDir "Containerfile"

function Assert-Wslc {
    if (-not (Get-Command wslc -ErrorAction SilentlyContinue)) {
        throw "wslc was not found on PATH. Install/update WSL (wsl --update) to enable native WSL containers."
    }
}

function Invoke-BuildImage {
    param([switch]$ForceNoCache)
    Assert-Wslc
    Write-Host "==> Building wslc devcontainer image ($ImageName)..." -ForegroundColor Cyan
    $buildArgs = @("build", "-t", $ImageName, "--build-arg", "VARIANT=bookworm", "-f", $Containerfile)
    if ($ForceNoCache) {
        $buildArgs += "--no-cache"
    }
    $buildArgs += $DevcontainerDir
    & wslc @buildArgs
    if ($LASTEXITCODE -ne 0) {
        throw "wslc build failed with exit code $LASTEXITCODE"
    }
}

function Ensure-ImageAndVolume {
    Assert-Wslc
    $existing = & wslc images --format "{{.Repository}}:{{.Tag}}" 2>$null
    if ($LASTEXITCODE -ne 0 -or -not ($existing -contains $ImageName)) {
        $rawImages = (& wslc images 2>$null | Out-String)
        if ($rawImages -notmatch "mural-devcontainer\s+latest") {
            Invoke-BuildImage
        }
    }

    $rawVolumes = (& wslc volume list 2>$null | Out-String)
    if ($rawVolumes -notmatch [regex]::Escape($PioVolume)) {
        Write-Host "==> Creating persistent PlatformIO cache volume ($PioVolume)..." -ForegroundColor Cyan
        & wslc volume create $PioVolume | Out-Null
    }
}

function Invoke-InContainer {
    param([Parameter(Mandatory = $true)][string]$BashCommand)
    Ensure-ImageAndVolume

    $repoMount = "type=bind,source=$($RepoRoot -replace '\\', '/'),target=/workspaces/mural"
    $pioMount = "type=volume,source=$PioVolume,target=/home/vscode/.pio-core"
    $fullCommand = "set -e; test -w /home/vscode/.pio-core || sudo chown -R vscode:vscode /home/vscode/.pio-core; $BashCommand"

    & wslc run --rm `
        --mount $repoMount `
        --mount $pioMount `
        -w /workspaces/mural `
        $ImageName `
        bash -lc $fullCommand

    if ($LASTEXITCODE -ne 0) {
        throw "Container command failed with exit code $LASTEXITCODE"
    }
}

function Resolve-HostPio {
    $cmd = Get-Command pio -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    $defaultPio = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\pio.exe"
    if (Test-Path $defaultPio) {
        return $defaultPio
    }
    throw "PlatformIO CLI (pio.exe) was not found on the Windows host. Install the PlatformIO IDE VS Code extension (or PlatformIO Core) on Windows to flash over USB COM ports."
}

function Invoke-HostUpload {
    param(
        [Parameter(Mandatory = $true)][string]$EnvName,
        [Parameter(Mandatory = $true)][string]$UploadTarget,
        [string]$UploadPort
    )
    $pio = Resolve-HostPio
    $pioArgs = @("run", "-d", $RepoRoot, "-e", $EnvName, "-t", "nobuild", "-t", $UploadTarget)
    if (-not [string]::IsNullOrWhiteSpace($UploadPort)) {
        $pioArgs += @("--upload-port", $UploadPort)
    }
    $env:MURAL_SKIP_WEB_BUILD = "1"
    try {
        Write-Host "==> Running host upload: pio $($pioArgs -join ' ')" -ForegroundColor Cyan
        & $pio @pioArgs
        if ($LASTEXITCODE -ne 0) {
            throw "Host upload failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Remove-Item Env:MURAL_SKIP_WEB_BUILD -ErrorAction SilentlyContinue
    }
}

switch ($Action) {
    "build-image" {
        Invoke-BuildImage -ForceNoCache:$NoCache
    }

    "build" {
        $ensureWebDeps = '[ -d worker/node_modules ] || npm i --prefix worker; [ -d ui/node_modules ] || npm i --prefix ui'
        switch ($Target) {
            "hw649_uno" {
                Write-Host "==> Building hw649_uno (ATmega328P) in wslc devcontainer..." -ForegroundColor Cyan
                Invoke-InContainer "pio run -e hw649_uno"
            }
            "hw649_esp8266" {
                Write-Host "==> Building hw649_esp8266 (Firmware + Web UI LittleFS) in wslc devcontainer..." -ForegroundColor Cyan
                Invoke-InContainer "$ensureWebDeps; pio run -e hw649_esp8266 && MURAL_SKIP_WEB_BUILD=1 pio run -e hw649_esp8266 -t buildfs"
            }
            "hw649_esp8266-fs" {
                Write-Host "==> Building hw649_esp8266 Web UI LittleFS image in wslc devcontainer..." -ForegroundColor Cyan
                Invoke-InContainer "$ensureWebDeps; python3 build.py && MURAL_SKIP_WEB_BUILD=1 pio run -e hw649_esp8266 -t buildfs"
            }
            "esp32dev" {
                Write-Host "==> Building esp32dev (FluidNC + Merged Bin) in wslc devcontainer..." -ForegroundColor Cyan
                Invoke-InContainer "$ensureWebDeps; pio run -e esp32dev -t mergebin"
            }
            "all" {
                Write-Host "==> Building HW-649 firmwares (hw649_uno + hw649_esp8266 + LittleFS) in wslc devcontainer..." -ForegroundColor Cyan
                Invoke-InContainer "$ensureWebDeps; pio run -e hw649_uno && pio run -e hw649_esp8266 && MURAL_SKIP_WEB_BUILD=1 pio run -e hw649_esp8266 -t buildfs"
            }
            default {
                throw "Unsupported build target: $Target"
            }
        }
    }

    "upload" {
        switch ($Target) {
            "hw649_uno" {
                Write-Host "DIP Switch Reminder: SW 3 & 4 ON (all others OFF) for ATmega328P upload." -ForegroundColor Yellow
                Invoke-HostUpload -EnvName "hw649_uno" -UploadTarget "upload" -UploadPort $Port
            }
            "hw649_esp8266" {
                Write-Host "DIP Switch Reminder: SW 5, 6 & 7 ON (all others OFF) and press Reset for ESP8266 upload." -ForegroundColor Yellow
                Invoke-HostUpload -EnvName "hw649_esp8266" -UploadTarget "upload" -UploadPort $Port
            }
            "hw649_esp8266-fs" {
                Write-Host "DIP Switch Reminder: SW 5, 6 & 7 ON (all others OFF) and press Reset for ESP8266 LittleFS upload." -ForegroundColor Yellow
                Invoke-HostUpload -EnvName "hw649_esp8266" -UploadTarget "uploadfs" -UploadPort $Port
            }
            "hw649_esp8266-all" {
                Write-Host "DIP Switch Reminder: SW 5, 6 & 7 ON (all others OFF) and press Reset for ESP8266 firmware + LittleFS upload." -ForegroundColor Yellow
                Invoke-HostUpload -EnvName "hw649_esp8266" -UploadTarget "upload" -UploadPort $Port
                Invoke-HostUpload -EnvName "hw649_esp8266" -UploadTarget "uploadfs" -UploadPort $Port
            }
            "esp32dev" {
                Invoke-HostUpload -EnvName "esp32dev" -UploadTarget "upload" -UploadPort $Port
                Invoke-HostUpload -EnvName "esp32dev" -UploadTarget "uploadfs" -UploadPort $Port
            }
            default {
                throw "Unsupported upload target: $Target"
            }
        }
    }
}
