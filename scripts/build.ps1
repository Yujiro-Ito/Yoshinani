<#
.SYNOPSIS
  よしなに 自己完結ビルドループ: vcvars 読込 → CMake configure → Ninja build → ctest。
.DESCRIPTION
  シェル状態は呼び出し間で消えるため、毎回 MSVC 環境(vcvars64)をこのプロセスに取り込む。
  CMake / Ninja は VS Build Tools 同梱のものを PATH に通して使う。
  Visual Studio IDE は不要（完全 CLI 完結）。
.PARAMETER Config   Debug | Release（既定 Debug）
.PARAMETER Clean    ビルドディレクトリを消してから構成し直す
.PARAMETER NoTest   ctest を実行しない（ビルドのみ）
.EXAMPLE
  pwsh -File scripts/build.ps1            # 構成→ビルド→テスト
  pwsh -File scripts/build.ps1 -Clean     # クリーンビルド
  pwsh -File scripts/build.ps1 -NoTest    # ビルドのみ
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$BuildType = 'Debug',
    [switch]$Clean,
    [switch]$NoTest
)

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build/ninja-$($BuildType.ToLower())"

function Fail($msg) {
    Write-Host "‼ 動作環境エラー: $msg" -ForegroundColor Red
    exit 1
}

# 1. MSVC (vcvars64) を vswhere で探す
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    Fail "vswhere が見つかりません。Visual Studio Build Tools（C++ ワークロード）を入れてください。"
}
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { $vsPath = & $vswhere -latest -products * -property installationPath }
if (-not $vsPath) { Fail "MSVC (VC++ ツール) が見つかりません。VS Build Tools を入れてください。" }

$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { Fail "vcvars64.bat が見つかりません: $vcvars" }

# 2. vcvars の環境変数をこのセッションへ取り込む
Write-Host "→ MSVC 環境を読込: $vsPath" -ForegroundColor Cyan
cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($matches[1])" -Value $matches[2] }
}

# 3. CMake / Ninja を PATH へ
#    優先: pip --user 導入の現行版（VS同梱の Microsoft パッチ版 CMake 3.20 は
#    Ninja 生成にバグがあるため避ける）。不在なら requirements から自動導入し、
#    それも無理なら VS 同梱版へフォールバック。
function Get-PipCmakeScripts {
    Get-ChildItem "$env:APPDATA\Python\Python*\Scripts" -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName 'cmake.exe') } |
        Select-Object -First 1 -ExpandProperty FullName
}

$pipScripts = Get-PipCmakeScripts

# ブートストラップ: cmake が無ければ requirements-build.txt から pip で自動導入
if (-not $pipScripts) {
    $py = Get-Command py -ErrorAction SilentlyContinue
    if (-not $py) { $py = Get-Command python -ErrorAction SilentlyContinue }
    $req = Join-Path $PSScriptRoot 'requirements-build.txt'
    if ($py -and (Test-Path $req)) {
        Write-Host "→ ビルドツール(cmake/ninja)が無いため pip で自動導入: $req" -ForegroundColor Cyan
        & $py.Source -m pip install --user -r $req
        $pipScripts = Get-PipCmakeScripts
    } elseif (-not $py) {
        Write-Host "  ! Python が無いため cmake/ninja の自動導入をスキップ（要 Python or VS同梱CMake）" -ForegroundColor Yellow
    }
}

if ($pipScripts) {
    $env:PATH = "$pipScripts;$env:PATH"
} else {
    Write-Host "  ! 現行版 CMake が無いため VS 同梱版へフォールバック（VS2019同梱は不具合あり）" -ForegroundColor Yellow
    $cmakeBin = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
    $ninjaBin = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
    $env:PATH = "$cmakeBin;$ninjaBin;$env:PATH"
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { Fail "cmake が見つかりません。`pip install cmake` を実行してください。" }
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) { Fail "ninja が見つかりません。`pip install ninja` を実行してください。" }
Write-Host "  cmake: $((Get-Command cmake).Source)" -ForegroundColor DarkGray

# 4. クリーン
if ($Clean -and (Test-Path $build)) {
    Write-Host "→ クリーン: $build" -ForegroundColor Cyan
    Remove-Item -Recurse -Force $build
}

# 5. configure → build → test
#    注意: native 引数内の $var は裸だと展開されないことがあるため、必ずダブルクォートで構築する。
Write-Host "→ configure ($BuildType)" -ForegroundColor Cyan
cmake -S $root -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$BuildType"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "→ build" -ForegroundColor Cyan
cmake --build $build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not $NoTest) {
    Write-Host "→ ctest" -ForegroundColor Cyan
    ctest --test-dir $build --output-on-failure
    exit $LASTEXITCODE
}

Write-Host "✓ 完了（ビルドのみ）" -ForegroundColor Green
