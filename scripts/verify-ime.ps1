<#
.SYNOPSIS
  よしなに IME 動作確認の「CLI 完結部分」: 環境チェック / ビルド / regsvr32 登録・解除。
  GUI 操作（メモ帳で実入力して preedit を目視）は .claude/skills/verify-ime が
  Windows-MCP / computer-use を使って行う。本スクリプトはその前段と後始末を担う。
.DESCRIPTION
  方針: 環境やツールが未整備なら「握りつぶさず」実行エラー(終了コード != 0)＋
  日本語メッセージでユーザに通知する（CLAUDE.md のフォールバック方針）。
.PARAMETER Action
  Check      : 動作確認に必要な前提が整っているかを点検（既定）
  Build      : DLL をビルド（build.ps1 を呼ぶ）
  Register   : ビルド済み DLL を regsvr32 で登録（管理者権限が必要）
  Unregister : regsvr32 /u で登録解除
.PARAMETER BuildType
  Debug | Release（既定 Debug）
.EXAMPLE
  pwsh -File scripts/verify-ime.ps1 -Action Check
  pwsh -File scripts/verify-ime.ps1 -Action Register   # 要管理者
#>
[CmdletBinding()]
param(
    [ValidateSet('Check', 'Build', 'Register', 'Unregister')]
    [string]$Action = 'Check',
    [ValidateSet('Debug', 'Release')]
    [string]$BuildType = 'Debug'
)

$ErrorActionPreference = 'Stop'
$root    = Split-Path -Parent $PSScriptRoot
$dllPath = Join-Path $root "build/ninja-$($BuildType.ToLower())/src/tsf/yoshinani.dll"

function Notify-Error($msg, [int]$code = 1) {
    Write-Host "‼ 動作確認エラー: $msg" -ForegroundColor Red
    exit $code
}
function Info($msg)  { Write-Host "  $msg" -ForegroundColor DarkGray }
function Ok($msg)    { Write-Host "✓ $msg" -ForegroundColor Green }
function Warn($msg)  { Write-Host "! $msg" -ForegroundColor Yellow }

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    return ([Security.Principal.WindowsPrincipal]$id).IsInRole(
        [Security.Principal.WindowsBuiltinRole]::Administrator)
}

# DLL が COM 登録に対応しているか（DllRegisterServer をエクスポートしているか）を dumpbin で確認。
# 1-A 未実装の間は未エクスポート → 「まだ登録できない」と明示通知する。
function Test-DllRegisterable($dll) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $vsPath = & $vswhere -latest -products * -property installationPath
    if (-not $vsPath) { return $null }
    $dumpbin = Get-ChildItem "$vsPath\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $dumpbin) { return $null }   # 判定不能
    $exports = & $dumpbin /exports $dll 2>$null
    return [bool]($exports -match 'DllRegisterServer')
}

switch ($Action) {

    'Build' {
        Info "build.ps1 で DLL をビルドします"
        & (Join-Path $PSScriptRoot 'build.ps1') -BuildType $BuildType -NoTest
        if ($LASTEXITCODE -ne 0) { Notify-Error "ビルドに失敗しました（build.ps1 の出力を確認）。" }
        if (-not (Test-Path $dllPath)) { Notify-Error "ビルドは通ったが DLL が見つかりません: $dllPath" }
        Ok "ビルド完了: $dllPath"
    }

    'Check' {
        $ready = $true

        # 1) ビルドツール
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path $vswhere) { Ok "VS Build Tools 検出" } else { Warn "VS Build Tools 未検出（build.ps1 が失敗します）"; $ready = $false }

        # 2) DLL の有無
        if (Test-Path $dllPath) {
            Ok "DLL あり: $dllPath"
            # 3) COM 登録対応（DllRegisterServer エクスポート）
            $reg = Test-DllRegisterable $dllPath
            if ($null -eq $reg)      { Warn "DllRegisterServer の有無を判定できませんでした（dumpbin 不在）" }
            elseif ($reg)            { Ok "DllRegisterServer をエクスポート済み → 登録可能" }
            else                     { Warn "DllRegisterServer 未エクスポート → まだ regsvr32 登録できません（1-A 未実装）"; $ready = $false }
        } else {
            Warn "DLL 未ビルド → 先に -Action Build を実行してください: $dllPath"; $ready = $false
        }

        # 4) 管理者権限（登録に必要）
        if (Test-Admin) { Ok "管理者権限あり（regsvr32 登録可）" } else { Warn "非管理者 → regsvr32 登録は失敗します。管理者シェルで実行してください。"; $ready = $false }

        Write-Host ""
        if ($ready) { Ok "動作確認の前提は整っています。スキル verify-ime で GUI 確認へ進めます。"; exit 0 }
        else        { Notify-Error "前提が未整備のため動作確認に進めません（上の ! を解消してください）。" }
    }

    'Register' {
        if (-not (Test-Path $dllPath)) { Notify-Error "DLL 未ビルド: $dllPath（-Action Build を先に）" }
        if (-not (Test-Admin))         { Notify-Error "管理者権限が必要です。管理者 PowerShell で再実行してください。" }
        $reg = Test-DllRegisterable $dllPath
        if ($reg -eq $false)           { Notify-Error "この DLL は DllRegisterServer を持ちません（1-A 未実装）。登録は 1-A 完了後に可能になります。" }
        Info "regsvr32 /s $dllPath"
        $p = Start-Process regsvr32.exe -ArgumentList '/s', "`"$dllPath`"" -Wait -PassThru -Verb RunAs
        if ($p.ExitCode -ne 0) { Notify-Error "regsvr32 登録に失敗（exit $($p.ExitCode)）。" }
        Ok "登録完了。設定 > 言語 > IME に「よしなに」が出るはずです。"
    }

    'Unregister' {
        if (-not (Test-Path $dllPath)) { Notify-Error "DLL が見つかりません: $dllPath" }
        Info "regsvr32 /u /s $dllPath"
        $p = Start-Process regsvr32.exe -ArgumentList '/u', '/s', "`"$dllPath`"" -Wait -PassThru -Verb RunAs
        if ($p.ExitCode -ne 0) { Notify-Error "regsvr32 解除に失敗（exit $($p.ExitCode)）。" }
        Ok "登録解除完了。"
    }
}
