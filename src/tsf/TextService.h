// 1-A/1-B/1-C/4-A/4-B — TIP 本体。
//   1-A: ITfTextInputProcessorEx（活性化・登録）
//   1-B: ITfKeyEventSink / ITfCompositionSink（キー捕捉・preedit）
//   1-C: TriggerPolicy(core) を使った確定/取消
//   4-A: 非同期変換（Tab=enqueue で打鍵継続、結果は投入順に先頭確定）
//   4-B: ITfDisplayAttributeProvider（入力中=実線 / 変換中=点線 下線）
#pragma once
#include "Globals.h"
#include "ConvertMarshaller.h"
#include "application/ConversionQueue.h"
#include "application/InputSession.h"
#include "application/Settings.h"
#include "domain/ports/IKanaKanjiConverter.h"
#include <functional>
#include <memory>
#include <set>

class CTextService final : public ITfTextInputProcessorEx,
                           public ITfKeyEventSink,
                           public ITfCompositionSink,
                           public ITfDisplayAttributeProvider {
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

    // ITfDisplayAttributeProvider（4-B）
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) override;

private:
    ~CTextService();

    // sink の Advise/Unadvise
    BOOL InitKeyEventSink();
    void UninitKeyEventSink();

    // edit session（同期・読み書き）を1回実行
    HRESULT RequestEditSession(ITfContext* pic, std::function<HRESULT(TfEditCookie)> fn);

    // composition 操作（edit session 内から呼ぶ）
    HRESULT StartComposition(TfEditCookie ec, ITfContext* pic);
    HRESULT UpdateText(TfEditCookie ec, ITfContext* pic);   // preedit 連結表示+属性を反映
    HRESULT ClearText(TfEditCookie ec);                     // composition の中身を空に（取消用）
    HRESULT CommitText(TfEditCookie ec, ITfContext* pic,    // 任意文字列で全確定
                       const std::u16string& text);
    void    EndComposition(TfEditCookie ec);

    // 4-B: composition 内 [begin,end) に表示属性 atom を適用（edit session 内）。
    HRESULT ApplyAttribute(TfEditCookie ec, ITfContext* pic, ITfRange* pCompRange,
                           LONG begin, LONG end, TfGuidAtom atom);

    // 4-A: 変換結果の到着（marshaller 経由・TIP スレッド）。投入順に先頭セグメントを確定する。
    void OnConvertResult(yoshinani::core::domain::RequestId id,
                         yoshinani::core::domain::ConversionResult result);

    // 4-A: 打鍵中/変換待ちセグメントの有無（Decide の preeditEmpty に渡す）。
    bool AllEmpty() const { return m_queue.Empty() && m_session.Empty(); }

    // 最後にキーを受けた context を保持（変換結果の到着時に edit session を要求するため）。
    void RetainContext(ITfContext* pic);
    void ReleaseContext();

    // settings.json（DLL と同じディレクトリ）を読む。無い/不正なら既定値。
    yoshinani::core::application::Settings LoadSettings() const;
    // 設定から確定トリガーの VK 集合を構築する。
    std::set<WPARAM> LoadTriggerVKs(const yoshinani::core::application::Settings& settings) const;
    // 設定から変換バックエンド（openai / ollama）を生成する（3-A ポート経由の注入）。
    std::shared_ptr<yoshinani::core::domain::IKanaKanjiConverter>
        CreateConverter(const yoshinani::core::application::ConverterSettings& cs) const;

    LONG          m_cRef;
    ITfThreadMgr* m_pThreadMgr;
    TfClientId    m_tfClientId;
    ITfComposition* m_pComposition;
    ITfContext*     m_pContext;   // composition 継続中の対象 context（AddRef 保持）

    yoshinani::core::application::InputSession    m_session;  // 打鍵中セグメント
    yoshinani::core::application::ConversionQueue m_queue;    // 変換待ちセグメント列（4-A）
    std::set<WPARAM> m_triggerVKs;   // 確定トリガー（設定由来。既定 = Tab）

    // 変換器（3-A ポート）。shared なのはワーカースレッドと生存を共有するため（4-A）。
    std::shared_ptr<yoshinani::core::domain::IKanaKanjiConverter> m_converter;
    yoshinani::tsf::ConvertMarshaller m_marshaller;           // 結果を TIP スレッドへ戻す
    yoshinani::core::domain::RequestId m_nextRequestId = 1;

    // 4-B: 表示属性の TfGuidAtom（Activate で RegisterGUID）
    TfGuidAtom m_attrInput = TF_INVALID_GUIDATOM;
    TfGuidAtom m_attrConverting = TF_INVALID_GUIDATOM;
};
