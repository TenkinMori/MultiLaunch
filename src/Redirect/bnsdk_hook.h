#pragma once
#include <string>

// 只把 %LocalLow%\feimo\AstralParty_CN\BnSdk 重定向到 profileRoot\BnSdk。
// 不碰 LocalLow 的其他内容，缓存/更新保持共用。
void BnSdkHook_Install(const std::wstring& profileRoot);
