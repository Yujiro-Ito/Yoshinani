# 1-A TSF スケルトン & COM 登録

## 概要

何も変換しない最小の TSF Text Input Processor（TIP）を COM in-proc サーバ（DLL）として実装し、
`regsvr32` で登録して **Windows の IME 一覧に「よしなに」を出す**。これが MAIN_SPEC Step1 の「最大の壁」の前半。

## 背景・目的

- TIP は全アプリのプロセス内にロードされる COM DLL。まず「OS に IME として認識される」ことが全ての前提。
- ここが通れば、以降は「preedit に入れる中身（1-B/1-C → 2 → 3）」を差し替えるだけになる。
- MAIN_SPEC §3「LLM を TIP に同居させない」を構造で守る: 本 DLL は薄いクライアントに徹する（1-A では変換に触れない）。

## 設計

### 実装するインターフェース / エクスポート（最小セット）

| 要素 | 役割 |
|--|--|
| `ITfTextInputProcessorEx` | TIP エントリ。`ActivateEx` / `Deactivate` |
| クラスファクトリ `IClassFactory` | CLSID から TIP インスタンス生成 |
| `DllGetClassObject` / `DllCanUnloadNow` | COM サーバ標準エクスポート |
| `DllRegisterServer` / `DllUnregisterServer` | 自己登録 / 解除 |

### CLSID / GUID

- TIP 用 CLSID を 1 つ生成（`Yoshinani` 識別子に対応）。`guidgen` 等で生成し定数化。
- 言語プロファイル GUID を 1 つ生成（profile GUID）。
- 内部名 `Yoshinani`、表示名「よしなに」、言語 = ja-JP（LANGID `0x0411`）。

### COM 自己登録（`DllRegisterServer` 内）

1. **CLSID 登録**: `HKCR\CLSID\{CLSID}\InProcServer32` に DLL パス、`ThreadingModel = Apartment`。
2. **TSF プロファイル登録**: `ITfInputProcessorProfiles::Register(CLSID)` → `AddLanguageProfile(CLSID, ja-JP, profileGUID, L"よしなに", ..., hIcon, ...)`。
3. **カテゴリ登録**: `ITfCategoryMgr::RegisterCategory` に最低 `GUID_TFCAT_TIP_KEYBOARD`。
   - モダン Windows 向けに以下も登録推奨（任意だが付けておくと挙動が安定）:
     `GUID_TFCAT_TIPCAP_SECUREMODE`, `GUID_TFCAT_TIPCAP_UIELEMENTENABLED`,
     `GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT`, `GUID_TFCAT_TIPCAP_COMLESS`,
     `GUID_TFCAT_TIPCAP_WOW16`, `GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT`,
     `GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT`。
4. `DllUnregisterServer` は上記を逆順に解除（`UnregisterCategory` / `RemoveLanguageProfile` / `Unregister` / CLSID 削除）。

### Activate / Deactivate の流れ（1-A 範囲）

```
ActivateEx(pThreadMgr, tfClientId, dwFlags)
   ├─ ITfThreadMgr を保持（ComPtr）、tfClientId を保持
   ├─ ITfThreadMgrEventSink を Advise（フォーカス追跡。実処理は 1-B）
   └─ ITfKeyEventSink を AdviseKeyEventSink（キー捕捉。実処理は 1-B/1-C）
Deactivate()
   └─ 上記 Advise を全て Unadvise し、保持した COM 参照を解放
```

> 1-A の時点では sink は登録するだけでよい（空実装で可）。「登録・活性化・解放が落ちない」ことが目的。

### レイヤ上の位置（0-A 準拠）

- すべて **infrastructure（`yoshinani.tsf`）**。core への依存はまだ無くてよい。
- COM 参照は `Microsoft::WRL::ComPtr` 等で infra 内に閉じ込める。

## 受け入れ条件（MAIN_SPEC §7 より）

- [ ] `regsvr32 yoshinani.dll` で登録でき、設定 > 言語 > IME 一覧に「よしなに」が表示される
- [ ] `regsvr32 /u yoshinani.dll` で綺麗に解除できる（レジストリに残骸を残さない）
- [ ] IME を「よしなに」に切替→別 IME へ切替→復帰しても落ちない（Activate/Deactivate の往復が安定）
- [ ] メモ帳等で選択しても（まだ何も起きないが）クラッシュしない

## 影響範囲

### 新規ファイル（`src/tsf/`）
- `dllmain.cpp`（DllMain / DllGetClassObject / DllCanUnloadNow / DllRegisterServer / DllUnregisterServer）
- `ClassFactory.{h,cpp}`
- `TextService.{h,cpp}`（`ITfTextInputProcessorEx` 実装の骨格）
- `Registration.{h,cpp}`（プロファイル/カテゴリ登録ロジック）
- `Guids.{h,cpp}`（CLSID / profile GUID 定数）
- `yoshinani.def`（0-B の EXPORTS）

## 依存関係（他SUBSPECとの関連）

| SUBSPEC | 関連 |
|--|--|
| 0-B | DLL ターゲット・`.def`・UNICODE 定義 |
| 1-B | ここで Advise した KeyEventSink / ThreadMgrEventSink の中身を実装 |
| 1-C | 確定処理の入口（KeyEventSink）を共有 |

## TBD（未確定事項）

- CLSID / profile GUID の実値（生成して `Guids.cpp` に固定）
- IME アイコン（`hIcon`）を v1 で用意するか（無しでも登録可）
- 管理者権限が要る登録範囲（HKCR 書き込み）と、ユーザ単位登録（per-user）の可否
- VS デバッグ手順（登録済み DLL をどのホストプロセスにアタッチしてデバッグするか。メモ帳 / `notepad.exe` 想定）の確定
