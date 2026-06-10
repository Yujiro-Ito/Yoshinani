// 1-A/1-B/1-C — TIP 本体実装。
#include "TextService.h"
#include "EditSession.h"
#include "KeyMap.h"
#include "OllamaKanaKanjiConverter.h"
#include "domain/TriggerPolicy.h"
#include "application/Settings.h"
#include <fstream>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <utility>

using yoshinani::core::domain::KeyKind;
using yoshinani::core::domain::InputAction;
using yoshinani::core::domain::Decide;

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

    // 英字・数字・記号は打鍵文字をそのまま preedit に入れる（大小・記号は Gemma へ忠実に渡す）。
    // 矢印・F キー等は文字にならないので自動的に素通し。
    out_ch = VkToChar(vk, lParam);
    return (out_ch != 0) ? KeyKind::Character : KeyKind::Other;
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
// ファイルが無い/不正でも既定（Tab）になる。これがトリガー設定の分離点。
std::set<WPARAM> CTextService::LoadTriggerVKs() const {
    using yoshinani::core::application::ParseSettings;
    using yoshinani::core::application::Settings;

    Settings settings;  // 既定 = {"Tab"}
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

// ---- 活性化 ------------------------------------------------------------------

STDMETHODIMP CTextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD /*dwFlags*/) {
    m_pThreadMgr = ptim;
    if (m_pThreadMgr) m_pThreadMgr->AddRef();
    m_tfClientId = tid;
    m_triggerVKs = LoadTriggerVKs();
    // v1 のショートカット: TIP が具体実装(Ollama)を直接 new している（ポート経由の DI ではない）。
    // バックエンド選択（Ollama / クラウド GPT / 自作デーモン）は将来 Factory/設定で差し替える残債。
    m_converter = std::make_unique<yoshinani::ipc::OllamaKanaKanjiConverter>();

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
    m_converter.reset();
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

// 変換結果（任意文字列）で composition を置換して確定する（Commit 用）。
HRESULT CTextService::CommitText(TfEditCookie ec, ITfContext* pic, const std::u16string& text) {
    if (!m_pComposition) {
        if (FAILED(StartComposition(ec, pic)) || !m_pComposition) return E_FAIL;
    }
    HRESULT hr = E_FAIL;
    ITfRange* pRange = nullptr;
    if (SUCCEEDED(m_pComposition->GetRange(&pRange)) && pRange) {
        hr = pRange->SetText(ec, 0, reinterpret_cast<const WCHAR*>(text.c_str()),
                             static_cast<LONG>(text.size()));
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

// ---- ITfKeyEventSink ---------------------------------------------------------

STDMETHODIMP CTextService::OnSetFocus(BOOL /*fForeground*/) {
    return S_OK;
}

STDMETHODIMP CTextService::OnTestKeyDown(ITfContext* /*pic*/, WPARAM wParam, LPARAM lParam,
                                         BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    wchar_t ch = 0;
    KeyKind kind = Classify(wParam, lParam, m_triggerVKs, ch);
    InputAction act = Decide(kind, m_session.Empty());
    *pfEaten = (act != InputAction::PassThrough);
    return S_OK;
}

STDMETHODIMP CTextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam,
                                     BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    wchar_t ch = 0;
    KeyKind kind = Classify(wParam, lParam, m_triggerVKs, ch);
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

        case InputAction::Commit: {
            // 変換は edit session の外で同期実行（ドキュメントロックを長時間握らないため）。
            // 失敗・デーモン不在時は生のローマ字 preedit をそのまま確定（フォールバック・3-E）。
            std::u16string toCommit = m_session.Preedit();
            if (m_converter && !m_session.Empty()) {
                m_converter->Convert(
                    m_nextRequestId++, m_session.Preedit(),
                    [&toCommit](yoshinani::core::domain::RequestId,
                                yoshinani::core::domain::ConversionResult r) {
                        if (r.ok && !r.text.empty()) toCommit = r.text;
                    });
            }
            RequestEditSession(pic, [this, pic, toCommit](TfEditCookie ec) -> HRESULT {
                CommitText(ec, pic, toCommit);
                return S_OK;
            });
            m_session.Clear();
            break;
        }

        case InputAction::CommitRaw: {
            // Enter による生確定: 変換せず preedit をそのまま確定（Google IME 準拠・1-C 拡張）。
            const std::u16string toCommit = m_session.Preedit();
            RequestEditSession(pic, [this, pic, toCommit](TfEditCookie ec) -> HRESULT {
                CommitText(ec, pic, toCommit);
                return S_OK;
            });
            m_session.Clear();
            break;
        }

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
