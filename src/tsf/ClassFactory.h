// 1-A TSF スケルトン — CLSID_Yoshinani 用クラスファクトリ。
#pragma once
#include "Globals.h"

class CClassFactory final : public IClassFactory {
public:
    CClassFactory();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IClassFactory
    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override;
    STDMETHODIMP LockServer(BOOL fLock) override;

private:
    ~CClassFactory() = default;
    LONG m_cRef;
};
