#pragma once

#include <windows.h>

#include <string>

// 将 HBITMAP 保存为 PNG 文件；失败时通过 errorOut 返回原因
bool SaveBitmapAsPng(HBITMAP bitmap, const std::wstring& path, std::wstring* errorOut);

