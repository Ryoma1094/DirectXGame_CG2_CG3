#pragma once

#include <Windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <wrl.h>
#include <cstdint>
#include "WinApp.h"

class Input
{
public:
	// namespace省略
	template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

public:
	// 初期化
	void Initialize(WinApp* winApp);

	// 更新
	void Update();

	// キー判定関数
	bool PushKey(BYTE keyNumber);      // 押されているか
	bool TriggerKey(BYTE keyNumber);   // 押された瞬間か
	bool ReleaseKey(uint8_t keyCode) const;   // 離されているか

	// キーバッファのポインタ取得
	const BYTE* GetKeyBuffer() const { return key; }

private:
	WinApp* winApp = nullptr;

	// DirectInputのポインタ
	ComPtr<IDirectInput8> directInput_ = nullptr;
	ComPtr<IDirectInputDevice8> keyboard_ = nullptr;

	// 全キーの状態を取得する
	BYTE key[256] = {};

	// 前回の全キーの状態
	BYTE keyPre[256] = {};
};