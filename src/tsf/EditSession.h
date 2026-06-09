// 1-B preedit — 汎用エディットセッション。
// ドキュメントロック下で実行したい処理を std::function で受ける薄いラッパ。
// （edit session 内に業務ロジックをベタ書きしない / §6.5 の指針に沿う）
#pragma once
#include "Globals.h"
#include <functional>

class CEditSession final : public ITfEditSession {
public:
    using Fn = std::function<HRESULT(TfEditCookie)>;
    explicit CEditSession(Fn fn) : m_cRef(1), m_fn(std::move(fn)) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession)) {
            *ppv = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG cr = InterlockedDecrement(&m_cRef);
        if (cr == 0) delete this;
        return cr;
    }
    STDMETHODIMP DoEditSession(TfEditCookie ec) override { return m_fn(ec); }

private:
    ~CEditSession() = default;
    LONG m_cRef;
    Fn   m_fn;
};
