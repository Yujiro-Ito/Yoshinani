// 1-A/1-B/1-C — TIP 本体。
//   1-A: ITfTextInputProcessorEx（活性化・登録）
//   1-B: ITfKeyEventSink / ITfCompositionSink（キー捕捉・preedit）
//   1-C: TriggerPolicy(core) を使った確定/取消
#pragma once
#include "Globals.h"
#include "application/InputSession.h"
#include "application/Settings.h"
#include "domain/ports/IKanaKanjiConverter.h"
#include <functional>
#include <memory>
#include <set>

class CTextService final : public ITfTextInputProcessorEx,
                           public ITfKeyEventSink,
                           public ITfCompositionSink {
public:
    CTextService();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor / Ex
    STDMETHODIMP Activate(ITfThreadMgr* ptim, TfClientId tid) override;
    STDMETHODIMP Deactivate() override;
    STDMETHODIMP ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) override;

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition) override;

private:
    ~CTextService();

    // sink の Advise/Unadvise
    BOOL InitKeyEventSink();
    void UninitKeyEventSink();

    // edit session（同期・読み書き）を1回実行
    HRESULT RequestEditSession(ITfContext* pic, std::function<HRESULT(TfEditCookie)> fn);

    // composition 操作（edit session 内から呼ぶ）
    HRESULT StartComposition(TfEditCookie ec, ITfContext* pic);
    HRESULT UpdateText(TfEditCookie ec, ITfContext* pic);   // preedit を composition に反映
    HRESULT ClearText(TfEditCookie ec);                     // composition の中身を空に（取消用）
    HRESULT CommitText(TfEditCookie ec, ITfContext* pic,    // 任意文字列(変換結果)で確定
                       const std::u16string& text);
    void    EndComposition(TfEditCookie ec);

    // settings.json（DLL と同じディレクトリ）を読む。無い/不正なら既定値。
    yoshinani::core::application::Settings LoadSettings() const;
    // 設定から確定トリガーの VK 集合を構築する。
    std::set<WPARAM> LoadTriggerVKs(const yoshinani::core::application::Settings& settings) const;
    // 設定から変換バックエンド（openai / ollama）を生成する（3-A ポート経由の注入）。
    std::unique_ptr<yoshinani::core::domain::IKanaKanjiConverter>
        CreateConverter(const yoshinani::core::application::ConverterSettings& cs) const;

    LONG          m_cRef;
    ITfThreadMgr* m_pThreadMgr;
    TfClientId    m_tfClientId;
    ITfComposition* m_pComposition;

    yoshinani::core::application::InputSession m_session;
    std::set<WPARAM> m_triggerVKs;   // 確定トリガー（設定由来。既定 = Tab）

    // 変換器（3-A ポート）。v1 は Ollama(Gemma) 実装を注入（将来は自作デーモン+パイプへ差し替え）。
    std::unique_ptr<yoshinani::core::domain::IKanaKanjiConverter> m_converter;
    yoshinani::core::domain::RequestId m_nextRequestId = 1;
};
