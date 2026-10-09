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
	bool PushKey(BYTE keyNumber);      // 押されているか
	bool TriggerKey(BYTE keyNumber);   // 押された瞬間か
	bool ReleaseKey(uint8_t keyCode) const;   // 離されているか

	// キーバッファのポインタ取得
	const BYTE* GetKeyBuffer() const { return key; }

private:
	// DirectInputのポインタ
	ComPtr<IDirectInput8> directInput_ = nullptr;
	ComPtr<IDirectInputDevice8> keyboard_ = nullptr;

	// キーボードのデバイス
	ComPtr<IDirectInputDevice8> keyboard;

	// 全キーの状態を取得する
	BYTE key[256] = {};

	// 前回の全キーの状態
	BYTE keyPre[256] = {};
};