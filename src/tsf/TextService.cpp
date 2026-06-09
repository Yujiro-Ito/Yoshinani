// 1-A/1-B/1-C — TIP 本体実装。
#include "TextService.h"
#include "EditSession.h"
#include "KeyMap.h"
#include "domain/TriggerPolicy.h"
#include "application/Settings.h"
#include <fstream>
#include <new>
#include <sstream>
#include <string>

using yoshinani::core::domain::KeyKind;
using yoshinani::core::domain::InputAction;
using yoshinani::core::domain::Decide;

namespace {

// 修飾キーが押されているか（Ctrl/Alt 併用はショートカット扱いで素通し）。
bool IsDown(int vk) { return (GetKeyState(vk) & 0x8000) != 0; }

// VK を core の KeyKind に正規化し、文字を伴う場合は out_ch に返す（infra の責務）。
//   どの VK が確定トリガーかは triggers（設定由来）で決まる。
KeyKind Classify(WPARAM vk, const std::set<WPARAM>& triggers, wchar_t& out_ch) {
    out_ch = 0;
    if (IsDown(VK_CONTROL) || IsDown(VK_MENU)) return KeyKind::Other;  // Ctrl/Alt は触らない

    if (triggers.count(vk) != 0) return KeyKind::Trigger;              // 確定トリガー（設定）

    if (vk >= 'A' && vk <= 'Z') {                  // 英字 → 小文字ローマ字
        out_ch = static_cast<wchar_t>(L'a' + (vk - 'A'));
        return KeyKind::Character;
    }
    switch (vk) {
        case VK_BACK:   return KeyKind::Backspace;
        case VK_ESCAPE: return KeyKind::Escape;
        default:        return KeyKind::Other;
    }
}

}  // namespace

CTextService::CTextService()
    : m_cRef(1), m_pThreadMgr(nullptr), m_tfClientId(TF_CLIENTID_NULL),
      m_pComposition(nullptr) {
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

// settings.json（DLL と同じディレクトリ）→ core パーサ → キー名を VK へ写像。
// ファイルが無い/不正でも既定（Space）になる。これがトリガー設定の分離点。
std::set<WPARAM> CTextService::LoadTriggerVKs() const {
    using yoshinani::core::application::ParseSettings;
    using yoshinani::core::application::Settings;

    Settings settings;  // 既定 = {"Space"}
    WCHAR dllPath[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_hInst, dllPath, ARRAYSIZE(dllPath));
    if (n > 0 && n < ARRAYSIZE(dllPath)) {
        std::wstring path(dllPath);
        const size_t slash = path.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            path = path.substr(0, slash + 1) + L"settings.json";
            std::ifstream f(path.c_str());  // MSVC: wchar_t* パス可
            if (f) {
                std::ostringstream ss;
                ss << f.rdbuf();
                settings = ParseSettings(ss.str());
            }
        }
    }

    std::set<WPARAM> vks;
    for (const std::string& name : settings.triggerKeys) {
        if (auto vk = KeyNameToVk(name)) vks.insert(*vk);
    }
    if (vks.empty()) vks.insert(static_cast<WPARAM>(VK_SPACE));  // 安全側
    return vks;
}

// ---- 活性化 ------------------------------------------------------------------

STDMETHODIMP CTextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD /*dwFlags*/) {
    m_pThreadMgr = ptim;
    if (m_pThreadMgr) m_pThreadMgr->AddRef();
    m_tfClientId = tid;
    m_triggerVKs = LoadTriggerVKs();

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
    UninitKeyEventSink();
    if (m_pComposition) {
        m_pComposition->Release();
        m_pComposition = nullptr;
    }
    m_session.Clear();
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

// ---- edit session ------------------------------------------------------------

HRESULT CTextService::RequestEditSession(ITfContext* pic, std::function<HRESULT(TfEditCookie)> fn) {
    if (!pic) return E_FAIL;
    CEditSession* es = new (std::nothrow) CEditSession(std::move(fn));
    if (!es) return E_OUTOFMEMORY;

    HRESULT hrSession = E_FAIL;
    HRESULT hr = pic->RequestEditSession(m_tfClientId, es,
                                         TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
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

HRESULT CTextService::UpdateText(TfEditCookie ec, ITfContext* pic) {
    if (!m_pComposition) return E_FAIL;

    ITfRange* pRange = nullptr;
    if (FAILED(m_pComposition->GetRange(&pRange))) return E_FAIL;

    const std::u16string& s = m_session.Preedit();
    pRange->SetText(ec, 0, reinterpret_cast<const WCHAR*>(s.c_str()), static_cast<LONG>(s.size()));

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

void CTextService::EndComposition(TfEditCookie ec) {
    if (m_pComposition) {
        m_pComposition->EndComposition(ec);
        m_pComposition->Release();
        m_pComposition = nullptr;
    }
}

// ---- ITfKeyEventSink ---------------------------------------------------------

STDMETHODIMP CTextService::OnSetFocus(BOOL /*fForeground*/) {
    return S_OK;
}

STDMETHODIMP CTextService::OnTestKeyDown(ITfContext* /*pic*/, WPARAM wParam, LPARAM /*lParam*/,
                                         BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    wchar_t ch = 0;
    KeyKind kind = Classify(wParam, m_triggerVKs, ch);
    InputAction act = Decide(kind, m_session.Empty());
    *pfEaten = (act != InputAction::PassThrough);
    return S_OK;
}

STDMETHODIMP CTextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM /*lParam*/,
                                     BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    wchar_t ch = 0;
    KeyKind kind = Classify(wParam, m_triggerVKs, ch);
    InputAction act = Decide(kind, m_session.Empty());
    *pfEaten = (act != InputAction::PassThrough);

    switch (act) {
        case InputAction::Append:
            m_session.AppendChar(static_cast<char16_t>(ch));
            RequestEditSession(pic, [this, pic](TfEditCookie ec) -> HRESULT {
                if (!m_pComposition) StartComposition(ec, pic);
                return UpdateText(ec, pic);
            });
            break;

        case InputAction::DeleteLast:
            m_session.Backspace();
            RequestEditSession(pic, [this, pic](TfEditCookie ec) -> HRESULT {
                HRESULT hr = UpdateText(ec, pic);
                if (m_session.Empty()) EndComposition(ec);
                return hr;
            });
            break;

        case InputAction::Commit:
            RequestEditSession(pic, [this](TfEditCookie ec) -> HRESULT {
                EndComposition(ec);
                return S_OK;
            });
            m_session.Clear();
            break;

        case InputAction::Cancel:
            RequestEditSession(pic, [this](TfEditCookie ec) -> HRESULT {
                ClearText(ec);
                EndComposition(ec);
                return S_OK;
            });
            m_session.Clear();
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
    if (m_pComposition) {
        m_pComposition->Release();
        m_pComposition = nullptr;
    }
    m_session.Clear();
    return S_OK;
}
