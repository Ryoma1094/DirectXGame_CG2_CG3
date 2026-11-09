#include "StringUtility.h"
#include <Windows.h>

namespace StringUtility {
	std::wstring ConvertString(const std::string& str) {
		if (str.empty()) return L"";
		int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
		if (sizeNeeded == 0) sizeNeeded = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, NULL, 0);
		std::wstring wstr(sizeNeeded - 1, 0);
		MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], sizeNeeded);
		return wstr;
	}

	std::string ConvertString(const std::wstring& str) {
		if (str.empty()) return "";
		int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, NULL, 0, NULL, NULL);
		if (sizeNeeded == 0) sizeNeeded = WideCharToMultiByte(CP_ACP, 0, str.c_str(), -1, NULL, 0, NULL, NULL);
		std::string result(sizeNeeded - 1, 0);
		WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, &result[0], sizeNeeded, NULL, NULL);
		return result;
	}
}