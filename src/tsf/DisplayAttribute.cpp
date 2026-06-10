// 4-B 表示属性 — 実装。
#include "DisplayAttribute.h"

#include <new>

namespace yoshinani::tsf {

// {7AD1B4F9-43BD-4DE6-AD5C-DF8E3C7B41A1}
const GUID GUID_YoshinaniDaInput =
    {0x7ad1b4f9, 0x43bd, 0x4de6, {0xad, 0x5c, 0xdf, 0x8e, 0x3c, 0x7b, 0x41, 0xa1}};
// {0E2BD9A4-8DF7-4C82-9E2B-6C5A1F0D73C2}
const GUID GUID_YoshinaniDaConverting =
    {0x0e2bd9a4, 0x8df7, 0x4c82, {0x9e, 0x2b, 0x6c, 0x5a, 0x1f, 0x0d, 0x73, 0xc2}};

namespace {

// 色はテーマ任せ（TF_CT_NONE）。下線スタイルだけで状態を区別する（v1）。
TF_DISPLAYATTRIBUTE MakeAttr(TF_DA_LINESTYLE style) {
    TF_DISPLAYATTRIBUTE da{};
    da.crText.type    = TF_CT_NONE;
    da.crBk.type      = TF_CT_NONE;
    da.crLine.type    = TF_CT_NONE;
    da.lsStyle        = style;
    da.fBoldLine      = FALSE;
    da.bAttr          = TF_ATTR_INPUT;
    return da;
}

}  // namespace

// ---- CDisplayAttributeInfo -----------------------------------------------------

CDisplayAttributeInfo::CDisplayAttributeInfo(const GUID& guid, const TF_DISPLAYATTRIBUTE& attr,
                                             const WCHAR* description)
    : m_guid(guid), m_attr(attr), m_description(description) {
    DllAddRef();
}

STDMETHODIMP CDisplayAttributeInfo::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfDisplayAttributeInfo)) {
        *ppv = static_cast<ITfDisplayAttributeInfo*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CDisplayAttributeInfo::AddRef() { return InterlockedIncrement(&m_cRef); }

STDMETHODIMP_(ULONG) CDisplayAttributeInfo::Release() {
    LONG cr = InterlockedDecrement(&m_cRef);
    if (cr == 0) {
        delete this;
        DllRelease();
    }
    return cr;
}

STDMETHODIMP CDisplayAttributeInfo::GetGUID(GUID* pguid) {
    if (!pguid) return E_INVALIDARG;
    *pguid = m_guid;
    return S_OK;
}

STDMETHODIMP CDisplayAttributeInfo::GetDescription(BSTR* pbstrDesc) {
    if (!pbstrDesc) return E_INVALIDARG;
    *pbstrDesc = SysAllocString(m_description);
    return *pbstrDesc ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CDisplayAttributeInfo::GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) {
    if (!pda) return E_INVALIDARG;
    *pda = m_attr;
    return S_OK;
}

STDMETHODIMP CDisplayAttributeInfo::SetAttributeInfo(const TF_DISPLAYATTRIBUTE* /*pda*/) {
    return E_NOTIMPL;  // ユーザーカスタムは将来の設定 UI で（v1 は固定）
}

STDMETHODIMP CDisplayAttributeInfo::Reset() { return S_OK; }

// ---- CEnumDisplayAttributeInfo --------------------------------------------------

// TSF フレームワークが enum を保持する間 DLL をアンロードさせない（DllCanUnloadNow 対）。
CEnumDisplayAttributeInfo::CEnumDisplayAttributeInfo() {
    DllAddRef();
}

STDMETHODIMP CEnumDisplayAttributeInfo::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumTfDisplayAttributeInfo)) {
        *ppv = static_cast<IEnumTfDisplayAttributeInfo*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CEnumDisplayAttributeInfo::AddRef() { return InterlockedIncrement(&m_cRef); }

STDMETHODIMP_(ULONG) CEnumDisplayAttributeInfo::Release() {
    LONG cr = InterlockedDecrement(&m_cRef);
    if (cr == 0) {
        delete this;
        DllRelease();
    }
    return cr;
}

STDMETHODIMP CEnumDisplayAttributeInfo::Clone(IEnumTfDisplayAttributeInfo** ppEnum) {
    if (!ppEnum) return E_INVALIDARG;
    auto* p = new (std::nothrow) CEnumDisplayAttributeInfo();
    if (!p) return E_OUTOFMEMORY;
    p->m_index = m_index;
    *ppEnum = p;
    return S_OK;
}

STDMETHODIMP CEnumDisplayAttributeInfo::Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo,
                                             ULONG* pcFetched) {
    if (!rgInfo) return E_INVALIDARG;
    ULONG fetched = 0;
    while (fetched < ulCount && m_index < 2) {
        rgInfo[fetched] = CreateDisplayAttributeInfo(
            m_index == 0 ? GUID_YoshinaniDaInput : GUID_YoshinaniDaConverting);
        if (!rgInfo[fetched]) break;
        ++fetched;
        ++m_index;
    }
    if (pcFetched) *pcFetched = fetched;
    return (fetched == ulCount) ? S_OK : S_FALSE;
}

STDMETHODIMP CEnumDisplayAttributeInfo::Reset() {
    m_index = 0;
    return S_OK;
}

STDMETHODIMP CEnumDisplayAttributeInfo::Skip(ULONG ulCount) {
    // オーバーフローさせず末尾に留める（COM enum 規約: 超過時は S_FALSE）。
    constexpr ULONG kCount = 2;
    const ULONG remain = kCount - m_index;
    m_index = (ulCount >= remain) ? kCount : m_index + ulCount;
    return (ulCount > remain) ? S_FALSE : S_OK;
}

// ---- factory --------------------------------------------------------------------

ITfDisplayAttributeInfo* CreateDisplayAttributeInfo(REFGUID guid) {
    if (IsEqualGUID(guid, GUID_YoshinaniDaInput)) {
        return new (std::nothrow)
            CDisplayAttributeInfo(GUID_YoshinaniDaInput, MakeAttr(TF_LS_SOLID),
                                  L"Yoshinani Input (typing)");
    }
    if (IsEqualGUID(guid, GUID_YoshinaniDaConverting)) {
        return new (std::nothrow)
            CDisplayAttributeInfo(GUID_YoshinaniDaConverting, MakeAttr(TF_LS_DOT),
                                  L"Yoshinani Converting (pending)");
    }
    return nullptr;
}

}  // namespace yoshinani::tsf
