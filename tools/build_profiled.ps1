param(
    [Parameter(Mandatory=$true)][string[]]$Roms,
    [string]$Compiler = 'g++',
    [UInt64]$Steps = 600000000,
    [UInt64[]]$StepsPerRom = @(),
    [string]$DeviceSettings = '',
    [string]$InputScript = '',
    [ValidateRange(1,16)][int]$CpuStepsPerTick = 2,
    [ValidateSet('O2','O3')][string]$Optimization = 'O2',
    [ValidateSet('native','x86-64')][string]$Architecture = 'native',
    [ValidateSet('x64','x86')][string]$Target = 'x64',
    [switch]$LegacyWindows,
    [string]$ResourceCompiler = 'windres',
    [switch]$Unity,
    [switch]$Gui,
    [switch]$Install,
    [ValidateSet('legacy','auto')][string]$Timing = 'legacy'
)
$ErrorActionPreference = 'Stop'
if (!$Steps) { throw 'Steps must be positive.' }
if ($StepsPerRom.Count -and ($StepsPerRom.Count -ne $Roms.Count -or $StepsPerRom -contains 0)) {
    throw 'StepsPerRom must contain one positive instruction limit per ROM.'
}
$projectRoot = Split-Path -Parent $PSScriptRoot
$romPaths = @($Roms | ForEach-Object { (Resolve-Path -LiteralPath $_).Path })
if ($InputScript) { $InputScript = (Resolve-Path -LiteralPath $InputScript).Path }
if ($DeviceSettings) { $DeviceSettings = (Resolve-Path -LiteralPath $DeviceSettings).Path }
$outputRelative = 'build/pgo-' + [Guid]::NewGuid().ToString('N')
$outputRoot = Join-Path $projectRoot $outputRelative
$profileRoot = Join-Path $outputRoot 'data'
New-Item -ItemType Directory -Force -Path $profileRoot | Out-Null
$executable = Join-Path $outputRoot 'oceanblast_profiled.exe'
$sources = @('src/memory/bus.cpp','src/cpu/arm920t.cpp','src/cartridge/cart_parser.cpp','src/display/display_win32.cpp','src/audio/audio_win32.cpp','src/main.cpp')
$unityFlags = @()
if ($Unity) {
    $unitySource = Join-Path $outputRoot 'unity.cpp'
    ($sources | ForEach-Object { '#include "../../' + $_ + '"' }) | Set-Content -LiteralPath $unitySource -Encoding utf8
    $sources = @($outputRelative + '/unity.cpp')
    $unityFlags = @('--param','inline-unit-growth=400','--param','large-unit-insns=100000','--param','max-inline-insns-single=1000','--param','max-inline-insns-auto=200')
}
$resObj = $outputRelative + '/oceanblast_res.o'
$architectureFlags = @('-march=' + $Architecture)
if ($Target -eq 'x86') { $architectureFlags = @('-m32','-march=i686','-msse2','-mfpmath=sse') }
if ($LegacyWindows) { $architectureFlags += @('-DOCEANBLAST_LEGACY_WINDOWS','-D_WIN32_WINNT=0x0601','-DWINVER=0x0601') }
$graphicsLibraries = @('-ld3d11','-ldxgi','-ld3dcompiler')
if ($LegacyWindows) { $graphicsLibraries = @() }
$common = @('-std=c++17','-Wall','-Wextra',('-' + $Optimization),'-Isrc') + $architectureFlags + $unityFlags + $sources + @($resObj,'-lgdi32','-luser32','-lwinmm','-lcomdlg32','-lcomctl32','-lshell32','-lole32','-ldinput8','-ldxguid','-lgdiplus') + $graphicsLibraries + @('-o',($outputRelative + '/oceanblast_profiled.exe'))
$profileOption = $profileRoot.Replace('\','/')
# Keep the counter filename relative to the project rather than embedding its
# full path again below profileRoot. Both compiler phases use the same prefix.
$profileSourcePrefix = $projectRoot
$common += @('-fprofile-prefix-path=' + $profileSourcePrefix)
Push-Location $projectRoot
try {
    $resourceFlags = @()
    if ($Target -eq 'x86') { $resourceFlags = @('-F','pe-i386') }
    & $ResourceCompiler src/display/oceanblast.rc -O coff @resourceFlags -o $resObj
    if ($LASTEXITCODE) { throw 'Resource compilation failed.' }
    & $Compiler @common ('-fprofile-generate=' + $profileOption)
    if ($LASTEXITCODE) { throw 'Instrumented compilation failed.' }
} finally { Pop-Location }
$index = 0
foreach ($rom in $romPaths) {
    $trainingSteps = if ($StepsPerRom.Count) { $StepsPerRom[$index] } else { $Steps }
    $sessionRoot = Join-Path $outputRoot ('session-' + $index++)
    New-Item -ItemType Directory -Force -Path $sessionRoot | Out-Null
    $arguments = @($rom,'--steps',$trainingSteps.ToString())
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
if ($Install) {
    $installedName = 'oceanblast.exe'
    if ($Target -eq 'x86') { $installedName = 'oceanblast-x86.exe' }
    elseif ($LegacyWindows) { $installedName = 'oceanblast-legacy-x64.exe' }
    $installPath = Join-Path $projectRoot ('bin/' + $installedName)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $installPath) | Out-Null
    Copy-Item -LiteralPath $executable -Destination $installPath -Force
    Write-Output "Installed: $installPath"
}
Write-Output $executable
