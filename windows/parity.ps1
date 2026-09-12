<#
.SYNOPSIS
    Measures whether the Windows build of the DSP produces the same audio as
    the reference GCC build.

.DESCRIPTION
    This is the Windows half of the project's parity claim. It renders the
    shared probe (cpp/tests/dsp_probe.cpp -- the same file the Linux and
    Android builds use) with two compilers and compares the raw f32 output.

    Reference : GCC, -std=c++20 -O3 -ffast-math -fno-math-errno
                (the flags in cpp/Makefile)
    Candidate : MSVC, the flags in windows/CMakeLists.txt

    The bar is the one Android hit against the desktop build:
        correlation 1.000000, largest sample difference 0.000006

    A GCC for Windows is needed for the reference side. MSYS2's ucrt64 g++
    works and is what this was developed against; set -Gxx to point elsewhere.

.EXAMPLE
    .\parity.ps1
    .\parity.ps1 -Gxx C:\msys64\ucrt64\bin\g++.exe
#>
[CmdletBinding()]
param(
    [string]$Gxx    = 'C:\msys64\ucrt64\bin\g++.exe',
    [string]$Python = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe",
    [string]$Probe  = "$PSScriptRoot\build\Release\dsp_probe.exe",
    [string]$WorkDir = "$PSScriptRoot\..\.parity"
)

$ErrorActionPreference = 'Stop'
$repo = Resolve-Path "$PSScriptRoot\.."

$modes = @('circular','pingpong','pendulum','linear','figure8','spiral','random','static')

function Need($path, $what) {
    if (-not (Test-Path $path)) { throw "$what not found at $path" }
}

Need $Gxx    'g++ (reference compiler)'
Need $Python 'python (for compare_f32.py)'
Need $Probe  'dsp_probe.exe -- build it first: cmake --build build --config Release'

$refDir  = Join-Path $WorkDir 'gcc'
$candDir = Join-Path $WorkDir 'msvc'

# Wipe first. compare_f32.py globs whatever *.f32 it finds, so a dump left by an
# earlier run with different flags would be silently folded into the result --
# which is exactly how a "failure" that was really a stale file wastes an hour.
Remove-Item -Recurse -Force $refDir, $candDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $refDir, $candDir | Out-Null

Write-Host "Building the reference with GCC..."
# g++ spawns cc1plus, which links against the toolchain's own libstdc++ and
# libgcc DLLs. Without its bin directory on PATH those fail to load and g++
# exits 1 having printed nothing at all -- which reads as a mystery, not a
# missing PATH. So put it there.
$env:PATH = (Split-Path $Gxx) + [IO.Path]::PathSeparator + $env:PATH

$refExe = Join-Path $WorkDir 'dsp_probe_gcc.exe'
& $Gxx -std=c++20 -O3 -ffast-math -fno-math-errno -D_USE_MATH_DEFINES `
       -I "$repo\cpp\src\dsp" `
       "$repo\cpp\tests\dsp_probe.cpp" "$repo\cpp\src\dsp\Processor.cpp" `
       -o $refExe
if ($LASTEXITCODE -ne 0) { throw "reference build failed" }

Write-Host "Rendering $($modes.Count) modes with each build..."
# The probe prints its timing to stderr. Windows PowerShell wraps any stderr
# from a native command in an ErrorRecord, which -ErrorActionPreference Stop
# then treats as fatal -- so a probe that ran perfectly would abort the script.
# Relax it for the render loop only.
$prevEap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    foreach ($m in $modes) {
        cmd /c "`"$refExe`" $m `"$(Join-Path $refDir  "$m.f32")`" 2>nul"
        cmd /c "`"$Probe`"  $m `"$(Join-Path $candDir "$m.f32")`" 2>nul"
    }
} finally {
    $ErrorActionPreference = $prevEap
}

Write-Host ""
& $Python "$repo\cpp\tests\compare_f32.py" $refDir $candDir
