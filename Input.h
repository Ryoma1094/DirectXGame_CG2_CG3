#pragma once

#include <Windows.h>
#define DIRECTINPUT_VERSION 0x0800 // DirectInputのバージョン指定
#include <dinput.h>
#include <wrl.h>
#include <cstdint>

class Input
{
public:
	// namespace省略
	template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

public:
	// 初期化（仮引数を指定）
	void Initialize(HINSTANCE hInstance, HWND hwnd);

	// 更新
	void Update();

	// キー判定関数
	bool PushKey(uint8_t keyCode) const;      // 押されているか
	bool TriggerKey(uint8_t keyCode) const;   // 押された瞬間か
	bool ReleaseKey(uint8_t keyCode) const;   // 離されているか

	// キーバッファのポインタ取得
	const BYTE* GetKeyBuffer() const { return key_; }

private:
	// DirectInputのポインタ
	ComPtr<IDirectInput8> directInput_ = nullptr;
	ComPtr<IDirectInputDevice8> keyboard_ = nullptr;

	// キーボードのデバイス
	ComPtr<IDirectInputDevice8> keyboard;

	// キーバッファ
	BYTE key_[256] = {};
	BYTE keyPre_[256] = {};
};