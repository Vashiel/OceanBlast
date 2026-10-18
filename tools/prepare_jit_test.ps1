param(
    [Parameter(Mandatory=$true)][string]$RomPath,
    [string]$ExecutablePath,
    [string]$NvramPath,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (!$ExecutablePath) { $ExecutablePath = Join-Path $repo 'build/jit/oceanblast_jit_player.exe' }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repo 'bin/JIT-Test' }
$exe = (Resolve-Path -LiteralPath $ExecutablePath).Path
$rom = (Resolve-Path -LiteralPath $RomPath).Path
$out = [IO.Path]::GetFullPath($OutputDirectory)
if ($out -eq [IO.Path]::GetFullPath((Join-Path $repo 'bin'))) {
    throw 'Use a separate test directory below bin, or another isolated folder.'
}
if ($NvramPath) { $NvramPath = (Resolve-Path -LiteralPath $NvramPath).Path }
New-Item -ItemType Directory -Path $out -Force | Out-Null
Copy-Item -LiteralPath $exe -Destination (Join-Path $out 'OceanBlast-JIT.exe')
$settings = Join-Path $out 'settings.ini'
$originalSettings = Join-Path $env:APPDATA 'OceanBlast/windows.ini'
if (!(Test-Path -LiteralPath $settings) -and (Test-Path -LiteralPath $originalSettings)) {
    Copy-Item -LiteralPath $originalSettings -Destination $settings
}
foreach ($mode in @('jit','interpreter')) {
    $run = Join-Path $out $mode
    New-Item -ItemType Directory -Path $run -Force | Out-Null
    $save = Join-Path $run 'wade-test.nvram'
    if ($NvramPath -and !(Test-Path -LiteralPath $save)) {
        Copy-Item -LiteralPath $NvramPath -Destination $save
    }
}
@{
    rom = $rom
    executable_sha256 = (Get-FileHash -LiteralPath (Join-Path $out 'OceanBlast-JIT.exe') -Algorithm SHA256).Hash.ToLower()
    status = 'Development build: slower than interpreter; pacing and audio unaccepted.'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'test-config.json') -Encoding UTF8
$start = @'
param([ValidateSet('jit','interpreter')][string]$Engine='jit')
$ErrorActionPreference='Stop'
$config=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'test-config.json') -Raw | ConvertFrom-Json
$exe=Join-Path $PSScriptRoot 'OceanBlast-JIT.exe'
$run=Join-Path $PSScriptRoot $Engine
$previousSettings=$env:OCEANBLAST_SETTINGS_PATH
$env:OCEANBLAST_SETTINGS_PATH=Join-Path $PSScriptRoot 'settings.ini'
Push-Location -LiteralPath $run
try {
    Write-Host 'OceanBlast development test. JIT is currently slower; audio acceptance is open.'
    Write-Host 'Close the game window to finish. Logs and saves stay in this test folder.'
    & $exe $config.rom --engine $Engine --title-blit off --gui --sound --profile --timing legacy --cpu-steps-per-tick 6 --cart-language keep --nvram (Join-Path $run 'wade-test.nvram') --window-mode plain --scale 3 *> (Join-Path $run 'run.log')
    if ($LASTEXITCODE -ne 0) { throw "Game exited with code $LASTEXITCODE. See $run\run.log" }
} finally {
    Pop-Location
    $env:OCEANBLAST_SETTINGS_PATH=$previousSettings
}
'@
Set-Content -LiteralPath (Join-Path $out 'Start-Wade.ps1') -Value $start -Encoding UTF8
foreach ($mode in @('jit','interpreter')) {
    $label = if ($mode -eq 'jit') { 'Wade-JIT-Test.cmd' } else { 'Wade-Interpreter-Reference.cmd' }
    $starter = "@echo off`r`nsetlocal DisableDelayedExpansion`r`npowershell.exe -NoProfile -ExecutionPolicy Bypass -File `"%~dp0Start-Wade.ps1`" -Engine $mode`r`nif errorlevel 1 pause`r`n"
    Set-Content -LiteralPath (Join-Path $out $label) -Value $starter -Encoding ASCII
}
@'
OceanBlast JIT development build

Wade-JIT-Test.cmd starts the native engine.
Wade-Interpreter-Reference.cmd starts the reference engine in the same EXE.
Run one at a time. Both disable the legacy title shortcut and use ratio 6.

Status: the JIT is currently slower than the interpreter. The short boot
CPU/SDRAM check passes; title, gameplay, pacing and live audio are unaccepted.
This is an early development test, not an upgrade to the installed player.

Keyboard mappings are copied from your existing settings when prepared.
Each engine has its own save and run.log. The JIT also writes engine_stats.csv.
Settings are local to this folder. Close the game window to finish.
The cartridge remains in its original location and is not included here.
'@ | Set-Content -LiteralPath (Join-Path $out 'README-Test.txt') -Encoding ASCII
Write-Host "Prepared isolated development test: $out"
Write-Host 'This is not a speed or audio acceptance build. The installed launcher is unchanged.'
