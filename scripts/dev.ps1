<#
.SYNOPSIS
    fastbus developer commands for Windows (PowerShell 5.1 or 7+).

.DESCRIPTION
    One entry point for the everyday workflow, so you never have to remember preset names
    or open a "Developer PowerShell" by hand. Presets that need the Visual Studio developer
    environment (clang-cl, Ninja, clang-tidy) get it automatically.

    Commands
      doctor        Check that the required tools are installed and the checkout is healthy
      presets       List the CMake presets available on this machine
      configure     cmake --preset <Preset>
      build         configure (if needed) + build
      test          ctest for <Preset> (writes build\<Preset>\junit.xml)
      all           configure + build + test                                  (default)
      asan          'all' with the windows-asan preset (MSVC AddressSanitizer + canary)
      tidy          build with clang-tidy on every file (windows-tidy preset)
      format        clang-format all sources in place
      lint          format check + file-length + layering checks (same as CI)
      bench         Release build + run the benchmarks
      check         everything CI checks on Windows: lint, all (Debug+Release), asan, tidy
      clean         delete build\<Preset>

.PARAMETER Preset
    Configure preset (default: windows-msvc). See `.\scripts\dev.ps1 presets`.

.PARAMETER Config
    Debug or Release. Only used by Visual Studio presets (windows-msvc).

.PARAMETER Fresh
    Re-run CMake configuration from scratch (cmake --fresh).

.EXAMPLE
    .\scripts\dev.ps1                       # build + test, MSVC Debug
.EXAMPLE
    .\scripts\dev.ps1 all -Config Release
.EXAMPLE
    .\scripts\dev.ps1 asan
.EXAMPLE
    .\scripts\dev.ps1 check                 # run before you push
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('doctor', 'presets', 'configure', 'build', 'test', 'all', 'asan', 'tidy',
                 'format', 'lint', 'bench', 'check', 'clean')]
    [string]$Command = 'all',

    [string]$Preset = 'windows-msvc',

    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',

    [switch]$Fresh
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot
# Windows PowerShell 5.1 has no $IsWindows; PowerShell 7 does. (Non-Windows hosts are only used
# to smoke-test this script; on them the VS environment steps are skipped.)
$OnWindows = ($PSVersionTable.PSEdition -eq 'Desktop') -or $IsWindows

# Presets that use the Ninja generator need cl/clang-cl/ninja on PATH (VS dev environment).
$NinjaPresets = @('windows-clangcl', 'windows-tidy')
# Visual Studio presets that have a separate "-release" build/test preset.
$MultiConfigWithRelease = @('windows-msvc')
$script:LlvmReported = $false

function Write-Step([string]$Text) {
    Write-Host ""
    Write-Host "==> $Text" -ForegroundColor Cyan
}

function Invoke-Native {
    param([Parameter(Mandatory)] [string]$Exe, [string[]]$Arguments = @())
    Write-Host "    $Exe $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "'$Exe $($Arguments -join ' ')' failed with exit code $LASTEXITCODE"
    }
}

function Get-VsInstallPath {
    if (-not $OnWindows) { return $null }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $path = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $path) { return $null }
    return ($path | Select-Object -First 1)
}

function Enter-VsDevEnvironment {
    if (-not $OnWindows) { return }
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }  # already inside one
    $vs = Get-VsInstallPath
    if (-not $vs) {
        throw "Visual Studio 2022 with the 'Desktop development with C++' workload was not found."
    }
    $module = Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
    Import-Module $module
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation `
        -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
    Set-Location $RepoRoot
}

function Assert-Uv {
    if (Get-Command uv -ErrorAction SilentlyContinue) { return }
    throw ("uv not found. Install it with: winget install astral-sh.uv " +
           "(then open a new terminal). See https://docs.astral.sh/uv/")
}

function Invoke-Python([string[]]$Arguments) {
    # All Python tooling runs through uv: it creates .venv from uv.lock on first use and puts
    # the pinned tools (clang-format 18, gcovr, pre-commit) on PATH for the script.
    Assert-Uv
    Invoke-Native 'uv' (@('run', '--frozen', 'python') + $Arguments)
}

function Get-BuildPresetName([string]$ConfigurePreset) {
    if ($MultiConfigWithRelease -contains $ConfigurePreset -and $Config -eq 'Release') {
        return "$ConfigurePreset-release"
    }
    return $ConfigurePreset
}

function Use-VsBundledLlvm {
    # clang-cl must match the MSVC standard library it compiles against: the STL rejects older
    # clang versions with "#error STL1000: Unexpected compiler version". A standalone LLVM
    # install on PATH (common on CI images and dev machines) can be older or newer than what
    # Visual Studio supports, so put VS's own bundled LLVM (clang-cl + clang-tidy) first.
    if (-not $OnWindows) { return }
    $vs = Get-VsInstallPath
    if (-not $vs) { return }
    $llvm = Join-Path $vs 'VC\Tools\Llvm\x64\bin'
    if (-not (Test-Path (Join-Path $llvm 'clang-cl.exe'))) {
        Write-Host ("warning: Visual Studio's clang-cl not found; install the 'C++ Clang " +
                    "Compiler for Windows' component. Falling back to clang-cl on PATH.") `
            -ForegroundColor Yellow
        return
    }
    if ((($env:PATH -split ';') | Select-Object -First 1) -ne $llvm) {
        $env:PATH = "$llvm;$env:PATH"
    }
}

function Initialize-Environment([string]$P) {
    # Ninja presets need the compilers on PATH; VS presets only need cmake/ctest, which a
    # Visual-Studio-only install keeps inside the developer environment as well.
    $needsCMake = -not (Get-Command ctest -ErrorAction SilentlyContinue)
    if ($NinjaPresets -contains $P -or $needsCMake) { Enter-VsDevEnvironment }
    if ($NinjaPresets -contains $P -and -not $script:LlvmReported) {
        Use-VsBundledLlvm
        foreach ($tool in @('clang-cl', 'clang-tidy')) {
            $cmd = Get-Command $tool -ErrorAction SilentlyContinue
            if ($cmd) {
                $ver = (& $cmd.Source --version | Select-String 'version' |
                        Select-Object -First 1)
                Write-Host "    using $tool $($cmd.Source)  ($ver)" -ForegroundColor DarkGray
            }
        }
        $script:LlvmReported = $true
    }
}

function Invoke-Configure([string]$P) {
    Initialize-Environment $P
    Write-Step "Configure ($P)"
    $cmakeArgs = @('--preset', $P)
    if ($Fresh) { $cmakeArgs += '--fresh' }
    Invoke-Native 'cmake' $cmakeArgs
}

function Invoke-Build([string]$P) {
    Initialize-Environment $P
    if ($Fresh -or -not (Test-Path (Join-Path $RepoRoot "build\$P\CMakeCache.txt"))) {
        Invoke-Configure $P
    }
    $bp = Get-BuildPresetName $P
    Write-Step "Build ($bp)"
    Invoke-Native 'cmake' @('--build', '--preset', $bp, '--parallel')
}

function Invoke-Test([string]$P) {
    Initialize-Environment $P
    $tp = Get-BuildPresetName $P
    Write-Step "Test ($tp)"
    Invoke-Native 'ctest' @('--preset', $tp, '--output-junit', 'junit.xml')
}

function Invoke-All([string]$P) {
    Invoke-Build $P
    Invoke-Test $P
}

function Invoke-Lint {
    Write-Step 'clang-format check (pinned clang-format 18 via uv)'
    Invoke-Python @('scripts/run_clang_format.py', '--check')
    Write-Step 'File-length policy'
    Invoke-Python @('scripts/check_file_length.py')
    Write-Step 'Layering rules'
    Invoke-Python @('scripts/check_layering.py')
}

function Invoke-Bench {
    $script:Config = 'Release'
    Invoke-Build 'windows-msvc'
    $binDir = Join-Path $RepoRoot 'build\windows-msvc\bin\Release'
    foreach ($exe in Get-ChildItem -Path $binDir -Filter 'fastbus_bm_*.exe' | Sort-Object Name) {
        Write-Step ('Benchmark: {0}' -f $exe.BaseName)
        Invoke-Native $exe.FullName @('--benchmark_min_time=0.05s')
    }
}

function Test-Tool([string]$Name, [string]$Hint) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) {
        Write-Host ("  [ok]      {0,-14} {1}" -f $Name, $cmd.Source) -ForegroundColor Green
        return $true
    }
    Write-Host ("  [missing] {0,-14} {1}" -f $Name, $Hint) -ForegroundColor Yellow
    return $false
}

function Invoke-Doctor {
    $problems = 0
    Write-Step 'Required'
    if (-not (Test-Tool 'git' 'winget install Git.Git')) { $problems++ }
    $vs = Get-VsInstallPath
    if ($vs) {
        Write-Host ("  [ok]      {0,-14} {1}" -f 'Visual Studio', $vs) -ForegroundColor Green
        Enter-VsDevEnvironment  # so tools bundled with VS (cmake, ninja, clang-*) are found
    } elseif ($OnWindows) {
        Write-Host "  [missing] Visual Studio 2022 with 'Desktop development with C++'" `
            -ForegroundColor Red
        $problems++
    }
    if (Test-Tool 'cmake' 'winget install Kitware.CMake  (or use the copy bundled with VS)') {
        $v = (& cmake --version | Select-Object -First 1) -replace '[^\d\.]', ''
        if ([version]$v -lt [version]'3.24') {
            Write-Host "            CMake $v is too old, need >= 3.24" -ForegroundColor Red
            $problems++
        }
    } else { $problems++ }
    if (Test-Tool 'uv' 'winget install astral-sh.uv   (manages Python + the pinned tools)') {
        & uv sync --frozen --quiet
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  [ok]      tool env       .venv synced from uv.lock" -ForegroundColor Green
        } else {
            Write-Host "  [error]   uv sync failed (see message above)" -ForegroundColor Red
            $problems++
        }
    } else { $problems++ }

    Write-Step 'Optional (needed for windows-clangcl / windows-tidy)'
    if ($vs -or -not $OnWindows) {
        Use-VsBundledLlvm
        $null = Test-Tool 'clang-cl' "VS Installer -> Modify -> 'C++ Clang Compiler for Windows'"
        $null = Test-Tool 'ninja' 'ships with VS (CMake tools component)'
        $null = Test-Tool 'clang-tidy' "VS 'C++ Clang tools' or: winget install LLVM.LLVM"
    }

    Write-Step 'Checkout'
    if ($RepoRoot -match 'OneDrive') {
        Write-Host "  [warn]    The repo is inside OneDrive ($RepoRoot)." -ForegroundColor Yellow
        Write-Host "            OneDrive syncs and locks build files, which causes slow builds" `
            -ForegroundColor Yellow
        Write-Host "            and random 'cannot open file' linker errors (LNK1104)." `
            -ForegroundColor Yellow
        Write-Host "            Recommended: move the repo to e.g. C:\dev\FastBus." `
            -ForegroundColor Yellow
    }
    if ($RepoRoot.Length -gt 60) {
        Write-Host "  [warn]    Repo path is $($RepoRoot.Length) characters; deep build paths" `
            -ForegroundColor Yellow
        Write-Host "            can hit Windows' 260-character limit. Prefer a short path." `
            -ForegroundColor Yellow
    }
    $longPaths = (& git config --get core.longpaths) 2>$null
    if ($longPaths -ne 'true') {
        Write-Host "  [tip]     git config --global core.longpaths true" -ForegroundColor DarkYellow
    }

    Write-Host ""
    if ($problems -eq 0) { Write-Host 'doctor: all required tools found.' -ForegroundColor Green }
    else { throw "doctor: $problems required item(s) missing (see above)." }
}

try {
    switch ($Command) {
        'doctor'    { Invoke-Doctor }
        'presets'   { Invoke-Native 'cmake' @('--list-presets=all') }
        'configure' { Invoke-Configure $Preset }
        'build'     { Invoke-Build $Preset }
        'test'      { Invoke-Test $Preset }
        'all'       { Invoke-All $Preset }
        'asan'      { Invoke-All 'windows-asan' }
        'tidy'      { Invoke-Build 'windows-tidy' }
        'format'    {
            Invoke-Python @('scripts/run_clang_format.py', '--fix')
        }
        'lint'      { Invoke-Lint }
        'bench'     { Invoke-Bench }
        'check'     {
            Invoke-Lint
            $script:Config = 'Debug';   Invoke-All 'windows-msvc'
            $script:Config = 'Release'; Invoke-All 'windows-msvc'
            Invoke-All 'windows-asan'
            Invoke-Build 'windows-tidy'
            Write-Host ""
            Write-Host 'check: all Windows checks passed. Linux-only checks (GCC, TSan, UBSan,' `
                -ForegroundColor Green
            Write-Host '       coverage) run in CI on push.' -ForegroundColor Green
        }
        'clean'     {
            $dir = Join-Path $RepoRoot "build\$Preset"
            if (Test-Path $dir) { Remove-Item -Recurse -Force $dir; Write-Host "removed $dir" }
            else { Write-Host "nothing to clean ($dir does not exist)" }
        }
    }
} catch {
    Write-Host ""
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
# Exit explicitly: otherwise the exit code of the last native command (e.g. `git config --get`
# returning 1 for an unset key) leaks out, and CI's PowerShell wrapper reports a failure.
exit 0
