param(
    [Parameter(Mandatory=$true)][string[]]$Roms,
    [string]$Compiler = 'g++',
    [UInt64]$Steps = 600000000,
    [string]$DeviceSettings = '',
    [string]$InputScript = '',
    [ValidateRange(1,16)][int]$CpuStepsPerTick = 2,
    [ValidateSet('O2','O3')][string]$Optimization = 'O2',
    [switch]$Unity,
    [switch]$Gui,
    [ValidateSet('legacy','auto')][string]$Timing = 'legacy'
)
$ErrorActionPreference = 'Stop'
if (!$Steps) { throw 'Steps must be positive.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$romPaths = @($Roms | ForEach-Object { (Resolve-Path -LiteralPath $_).Path })
if ($InputScript) { $InputScript = (Resolve-Path -LiteralPath $InputScript).Path }
if ($DeviceSettings) { $DeviceSettings = (Resolve-Path -LiteralPath $DeviceSettings).Path }
$outputRelative = 'build/pgo-' + [Guid]::NewGuid().ToString('N')
$outputRoot = Join-Path $projectRoot $outputRelative
$profileRoot = Join-Path $outputRoot 'data'
New-Item -ItemType Directory -Force -Path $profileRoot | Out-Null
$executable = Join-Path $outputRoot 'oceanblast_profiled.exe'
$sources = @('src/main.cpp','src/cpu/arm920t.cpp','src/memory/bus.cpp','src/cartridge/cart_parser.cpp','src/display/display_win32.cpp','src/audio/audio_win32.cpp')
if ($Unity) {
    $unitySource = Join-Path $outputRoot 'unity.cpp'
    ($sources | ForEach-Object { '#include "../../' + $_ + '"' }) | Set-Content -LiteralPath $unitySource -Encoding utf8
    $sources = @($outputRelative + '/unity.cpp')
}
$common = @('-std=c++17','-Wall','-Wextra',('-' + $Optimization),'-Isrc') + $sources + @('-lgdi32','-luser32','-lwinmm','-lcomdlg32','-ld3d11','-ldxgi','-ld3dcompiler','-o',($outputRelative + '/oceanblast_profiled.exe'))
$profileOption = $profileRoot.Replace('\','/')
Push-Location $projectRoot
try {
    & $Compiler @common ('-fprofile-generate=' + $profileOption)
    if ($LASTEXITCODE) { throw 'Instrumented compilation failed.' }
} finally { Pop-Location }
$index = 0
foreach ($rom in $romPaths) {
    $sessionRoot = Join-Path $outputRoot ('session-' + $index++)
    New-Item -ItemType Directory -Force -Path $sessionRoot | Out-Null
    $arguments = @($rom,'--steps',$Steps.ToString())
    if ($Gui) { $arguments += @('--gui','--exit-on-limit') }
    if ($Timing -eq 'auto') { $arguments += @('--timing','auto') }
    else { $arguments += @('--cpu-steps-per-tick',$CpuStepsPerTick.ToString()) }
    if ($InputScript) { $arguments += @('--input-script',$InputScript) }
    if ($DeviceSettings) {
        $image = Join-Path $sessionRoot 'board.nvram'
        Copy-Item -LiteralPath $DeviceSettings -Destination $image
        $arguments += @('--nvram',$image)
    }
    Push-Location $sessionRoot
    try {
        & $executable @arguments *> training.log
        if ($LASTEXITCODE) { throw 'Cartridge training run failed.' }
    } finally { Pop-Location }
}
Push-Location $projectRoot
try {
    & $Compiler @common ('-fprofile-use=' + $profileOption) '-fprofile-correction' '-Werror=missing-profile'
    if ($LASTEXITCODE) { throw 'Profile-guided compilation failed.' }
} finally { Pop-Location }
# Profiles and guest dumps remain in ignored build/. Installation is separate.
Write-Output $executable
