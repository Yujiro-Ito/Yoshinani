// 4-B 表示属性 — preedit の状態を下線スタイルで区別する（infra）。
//   入力中（打鍵中セグメント）   = 実線下線（TF_LS_SOLID）
//   変換中（変換待ちセグメント） = 点線下線（TF_LS_DOT）
// 色は v1 ではテーマ任せ（TF_CT_NONE）。好みの色は将来の設定 UI で。
#pragma once
#include "Globals.h"

namespace yoshinani::tsf {

// 表示属性の GUID（DllRegisterServer での登録は不要。ITfCategoryMgr::RegisterGUID で
// 実行時に TfGuidAtom 化して GUID_PROP_ATTRIBUTE に設定する）。
extern const GUID GUID_YoshinaniDaInput;       // 入力中
extern const GUID GUID_YoshinaniDaConverting;  // 変換中

// ITfDisplayAttributeInfo の単純実装（固定スタイルを返すだけ）。
class CDisplayAttributeInfo final : public ITfDisplayAttributeInfo {
public:
    CDisplayAttributeInfo(const GUID& guid, const TF_DISPLAYATTRIBUTE& attr,
                          const WCHAR* description);

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfDisplayAttributeInfo
    STDMETHODIMP GetGUID(GUID* pguid) override;
    STDMETHODIMP GetDescription(BSTR* pbstrDesc) override;
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) override;
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE* pda) override;
    STDMETHODIMP Reset() override;

private:
    ~CDisplayAttributeInfo() = default;

    LONG                m_cRef = 1;
    GUID                m_guid;
    TF_DISPLAYATTRIBUTE m_attr;
    const WCHAR*        m_description;
};

// 2 つの属性（入力中/変換中）を列挙する IEnumTfDisplayAttributeInfo。
class CEnumDisplayAttributeInfo final : public IEnumTfDisplayAttributeInfo {
public:
    CEnumDisplayAttributeInfo();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IEnumTfDisplayAttributeInfo
    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched) override;
    STDMETHODIMP Reset() override;
    STDMETHODIMP Skip(ULONG ulCount) override;

private:
    ~CEnumDisplayAttributeInfo() = default;

    LONG  m_cRef = 1;
    ULONG m_index = 0;  // 0 = 入力中, 1 = 変換中
};

// GUID に対応する ITfDisplayAttributeInfo を生成（呼び出し側が Release）。未知 GUID は nullptr。
ITfDisplayAttributeInfo* CreateDisplayAttributeInfo(REFGUID guid);

}  // namespace yoshinani::tsf
