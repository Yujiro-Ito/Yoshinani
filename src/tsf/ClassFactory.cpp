// 1-A TSF スケルトン — クラスファクトリ実装。
#include "ClassFactory.h"
#include "TextService.h"
#include <new>

CClassFactory::CClassFactory() : m_cRef(1) {
    DllAddRef();
}

STDMETHODIMP CClassFactory::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CClassFactory::AddRef() {
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CClassFactory::Release() {
    LONG cr = InterlockedDecrement(&m_cRef);
    if (cr == 0) {
        DllRelease();
        delete this;
    }
    return cr;
}

STDMETHODIMP CClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) {
    if (ppv) *ppv = nullptr;
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;

    CTextService* p = new (std::nothrow) CTextService();
    if (!p) return E_OUTOFMEMORY;

    HRESULT hr = p->QueryInterface(riid, ppv);
    p->Release();   // QueryInterface 成功時のみ参照が残る
    return hr;
}

STDMETHODIMP CClassFactory::LockServer(BOOL fLock) {
    if (fLock) DllAddRef(); else DllRelease();
    return S_OK;
}
