param([string]$CompilerDirectory = 'C:\Program Files (x86)\Dev-Cpp\MinGW64\bin')
$ErrorActionPreference = 'Stop'
$folder = Split-Path -Parent $MyInvocation.MyCommand.Path
$compiler = Join-Path $CompilerDirectory 'g++.exe'
$resources = Join-Path $CompilerDirectory 'windres.exe'
foreach ($tool in @($compiler, $resources)) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "Missing build tool: $tool" }
}
$build = Join-Path $folder 'build'
$dist = Join-Path $folder 'dist'
New-Item -ItemType Directory -Path $build, $dist -Force | Out-Null
Push-Location $build
$previousPath = $env:PATH
$env:PATH = $CompilerDirectory + ';' + $env:PATH
try {
    & $compiler -std=gnu++11 -O2 -shared -static -s -o capture.dll ../src/capture_hook.cpp ../src/zlib_proxy.def -luser32
    if ($LASTEXITCODE -ne 0) { throw 'Capture DLL build failed' }
    & $compiler -std=gnu++11 -O2 -static -s -municode -mwindows -o Valorant.exe ../src/anchor.cpp -luser32 -lgdi32
    if ($LASTEXITCODE -ne 0) { throw 'Anchor build failed' }
    $utf8 = New-Object Text.UTF8Encoding $true
    [IO.File]::WriteAllText((Join-Path $build 'manage.ps1'), [IO.File]::ReadAllText((Join-Path $folder 'src\manage.ps1')), $utf8)
    & $resources --preprocessor=gcc.exe --preprocessor-arg=-E --preprocessor-arg=-xc --preprocessor-arg=-DRC_INVOKED -I . -i ../src/resources.rc -o resources.o
    if ($LASTEXITCODE -ne 0) { throw 'Resource build failed' }
    & $compiler -std=gnu++11 -O2 -static -s -municode -mwindows -o ../dist/EciyuanDesktopCompanion.exe ../src/launcher.cpp resources.o -luser32 -lgdi32 -lgdiplus -lshell32 -lcomdlg32 -lole32
    if ($LASTEXITCODE -ne 0) { throw 'Launcher build failed' }
} finally { $env:PATH = $previousPath; Pop-Location }
Get-FileHash -LiteralPath (Join-Path $dist 'EciyuanDesktopCompanion.exe') -Algorithm SHA256
