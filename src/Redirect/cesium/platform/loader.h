#pragma once
// 精简版 loader.h 替身：steamhack 只用到 log_line。
#include <string>
#include <windows.h>

void log_line(const char* msg);
void log_line(const std::string& msg);
void log_line(const std::wstring& msg);
