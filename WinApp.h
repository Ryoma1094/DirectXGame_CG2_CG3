#pragma once
#include <Windows.h>
#include <cstdint>

// WindowsAPI
class WinApp
{
public:
	// ウィンドウプロシージャ
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	// クライアント領域のサイズ
	static const int32_t kClientWidth = 1280;
	static const int32_t kClientHeight = 720;

	// 初期化
	void Initialize();
	// 更新
	void Update();
	// 終了処理
	void Finalize();

	// ウィンドウハンドル取得
	HWND GetHwnd() const { return hwnd; }
	// HINSTANCE取得
	HINSTANCE GetHInstance() const { return wc_.hInstance; }

	

private:
	// ウィンドウハンドル
	HWND hwnd = nullptr;
	// ウィンドウクラス
	WNDCLASS wc_{};
};