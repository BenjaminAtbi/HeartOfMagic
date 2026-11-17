<#
.SYNOPSIS
    The dev renderer: the panel's dev harness, full screen in a Chromium browser, on the game's own data,
    reloading on every saved change.
.DESCRIPTION
    Starts tools/dev-renderer/serve.js (Node) in its own window, then opens the harness in Chrome (Edge when
    Chrome is not installed) as a full-screen app window with a profile of its own (so it never joins a
    running browser session, and full screen sticks). F11 leaves full screen; F12 opens DevTools.
    -Data is the game's SKSE/Plugins/SpellLearning folder (spell_scan_output.json needs debug.json in the
    install); without it the harness's test data is used. HOM_DEV_DATA stands in for -Data.
    Chromium is not Ultralight: layout, fonts and some CSS differ from the game. Check the final look in game.
.EXAMPLE
    .\tools\dev-renderer\launch.ps1 -Data 'D:\Game\MO2\mods\Heart of Magic Spell Tree\SKSE\Plugins\SpellLearning'
    .\tools\dev-renderer\launch.ps1 -Windowed -Width 1920 -Height 1080
#>
[CmdletBinding()]
param(
    [string]$Data = $env:HOM_DEV_DATA,
    [int]$Port = 5557,
    [switch]$Windowed,
    [int]$Width = 1920,
    [int]$Height = 1080
)
$ErrorActionPreference = 'Stop'

$serve = Join-Path $PSScriptRoot 'serve.js'
$node = (Get-Command node -ErrorAction Stop).Source
$browsers = @(
    "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
    "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
    "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe",
    "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
    "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe"
)
$browser = $browsers | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $browser) { throw 'No Chrome or Edge found.' }
if ($Data -and -not (Test-Path (Join-Path $Data 'config.json'))) { throw "No config.json in -Data '$Data'." }

# The server: reuse one already listening on the port, otherwise start it in a window of its own
$listening = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
if ($listening) {
    Write-Host "A server already listens on port $Port - using it (close its window to restart with other -Data)."
} else {
    $serveArgs = @("`"$serve`"", '--port', $Port)
    if ($Data) { $serveArgs += @('--data', "`"$Data`"") }
    Start-Process -FilePath $node -ArgumentList $serveArgs -WindowStyle Minimized | Out-Null
    $deadline = (Get-Date).AddSeconds(10)
    while (-not (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue)) {
        if ((Get-Date) -gt $deadline) { throw "The server did not start on port $Port." }
        Start-Sleep -Milliseconds 200
    }
}

$profileDir = Join-Path $env:TEMP 'hom-dev-renderer-profile'
$url = "http://127.0.0.1:$Port/dev-harness.html"
$browserArgs = @("--app=$url", "--user-data-dir=`"$profileDir`"", '--no-first-run', '--no-default-browser-check',
    "--window-size=$Width,$Height")
if (-not $Windowed) { $browserArgs += '--start-fullscreen' }
Start-Process -FilePath $browser -ArgumentList $browserArgs | Out-Null
Write-Host "Dev renderer: $url in $(Split-Path $browser -Leaf)$(if ($Windowed) { " ($Width x $Height)" } else { ' (full screen, F11 to leave)' })"
Write-Host "Game data: $(if ($Data) { $Data } else { '(none: test data)' })"
