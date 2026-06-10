// 1-A/1-B/1-C/4-A/4-B — TIP 本体実装。
#include "TextService.h"
#include "DisplayAttribute.h"
#include "EditSession.h"
#include "KeyMap.h"
#include "OllamaKanaKanjiConverter.h"
#include "OpenAiKanaKanjiConverter.h"
#include "application/PreeditView.h"
#include "domain/TriggerPolicy.h"
#include <shlobj.h>
#include <fstream>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <utility>

using yoshinani::core::application::BuildPreeditView;
using yoshinani::core::application::ConversionRequest;
using yoshinani::core::application::ConvState;
using yoshinani::core::domain::ConversionResult;
using yoshinani::core::domain::Decide;
using yoshinani::core::domain::InputAction;
using yoshinani::core::domain::KeyKind;
using yoshinani::core::domain::RequestId;

namespace {

// 修飾キーが押されているか（Ctrl/Alt 併用はショートカット扱いで素通し）。
bool IsDown(int vk) { return (GetKeyState(vk) & 0x8000) != 0; }

// VK を core の KeyKind に正規化し、文字を伴う場合は out_ch に返す（infra の責務）。
//   どの VK が確定トリガーかは triggers（設定由来）で決まる。
//   文字は ToUnicodeEx（KeyMap::VkToChar）で取得し、Shift/CapsLock/レイアウトを反映する（R1/R2）。
KeyKind Classify(WPARAM vk, LPARAM lParam, const std::set<WPARAM>& triggers, wchar_t& out_ch) {
    out_ch = 0;
    if (IsDown(VK_CONTROL) || IsDown(VK_MENU)) return KeyKind::Other;  // Ctrl/Alt は触らない

    if (triggers.count(vk) != 0) return KeyKind::Trigger;              // 確定トリガー（設定・既定 Tab）

    switch (vk) {
        case VK_RETURN: return KeyKind::Enter;                  // 生確定（preedit 中のみ消費）
        case VK_SPACE:  out_ch = L' '; return KeyKind::Space;   // 区切り空白（分かち書き用）
        case VK_BACK:   return KeyKind::Backspace;
        case VK_ESCAPE: return KeyKind::Escape;
        default:        break;
    }

    // 英字・数字・記号は打鍵文字をそのまま preedit に入れる（大小・記号は LLM へ忠実に渡す）。
    // 矢印・F キー等は文字にならないので自動的に素通し。
    out_ch = VkToChar(vk, lParam);
    return (out_ch != 0) ? KeyKind::Character : KeyKind::Other;
}

}  // namespace

CTextService::CTextService()
    : m_cRef(1), m_pThreadMgr(nullptr), m_tfClientId(TF_CLIENTID_NULL),
      m_pComposition(nullptr), m_pContext(nullptr),
      m_queue(8) {  // 4-A: 変換待ちは最大8（満杯時の Tab は無視＝打鍵継続）
    DllAddRef();
}

CTextService::~CTextService() {
    DllRelease();
}

// ---- IUnknown ----------------------------------------------------------------

STDMETHODIMP CTextService::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfTextInputProcessor)) {
        *ppv = static_cast<ITfTextInputProcessor*>(this);
    } else if (IsEqualIID(riid, IID_ITfTextInputProcessorEx)) {
        *ppv = static_cast<ITfTextInputProcessorEx*>(this);
    } else if (IsEqualIID(riid, IID_ITfKeyEventSink)) {
        *ppv = static_cast<ITfKeyEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfCompositionSink)) {
        *ppv = static_cast<ITfCompositionSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider)) {
        *ppv = static_cast<ITfDisplayAttributeProvider*>(this);
    } else if (IsEqualIID(riid, IID_ITfCompartmentEventSink)) {
        *ppv = static_cast<ITfCompartmentEventSink*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CTextService::AddRef() {
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CTextService::Release() {
    LONG cr = InterlockedDecrement(&m_cRef);
    if (cr == 0) delete this;
    return cr;
}

// ---- 設定読込 ----------------------------------------------------------------

namespace {

// 1ファイルを文字列に読む（存在しない/開けないなら空文字を返す）。
std::string ReadAllText(const std::wstring& path) {
    std::ifstream f(path.c_str());  // MSVC: wchar_t* パス可
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// %APPDATA%\yoshinani\settings.json のパスを返す（取得失敗時は空）。
// タスクトレイ UI が書き込む正規の場所。DLL 同居 settings.json はフォールバック。
std::wstring AppDataSettingsPath() {
    PWSTR roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) return {};
    std::wstring p(roaming);
    CoTaskMemFree(roaming);
    p += L"\\yoshinani\\settings.json";
    return p;
}

// DLL と同居する settings.json のパス（旧仕様。フォールバック用）。
std::wstring DllSettingsPath() {
    WCHAR dllPath[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_hInst, dllPath, ARRAYSIZE(dllPath));
    if (n == 0 || n >= ARRAYSIZE(dllPath)) return {};
    std::wstring path(dllPath);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    return path.substr(0, slash + 1) + L"settings.json";
}

}  // namespace

// settings.json を読む。優先順位:
//   1) %APPDATA%\yoshinani\settings.json（タスクトレイ UI の書き込み先・正規）
//   2) DLL と同じディレクトリの settings.json（旧仕様・移行期のフォールバック）
//   3) 既定値（Tab / openai gpt-5.4-mini low）
yoshinani::core::application::Settings CTextService::LoadSettings() const {
    using yoshinani::core::application::ParseSettings;
    using yoshinani::core::application::Settings;

    if (const auto appData = AppDataSettingsPath(); !appData.empty()) {
        const auto text = ReadAllText(appData);
        if (!text.empty()) return ParseSettings(text);
    }
    if (const auto dllSide = DllSettingsPath(); !dllSide.empty()) {
        const auto text = ReadAllText(dllSide);
        if (!text.empty()) return ParseSettings(text);
    }
    return Settings{};
}

// 設定のキー名を VK へ写像（トリガー設定の分離点）。
std::set<WPARAM> CTextService::LoadTriggerVKs(
        const yoshinani::core::application::Settings& settings) const {
    std::set<WPARAM> vks;
    for (const std::string& name : settings.triggerKeys) {
        if (auto vk = KeyNameToVk(name)) {
            // Space は分かち書きの区切り専用。トリガーには使わせない
            // （設定で "Space" を指定しても区切り機能を優先）。
            if (*vk == static_cast<WPARAM>(VK_SPACE)) continue;
            // Enter は生確定（CommitRaw）専用。トリガーにすると変換確定に
            // 化けて Google IME 準拠の挙動が壊れるため除外（1-C 拡張）。
            if (*vk == static_cast<WPARAM>(VK_RETURN)) continue;
            vks.insert(*vk);
        }
    }
    if (vks.empty()) vks.insert(static_cast<WPARAM>(VK_TAB));  // 安全側（既定トリガー）
    return vks;
}

// モード切替キー（1-D）。半角/全角はアプリ/状況により VK_KANJI(0x19) /
// VK_OEM_AUTO(0xF3) / VK_OEM_ENLW(0xF4) のいずれで届くか揺れるため、
// "Kanji" 指定時は 3 つともエイリアスとして登録する。
std::set<WPARAM> CTextService::LoadModeToggleVKs(
        const yoshinani::core::application::Settings& settings) const {
    std::set<WPARAM> vks;
    for (const std::string& name : settings.modeToggleKeys) {
        if (auto vk = KeyNameToVk(name)) vks.insert(*vk);
    }
    if (vks.empty()) vks.insert(static_cast<WPARAM>(VK_KANJI));  // 安全側（既定 = 半角/全角）
    if (vks.count(static_cast<WPARAM>(VK_KANJI)) != 0) {
        vks.insert(static_cast<WPARAM>(VK_OEM_AUTO));
        vks.insert(static_cast<WPARAM>(VK_OEM_ENLW));
    }
    return vks;
}

// 「直接→変換」遷移キー（Google 日本語入力流。空＝指定なし、その場合 modeToggle のみ機能）。
std::set<WPARAM> CTextService::LoadConversionOnVKs(
        const yoshinani::core::application::Settings& settings) const {
    std::set<WPARAM> vks;
    for (const std::string& name : settings.conversionOnKeys) {
        if (auto vk = KeyNameToVk(name)) vks.insert(*vk);
    }
    return vks;
}

// 「変換→直接」遷移キー。
std::set<WPARAM> CTextService::LoadDirectOnVKs(
        const yoshinani::core::application::Settings& settings) const {
    std::set<WPARAM> vks;
    for (const std::string& name : settings.directOnKeys) {
        if (auto vk = KeyNameToVk(name)) vks.insert(*vk);
    }
    return vks;
}

// 変換バックエンドの生成。model 空文字は「バックエンド既定」（Settings.h）。
// A/B 実測（2026-06-10）より既定はクラウド openai gpt-5.4-mini + low
// （空白なしローマ字を実質解決。ローカル派は settings.json で "ollama" に切替）。
std::shared_ptr<yoshinani::core::domain::IKanaKanjiConverter> CTextService::CreateConverter(
        const yoshinani::core::application::ConverterSettings& cs) const {
    if (cs.backend == "ollama") {
        if (cs.model.empty()) return std::make_shared<yoshinani::ipc::OllamaKanaKanjiConverter>();
        return std::make_shared<yoshinani::ipc::OllamaKanaKanjiConverter>(L"localhost", 11434,
                                                                          cs.model);
    }
    if (cs.model.empty()) {
        return std::make_shared<yoshinani::ipc::OpenAiKanaKanjiConverter>("gpt-5.4-mini",
                                                                          cs.reasoningEffort);
    }
    return std::make_shared<yoshinani::ipc::OpenAiKanaKanjiConverter>(cs.model, cs.reasoningEffort);
}

// ---- 活性化 ------------------------------------------------------------------

STDMETHODIMP CTextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD /*dwFlags*/) {
    m_pThreadMgr = ptim;
    if (m_pThreadMgr) m_pThreadMgr->AddRef();
    m_tfClientId = tid;
    const auto settings = LoadSettings();
    m_triggerVKs      = LoadTriggerVKs(settings);
    m_modeToggleVKs   = LoadModeToggleVKs(settings);
    m_conversionOnVKs = LoadConversionOnVKs(settings);
    m_directOnVKs     = LoadDirectOnVKs(settings);
    m_converter     = CreateConverter(settings.converter);

    // 1-D: open/close コンパートメントを初期化（open=変換が既定）し、変更を購読する。
    InitOpenCloseCompartment();

    // 4-A: ワーカー→TIP スレッドのマーシャラ（結果は OnConvertResult に届く）。
    m_marshaller.Start([this](RequestId id, ConversionResult r) {
        OnConvertResult(id, std::move(r));
    });

    // 4-B: 表示属性 GUID を TfGuidAtom 化（GUID_PROP_ATTRIBUTE の値として使う）。
    ITfCategoryMgr* pCat = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfCategoryMgr, reinterpret_cast<void**>(&pCat)))) {
        pCat->RegisterGUID(yoshinani::tsf::GUID_YoshinaniDaInput, &m_attrInput);
        pCat->RegisterGUID(yoshinani::tsf::GUID_YoshinaniDaConverting, &m_attrConverting);
        pCat->Release();
    }

    if (!InitKeyEventSink()) {
        Deactivate();
        return E_FAIL;
    }
    return S_OK;
}

STDMETHODIMP CTextService::Activate(ITfThreadMgr* ptim, TfClientId tid) {
    return ActivateEx(ptim, tid, 0);
}

STDMETHODIMP CTextService::Deactivate() {
    UninitOpenCloseCompartment();
    UninitKeyEventSink();
    m_marshaller.Stop();  // 以後ワーカーの結果は捨てられる（OnConvertResult は来ない）
    if (m_pComposition) {
        m_pComposition->Release();
        m_pComposition = nullptr;
    }
    ReleaseContext();
    m_queue.Clear();
    m_session.Clear();
    m_converter.reset();  // 実行中ワーカーは shared_ptr 共有で生存（4-A）
    if (m_pThreadMgr) {
        m_pThreadMgr->Release();
        m_pThreadMgr = nullptr;
    }
    m_tfClientId = TF_CLIENTID_NULL;
    return S_OK;
}

BOOL CTextService::InitKeyEventSink() {
    if (!m_pThreadMgr) return FALSE;
    ITfKeystrokeMgr* pksm = nullptr;
    if (FAILED(m_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&pksm)))) {
        return FALSE;
    }
    HRESULT hr = pksm->AdviseKeyEventSink(m_tfClientId, static_cast<ITfKeyEventSink*>(this), TRUE);
    pksm->Release();
    return SUCCEEDED(hr);
}

void CTextService::UninitKeyEventSink() {
    if (!m_pThreadMgr) return;
    ITfKeystrokeMgr* pksm = nullptr;
    if (SUCCEEDED(m_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&pksm)))) {
        pksm->UnadviseKeyEventSink(m_tfClientId);
        pksm->Release();
    }
}

// ---- context 保持（4-A: 結果到着時に edit session を要求するため） -------------

void CTextService::RetainContext(ITfContext* pic) {
    if (m_pContext == pic) return;
    if (m_pContext) m_pContext->Release();
    m_pContext = pic;
    if (m_pContext) m_pContext->AddRef();
}

void CTextService::ReleaseContext() {
    if (m_pContext) {
        m_pContext->Release();
        m_pContext = nullptr;
    }
}

// ---- 1-D: open/close コンパートメント同期 -------------------------------------

void CTextService::InitOpenCloseCompartment() {
    if (!m_pThreadMgr) return;
    ITfCompartmentMgr* pMgr = nullptr;
    if (FAILED(m_pThreadMgr->QueryInterface(IID_ITfCompartmentMgr,
                                            reinterpret_cast<void**>(&pMgr)))) {
        return;
    }
    if (SUCCEEDED(pMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &m_pOpenClose)) &&
        m_pOpenClose) {
        // activate 時は常に変換モード（open）で開始する — 1-D 確定事項。
        // open/close はキーボードスレッド共有のため、直前の別 IME が残した close 値を
        // 引き継ぐと「よしなにを選んだのに全キー素通し＝動かない」に見える。
        // IME を選んだ＝変換意思とみなし、自分が activate されたら変換で開始する。
        m_mode.Set(yoshinani::core::application::InputMode::Conversion);
        SetOpenCloseCompartment(true);
        // 言語バー/アプリ側からの open/close 変更にも追従する（モード状態の真実の源）。
        ITfSource* pSource = nullptr;
        if (SUCCEEDED(m_pOpenClose->QueryInterface(IID_ITfSource,
                                                   reinterpret_cast<void**>(&pSource)))) {
            pSource->AdviseSink(IID_ITfCompartmentEventSink,
                                static_cast<ITfCompartmentEventSink*>(this), &m_openCloseCookie);
            pSource->Release();
        }
    }
    pMgr->Release();
}

void CTextService::UninitOpenCloseCompartment() {
    if (m_pOpenClose) {
        if (m_openCloseCookie != TF_INVALID_COOKIE) {
            ITfSource* pSource = nullptr;
            if (SUCCEEDED(m_pOpenClose->QueryInterface(IID_ITfSource,
                                                       reinterpret_cast<void**>(&pSource)))) {
                pSource->UnadviseSink(m_openCloseCookie);
                pSource->Release();
            }
            m_openCloseCookie = TF_INVALID_COOKIE;
        }
        m_pOpenClose->Release();
        m_pOpenClose = nullptr;
    }
}

void CTextService::SetOpenCloseCompartment(bool open) {
    if (!m_pOpenClose) return;
    VARIANT var;
    var.vt   = VT_I4;
    var.lVal = open ? 1 : 0;
    m_pOpenClose->SetValue(m_tfClientId, &var);
}

// open/close の変更（自分の SetValue / 言語バー / アプリ）→ core のモード状態へ反映。
STDMETHODIMP CTextService::OnChange(REFGUID rguid) {
    using yoshinani::core::application::InputMode;
    if (!IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE) || !m_pOpenClose) return S_OK;
    VARIANT var;
    VariantInit(&var);
    if (SUCCEEDED(m_pOpenClose->GetValue(&var)) && var.vt == VT_I4) {
        const bool open = (var.lVal != 0);
        // 直接入力へ切り替わる瞬間に未確定が残っていたら生確定して保護する。
        // sink コールバック中なので edit session は非同期（fromSink=true）。
        if (!open && !AllEmpty() && m_pContext) FlushAsRaw(m_pContext, /*fromSink=*/true);
        m_mode.Set(open ? InputMode::Conversion : InputMode::Direct);
    }
    VariantClear(&var);
    return S_OK;
}

// 未確定区間（変換待ち+打鍵中）を生のまま全確定する（Enter 生確定・モード切替の共通経路）。
// sink コールバック発（fromSink）は同期 edit session が再入/失敗しうるため非同期で要求する。
void CTextService::FlushAsRaw(ITfContext* pic, bool fromSink) {
    if (AllEmpty()) return;
    const std::u16string toCommit = BuildPreeditView(m_queue, m_session.Preedit()).text;
    m_queue.Clear();
    m_session.Clear();
    const DWORD flags = (fromSink ? TF_ES_ASYNC : TF_ES_SYNC) | TF_ES_READWRITE;
    RequestEditSession(pic, [this, pic, toCommit](TfEditCookie ec) -> HRESULT {
        m_history.Push(toCommit);  // 生確定も文脈になる（継続モード）。確定と同時に積む
        CommitText(ec, pic, toCommit);
        return S_OK;
    }, flags);
    ReleaseContext();
}

// ---- edit session ------------------------------------------------------------

HRESULT CTextService::RequestEditSession(ITfContext* pic, std::function<HRESULT(TfEditCookie)> fn,
                                         DWORD flags) {
    if (!pic) return E_FAIL;
    CEditSession* es = new (std::nothrow) CEditSession(std::move(fn));
    if (!es) return E_OUTOFMEMORY;

    HRESULT hrSession = E_FAIL;
    HRESULT hr = pic->RequestEditSession(m_tfClientId, es, flags, &hrSession);
    es->Release();
    return SUCCEEDED(hr) ? hrSession : hr;
}

// ---- composition 操作（edit session 内） -------------------------------------

HRESULT CTextService::StartComposition(TfEditCookie ec, ITfContext* pic) {
    ITfInsertAtSelection* pInsert = nullptr;
    if (FAILED(pic->QueryInterface(IID_ITfInsertAtSelection, reinterpret_cast<void**>(&pInsert)))) {
        return E_FAIL;
    }

    ITfRange* pRange = nullptr;
    HRESULT hr = pInsert->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, nullptr, 0, &pRange);
    if (SUCCEEDED(hr) && pRange) {
        ITfContextComposition* pCtxComp = nullptr;
        if (SUCCEEDED(pic->QueryInterface(IID_ITfContextComposition,
                                          reinterpret_cast<void**>(&pCtxComp)))) {
            pCtxComp->StartComposition(ec, pRange,
                                       static_cast<ITfCompositionSink*>(this), &m_pComposition);
            pCtxComp->Release();
        }
        pRange->Release();
    }
    pInsert->Release();
    return m_pComposition ? S_OK : E_FAIL;
}

// 4-B: composition 内の [begin, end)（文字オフセット）に表示属性 atom を適用。
HRESULT CTextService::ApplyAttribute(TfEditCookie ec, ITfContext* pic, ITfRange* pCompRange,
                                     LONG begin, LONG end, TfGuidAtom atom) {
    if (begin >= end || atom == TF_INVALID_GUIDATOM) return S_OK;

    ITfProperty* pProp = nullptr;
    if (FAILED(pic->GetProperty(GUID_PROP_ATTRIBUTE, &pProp))) return E_FAIL;

    HRESULT hr = E_FAIL;
    ITfRange* pSub = nullptr;
    if (SUCCEEDED(pCompRange->Clone(&pSub))) {
        LONG shifted = 0;
        pSub->Collapse(ec, TF_ANCHOR_START);
        pSub->ShiftEnd(ec, end, &shifted, nullptr);
        pSub->ShiftStart(ec, begin, &shifted, nullptr);
        VARIANT var;
        var.vt   = VT_I4;
        var.lVal = static_cast<LONG>(atom);
        hr = pProp->SetValue(ec, pSub, &var);
        pSub->Release();
    }
    pProp->Release();
    return hr;
}

// preedit 連結表示（変換待ち + 打鍵中）と表示属性を composition に反映する。
HRESULT CTextService::UpdateText(TfEditCookie ec, ITfContext* pic) {
    if (!m_pComposition) return E_FAIL;

    ITfRange* pRange = nullptr;
    if (FAILED(m_pComposition->GetRange(&pRange))) return E_FAIL;

    const auto view = BuildPreeditView(m_queue, m_session.Preedit());
    pRange->SetText(ec, 0, reinterpret_cast<const WCHAR*>(view.text.c_str()),
                    static_cast<LONG>(view.text.size()));

    // 表示属性: 先頭の変換中区間 = 点線、残りの入力中区間 = 実線（4-B）。
    const LONG total = static_cast<LONG>(view.text.size());
    const LONG conv  = static_cast<LONG>(view.convertingLen);
    ApplyAttribute(ec, pic, pRange, 0, conv, m_attrConverting);
    ApplyAttribute(ec, pic, pRange, conv, total, m_attrInput);

    // 選択（キャレット）を末尾へ
    ITfRange* pSel = nullptr;
    if (SUCCEEDED(pRange->Clone(&pSel))) {
        pSel->Collapse(ec, TF_ANCHOR_END);
        TF_SELECTION ts;
        ts.range = pSel;
        ts.style.ase = TF_AE_NONE;
        ts.style.fInterimChar = FALSE;
        pic->SetSelection(ec, 1, &ts);
        pSel->Release();
    }
    pRange->Release();
    return S_OK;
}

HRESULT CTextService::ClearText(TfEditCookie ec) {
    if (!m_pComposition) return S_OK;
    ITfRange* pRange = nullptr;
    if (SUCCEEDED(m_pComposition->GetRange(&pRange))) {
        pRange->SetText(ec, 0, L"", 0);
        pRange->Release();
    }
    return S_OK;
}

// 任意文字列で composition 全体を置換して確定する（Enter 生確定・全確定用）。
HRESULT CTextService::CommitText(TfEditCookie ec, ITfContext* pic, const std::u16string& text) {
    if (!m_pComposition) {
        if (FAILED(StartComposition(ec, pic)) || !m_pComposition) return E_FAIL;
    }
    HRESULT hr = E_FAIL;
    ITfRange* pRange = nullptr;
    if (SUCCEEDED(m_pComposition->GetRange(&pRange)) && pRange) {
        hr = pRange->SetText(ec, 0, reinterpret_cast<const WCHAR*>(text.c_str()),
                             static_cast<LONG>(text.size()));
        // 確定文字列に preedit の下線属性を残さない（4-B）。
        ITfProperty* pProp = nullptr;
        if (SUCCEEDED(pic->GetProperty(GUID_PROP_ATTRIBUTE, &pProp))) {
            pProp->Clear(ec, pRange);
            pProp->Release();
        }
        // キャレットを確定文字列の末尾へ
        ITfRange* pSel = nullptr;
        if (SUCCEEDED(pRange->Clone(&pSel))) {
            pSel->Collapse(ec, TF_ANCHOR_END);
            TF_SELECTION ts;
            ts.range = pSel;
            ts.style.ase = TF_AE_NONE;
            ts.style.fInterimChar = FALSE;
            pic->SetSelection(ec, 1, &ts);
            pSel->Release();
        }
        pRange->Release();
    }
    // SetText の成否に関わらず composition は必ず終了させる（宙吊り防止）。失敗は hr で返す。
    EndComposition(ec);
    return hr;
}

void CTextService::EndComposition(TfEditCookie ec) {
    if (m_pComposition) {
        m_pComposition->EndComposition(ec);
        m_pComposition->Release();
        m_pComposition = nullptr;
    }
}

// ---- 4-A: 変換結果の到着（TIP スレッド・marshaller 経由） ----------------------

void CTextService::OnConvertResult(RequestId id, ConversionResult result) {
    // Esc 全取消・Enter 全確定の後に届いた結果は queue に居ないので false → 無視。
    const bool marked = (result.ok && !result.text.empty())
                            ? m_queue.MarkDone(id, std::move(result.text))
                            : m_queue.MarkFailed(id);
    if (!marked) return;

    if (!m_pComposition || !m_pContext) return;  // composition 消滅（フォーカス移動等）→ 破棄

    // pop は edit session の中で行う: session 要求が失敗しても結果はキューに残り、
    // 次の結果到着時に再試行できる（先に pop すると失敗時に入力が無言で消える）。
    RequestEditSession(m_pContext, [this](TfEditCookie ec) -> HRESULT {
        return CommitReadyInOrder(ec, m_pContext);
    });
    // 全確定（CommitText が composition を終了）していたら context を手放す。
    if (!m_pComposition) ReleaseContext();
}

// 投入順に「先頭から連続して終わっている分」を確定する（変換結果到着・Enter の共通経路）。
HRESULT CTextService::CommitReadyInOrder(TfEditCookie ec, ITfContext* pic) {
    if (!m_pComposition || !pic) return S_OK;

    {
        // 投入順に「先頭から連続して終わっている分」だけ確定する（追い越し禁止・§6.5 ③）。
        std::u16string committed;
        while (auto req = m_queue.PopReadyInOrder()) {
            committed += (req->state == ConvState::Done) ? req->result
                                                         : req->source;  // 失敗は生ローマ字
        }
        // 先頭がまだ変換中で確定できる分が無い → preedit 表示だけ更新
        //   （Enter で積んだ改行/生確定セグメントを「打った位置」に見せるため）。
        if (committed.empty()) return UpdateText(ec, pic);

        if (AllEmpty()) {
            // 全確定: composition 全体を確定文字列で置換して終了。
            m_history.Push(committed);  // 継続モード: 確定文を次の変換の文脈に
            CommitText(ec, pic, committed);
            return S_OK;
        }

        // 部分確定: 全文 = 確定分 + 残り preedit を書き、composition の先頭を
        // 確定分の直後へ縮める（ShiftStart）。先頭より前は確定テキストになる。
        ITfRange* pRange = nullptr;
        if (FAILED(m_pComposition->GetRange(&pRange))) return E_FAIL;

        const auto remaining = BuildPreeditView(m_queue, m_session.Preedit());
        const std::u16string full = committed + remaining.text;
        pRange->SetText(ec, 0, reinterpret_cast<const WCHAR*>(full.c_str()),
                        static_cast<LONG>(full.size()));

        // 旧属性を一掃（確定分に下線を残さない・4-B）。
        ITfProperty* pProp = nullptr;
        if (SUCCEEDED(pic->GetProperty(GUID_PROP_ATTRIBUTE, &pProp))) {
            pProp->Clear(ec, pRange);
            pProp->Release();
        }

        // 新しい composition 開始位置（確定分の直後）を作って ShiftStart。
        // shifted が期待値に届かない（range 末尾到達等）場合は縮小を諦め、
        // 全体を生のまま確定する（composition とモデルの不一致を残さない・稀ケース）。
        bool shrunk = false;
        ITfRange* pNewStart = nullptr;
        if (SUCCEEDED(pRange->Clone(&pNewStart))) {
            const LONG want = static_cast<LONG>(committed.size());
            LONG shifted = 0;
            pNewStart->Collapse(ec, TF_ANCHOR_START);
            pNewStart->ShiftEnd(ec, want, &shifted, nullptr);
            if (shifted == want) {
                pNewStart->Collapse(ec, TF_ANCHOR_END);
                shrunk = SUCCEEDED(m_pComposition->ShiftStart(ec, pNewStart));
            }
            pNewStart->Release();
        }
        pRange->Release();
        if (!shrunk) {
            m_queue.Clear();
            m_session.Clear();
            m_history.Push(full);  // フォールバック全確定も文脈に（継続モード）
            CommitText(ec, pic, full);
            return S_OK;
        }
        m_history.Push(committed);  // 部分確定した分も文脈に（継続モード）

        // 縮めた composition に属性を再適用 + キャレット末尾。
        ITfRange* pCompRange = nullptr;
        if (SUCCEEDED(m_pComposition->GetRange(&pCompRange))) {
            const LONG total = static_cast<LONG>(remaining.text.size());
            const LONG conv  = static_cast<LONG>(remaining.convertingLen);
            ApplyAttribute(ec, pic, pCompRange, 0, conv, m_attrConverting);
            ApplyAttribute(ec, pic, pCompRange, conv, total, m_attrInput);

            ITfRange* pSel = nullptr;
            if (SUCCEEDED(pCompRange->Clone(&pSel))) {
                pSel->Collapse(ec, TF_ANCHOR_END);
                TF_SELECTION ts;
                ts.range = pSel;
                ts.style.ase = TF_AE_NONE;
                ts.style.fInterimChar = FALSE;
                pic->SetSelection(ec, 1, &ts);
                pSel->Release();
            }
            pCompRange->Release();
        }
        return S_OK;
    }
}

// ---- ITfKeyEventSink ---------------------------------------------------------

STDMETHODIMP CTextService::OnSetFocus(BOOL /*fForeground*/) {
    return S_OK;
}

STDMETHODIMP CTextService::OnTestKeyDown(ITfContext* /*pic*/, WPARAM wParam, LPARAM lParam,
                                         BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    // 1-D: モード切替キー（両方向トグル）は常に消費。
    if (m_modeToggleVKs.count(wParam) != 0) {
        *pfEaten = TRUE;
        return S_OK;
    }
    // 片方向遷移キー: 直接→変換 / 変換→直接（現在モードと噛み合うときだけ消費）。
    if (m_mode.IsDirect() && m_conversionOnVKs.count(wParam) != 0) {
        *pfEaten = TRUE;
        return S_OK;
    }
    if (!m_mode.IsDirect() && m_directOnVKs.count(wParam) != 0) {
        *pfEaten = TRUE;
        return S_OK;
    }
    if (m_mode.IsDirect()) {
        *pfEaten = FALSE;
        return S_OK;
    }
    wchar_t ch = 0;
    KeyKind kind = Classify(wParam, lParam, m_triggerVKs, ch);
    InputAction act = Decide(kind, AllEmpty());
    *pfEaten = (act != InputAction::PassThrough);
    return S_OK;
}

STDMETHODIMP CTextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam,
                                     BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    using yoshinani::core::application::InputMode;

    // 1-D: モード遷移はトグル / 直接→変換 / 変換→直接 の 3 系統。
    // どれにマッチしても未確定が残っていれば生確定してから切替（データ保護）、
    // 順序固定: m_mode.Set / Toggle が先、SetOpenCloseCompartment は後
    //         （SetOpenCloseCompartment が OnChange を同期発火させる場合があり、
    //          逆順だと OnChange 側の Set 後にこちらの Set/Toggle が走り二重反転する）。
    const bool isToggle    = (m_modeToggleVKs.count(wParam) != 0);
    const bool isConvOn    = (m_mode.IsDirect()  && m_conversionOnVKs.count(wParam) != 0);
    const bool isDirectOn  = (!m_mode.IsDirect() && m_directOnVKs.count(wParam) != 0);
    if (isToggle || isConvOn || isDirectOn) {
        *pfEaten = TRUE;
        if (!AllEmpty()) FlushAsRaw(pic);
        if (isToggle)         m_mode.Toggle();
        else if (isConvOn)    m_mode.Set(InputMode::Conversion);
        else /* isDirectOn */ m_mode.Set(InputMode::Direct);
        SetOpenCloseCompartment(!m_mode.IsDirect());  // open = 変換（言語バー表示も同期）
        return S_OK;
    }
    if (m_mode.IsDirect()) {
        *pfEaten = FALSE;
        return S_OK;
    }
    wchar_t ch = 0;
    KeyKind kind = Classify(wParam, lParam, m_triggerVKs, ch);
    InputAction act = Decide(kind, AllEmpty());
    *pfEaten = (act != InputAction::PassThrough);

    switch (act) {
        case InputAction::Append:
            m_session.AppendChar(static_cast<char16_t>(ch));
            RetainContext(pic);
            RequestEditSession(pic, [this, pic](TfEditCookie ec) -> HRESULT {
                if (!m_pComposition) StartComposition(ec, pic);
                return UpdateText(ec, pic);
            });
            break;

        case InputAction::DeleteLast:
            // 4-A: Backspace は打鍵中セグメントのみ編集（変換待ちには触れない）。
            //      打鍵中が空（変換待ちのみ）のときは何もしない（キーは消費＝アプリに渡さない）。
            if (m_session.Empty()) break;
            m_session.Backspace();
            RequestEditSession(pic, [this, pic](TfEditCookie ec) -> HRESULT {
                HRESULT hr = UpdateText(ec, pic);
                if (AllEmpty()) {
                    EndComposition(ec);
                }
                return hr;
            });
            if (AllEmpty()) ReleaseContext();
            break;

        case InputAction::Commit: {
            // 4-A: 打鍵中セグメントを変換待ちに enqueue し、即座に打鍵を続けられるようにする。
            //      満杯（8件）なら何もしない（キーは消費・打鍵は継続できる）。
            if (m_session.Empty() || m_queue.Full()) break;
            const RequestId id = m_nextRequestId++;
            const std::u16string source = m_session.Preedit();
            m_queue.TryEnqueue(ConversionRequest{id, source, ConvState::Pending, {}});
            m_session.Clear();
            RetainContext(pic);
            // ワーカーで変換（待たない）。直前の確定文を文脈として渡す（継続モード）。
            m_marshaller.Dispatch(m_converter, id, {source, m_history.Tail()});
            RequestEditSession(pic, [this, pic](TfEditCookie ec) -> HRESULT {
                return UpdateText(ec, pic);  // テキスト不変・属性が 入力中→変換中 に変わる
            });
            break;
        }

        case InputAction::CommitRaw: {
            // Enter の挙動（2026-06-10 ユーザー要望）: 改行を「打った位置」に厳守して置く。
            //   打鍵中(preedit)は生のまま確定し、その直後に改行を入れる。両方を投入順
            //   キューに「確定済みセグメント」として積む＝前の変換中があればその後ろに並び、
            //   投入順に確定される（位置厳守・捨てない）。前に変換中が無ければ即確定＝即改行。
            //   改行は積んだ瞬間 preedit にも現れる（CommitReadyInOrder→UpdateText）。
            if (!m_session.Empty()) {
                m_queue.PushCommitted(m_nextRequestId++, m_session.Preedit());  // 生確定
                m_session.Clear();
            }
            m_queue.PushCommitted(m_nextRequestId++, std::u16string(u"\n"));     // 改行
            RetainContext(pic);
            RequestEditSession(pic, [this, pic](TfEditCookie ec) -> HRESULT {
                if (!m_pComposition && FAILED(StartComposition(ec, pic))) return E_FAIL;
                return CommitReadyInOrder(ec, pic);
            });
            if (!m_pComposition) ReleaseContext();
            break;
        }

        case InputAction::Cancel:
            // Esc: 未確定区間（変換待ち含む）を丸ごと取消（4-A の全取消）。
            m_queue.Clear();
            m_session.Clear();
            RequestEditSession(pic, [this](TfEditCookie ec) -> HRESULT {
                ClearText(ec);
                EndComposition(ec);
                return S_OK;
            });
            ReleaseContext();
            break;

        case InputAction::PassThrough:
        default:
            break;
    }
    return S_OK;
}

STDMETHODIMP CTextService::OnTestKeyUp(ITfContext* /*pic*/, WPARAM /*wParam*/, LPARAM /*lParam*/,
                                       BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP CTextService::OnKeyUp(ITfContext* /*pic*/, WPARAM /*wParam*/, LPARAM /*lParam*/,
                                   BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP CTextService::OnPreservedKey(ITfContext* /*pic*/, REFGUID /*rguid*/, BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

// ---- ITfCompositionSink ------------------------------------------------------

STDMETHODIMP CTextService::OnCompositionTerminated(TfEditCookie /*ecWrite*/,
                                                   ITfComposition* /*pComposition*/) {
    // アプリ側都合で composition が終了（フォーカス移動など）→ 後始末。
    // 変換待ちも破棄（飛行中の結果は到着時に無視される・4-A）。
    if (m_pComposition) {
        m_pComposition->Release();
        m_pComposition = nullptr;
    }
    ReleaseContext();
    m_queue.Clear();
    m_session.Clear();
    return S_OK;
}

// ---- ITfDisplayAttributeProvider（4-B） ---------------------------------------

STDMETHODIMP CTextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) {
    if (!ppEnum) return E_INVALIDARG;
    *ppEnum = new (std::nothrow) yoshinani::tsf::CEnumDisplayAttributeInfo();
    return *ppEnum ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CTextService::GetDisplayAttributeInfo(REFGUID guid,
                                                   ITfDisplayAttributeInfo** ppInfo) {
    if (!ppInfo) return E_INVALIDARG;
    *ppInfo = yoshinani::tsf::CreateDisplayAttributeInfo(guid);
    return *ppInfo ? S_OK : E_INVALIDARG;
}
