// bnsdk_hook.cpp —— 只重定向 BnSdk 目录。
//
// 真实路径:
//   %LocalLow%\feimo\AstralParty_CN\BnSdk\...
// profile 路径:
//   <ASTRAL_PROFILE_DIR>\BnSdk\...
//
// 其余 LocalLow（Addressables / NextPack / Player.log 等）不经过这里。
#include "bnsdk_hook.h"

#include <windows.h>
#include <string>
#include <vector>
#include "MinHook.h"

namespace {

typedef HANDLE(WINAPI* PFN_CreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                                        DWORD, DWORD, HANDLE);
typedef HANDLE(WINAPI* PFN_CreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                                        DWORD, DWORD, HANDLE);
typedef DWORD(WINAPI* PFN_GetFileAttributesW)(LPCWSTR);
typedef DWORD(WINAPI* PFN_GetFileAttributesA)(LPCSTR);
typedef BOOL(WINAPI* PFN_GetFileAttributesExW)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
typedef HANDLE(WINAPI* PFN_FindFirstFileW)(LPCWSTR, LPWIN32_FIND_DATAW);
typedef HANDLE(WINAPI* PFN_FindFirstFileExW)(LPCWSTR, FINDEX_INFO_LEVELS,
                                             LPVOID, FINDEX_SEARCH_OPS, LPVOID, DWORD);
typedef BOOL(WINAPI* PFN_CreateDirectoryW)(LPCWSTR, LPSECURITY_ATTRIBUTES);
typedef BOOL(WINAPI* PFN_RemoveDirectoryW)(LPCWSTR);
typedef BOOL(WINAPI* PFN_DeleteFileW)(LPCWSTR);
typedef BOOL(WINAPI* PFN_MoveFileExW)(LPCWSTR, LPCWSTR, DWORD);
typedef BOOL(WINAPI* PFN_CopyFileW)(LPCWSTR, LPCWSTR, BOOL);
typedef BOOL(WINAPI* PFN_SetFileAttributesW)(LPCWSTR, DWORD);

PFN_CreateFileW        g_createFileW;
PFN_CreateFileA        g_createFileA;
PFN_GetFileAttributesW g_getAttrsW;
PFN_GetFileAttributesA g_getAttrsA;
PFN_GetFileAttributesExW g_getAttrsExW;
PFN_FindFirstFileW     g_findFirstW;
PFN_FindFirstFileExW   g_findFirstExW;
PFN_CreateDirectoryW   g_createDirW;
PFN_RemoveDirectoryW   g_removeDirW;
PFN_DeleteFileW        g_deleteFileW;
PFN_MoveFileExW        g_moveFileW;
PFN_CopyFileW          g_copyFileW;
PFN_SetFileAttributesW g_setAttrsW;

std::wstring g_real;      // ...\LocalLow\feimo\AstralParty_CN\BnSdk
std::wstring g_profile;   // <profile>\BnSdk
bool g_installed = false;

std::wstring RealLocalLow() {
    wchar_t buf[MAX_PATH] = {0};
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    std::wstring out(buf);
    out += L"\\AppData\\LocalLow";
    return out;
}

std::wstring FullPath(const std::wstring& in) {
    if (in.empty()) return in;
    if (in[0] == L'\\' || (in.size() > 1 && in[1] == L':')) return in;
    wchar_t buf[MAX_PATH * 4] = {0};
    DWORD n = GetFullPathNameW(in.c_str(), MAX_PATH * 4, buf, nullptr);
    if (n == 0 || n >= MAX_PATH * 4) return in;
    return std::wstring(buf, n);
}

bool Redirect(const wchar_t* raw, std::wstring& out) {
    if (!raw || !*raw || g_real.empty()) return false;

    std::wstring p(raw);
    bool longPrefix = false;
    if (p.rfind(L"\\\\?\\", 0) == 0) {
        longPrefix = true;
        p = p.substr(4);
    }
    p = FullPath(p);

    if (_wcsnicmp(p.c_str(), g_real.c_str(), g_real.size()) != 0) return false;
    if (p.size() != g_real.size() && p[g_real.size()] != L'\\') return false;

    out = g_profile + p.substr(g_real.size());
    if (longPrefix) out = L"\\\\?\\" + out;
    return true;
}

std::wstring ToWide(const char* s) {
    if (!s) return L"";
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, -1, &w[0], n);
    while (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

std::string ToAnsi(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string a(n, '\0');
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, &a[0], n, nullptr, nullptr);
    while (!a.empty() && a.back() == '\0') a.pop_back();
    return a;
}

void EnsureDir(const std::wstring& path) {
    if (path.empty()) return;
    std::wstring cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur += path[i];
        if (path[i] == L'\\' || i + 1 == path.size()) {
            std::wstring d = cur;
            while (!d.empty() && d.back() == L'\\') d.pop_back();
            if (d.size() > 3) CreateDirectoryW(d.c_str(), nullptr);
        }
    }
}

// ---------------------------------------------------------------- hooks
HANDLE WINAPI HookCreateFileW(LPCWSTR name, DWORD access, DWORD share,
                              LPSECURITY_ATTRIBUTES sa, DWORD disposition,
                              DWORD flags, HANDLE tmpl) {
    std::wstring p;
    if (Redirect(name, p)) name = p.c_str();
    return g_createFileW(name, access, share, sa, disposition, flags, tmpl);
}

HANDLE WINAPI HookCreateFileA(LPCSTR name, DWORD access, DWORD share,
                              LPSECURITY_ATTRIBUTES sa, DWORD disposition,
                              DWORD flags, HANDLE tmpl) {
    std::wstring p;
    std::string a;
    if (Redirect(ToWide(name).c_str(), p)) {
        a = ToAnsi(p);
        name = a.c_str();
    }
    return g_createFileA(name, access, share, sa, disposition, flags, tmpl);
}

DWORD WINAPI HookGetFileAttributesW(LPCWSTR name) {
    std::wstring p;
    if (Redirect(name, p)) name = p.c_str();
    return g_getAttrsW(name);
}

DWORD WINAPI HookGetFileAttributesA(LPCSTR name) {
    std::wstring p;
    std::string a;
    if (Redirect(ToWide(name).c_str(), p)) {
        a = ToAnsi(p);
        name = a.c_str();
    }
    return g_getAttrsA(name);
}

BOOL WINAPI HookGetFileAttributesExW(LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info) {
    std::wstring p;
    if (Redirect(name, p)) name = p.c_str();
    return g_getAttrsExW(name, level, info);
}

HANDLE WINAPI HookFindFirstFileW(LPCWSTR name, LPWIN32_FIND_DATAW data) {
    std::wstring p;
    if (Redirect(name, p)) name = p.c_str();
    return g_findFirstW(name, data);
}

HANDLE WINAPI HookFindFirstFileExW(LPCWSTR name, FINDEX_INFO_LEVELS level, LPVOID data,
                                   FINDEX_SEARCH_OPS op, LPVOID filter, DWORD flags) {
    std::wstring p;
    if (Redirect(name, p)) name = p.c_str();
    return g_findFirstExW(name, level, data, op, filter, flags);
}

BOOL WINAPI HookCreateDirectoryW(LPCWSTR name, LPSECURITY_ATTRIBUTES sa) {
    std::wstring p;
    if (Redirect(name, p)) name = p.c_str();
    return g_createDirW(name, sa);
}

BOOL WINAPI HookRemoveDirectoryW(LPCWSTR name) {
    std::wstring p;
    if (Redirect(name, p)) name = p.c_str();
    return g_removeDirW(name);
}

BOOL WINAPI HookDeleteFileW(LPCWSTR name) {
    std::wstring p;
    if (Redirect(name, p)) name = p.c_str();
    return g_deleteFileW(name);
}

BOOL WINAPI HookMoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags) {
    std::wstring a, b;
    if (Redirect(from, a)) from = a.c_str();
    if (Redirect(to, b)) to = b.c_str();
    return g_moveFileW(from, to, flags);
}

BOOL WINAPI HookCopyFileW(LPCWSTR from, LPCWSTR to, BOOL failIfExists) {
    std::wstring a, b;
    if (Redirect(from, a)) from = a.c_str();
    if (Redirect(to, b)) to = b.c_str();
    return g_copyFileW(from, to, failIfExists);
}

BOOL WINAPI HookSetFileAttributesW(LPCWSTR name, DWORD attrs) {
    std::wstring p;
    if (Redirect(name, p)) name = p.c_str();
    return g_setAttrsW(name, attrs);
}

#define HOOK_API(mod, name, fn, orig) \
    MH_CreateHookApi(L##mod, name, reinterpret_cast<LPVOID>(fn), \
                     reinterpret_cast<LPVOID*>(&orig))

}  // namespace

void BnSdkHook_Install(const std::wstring& profileRoot) {
    if (g_installed) return;
    g_installed = true;

    std::wstring low = RealLocalLow();
    if (low.empty() || profileRoot.empty()) return;
    g_real = low + L"\\feimo\\AstralParty_CN\\BnSdk";
    // 沿用既有 profile 布局：...\AppData\LocalLow\feimo\AstralParty_CN\BnSdk
    g_profile = profileRoot + L"\\AppData\\LocalLow\\feimo\\AstralParty_CN\\BnSdk";
    EnsureDir(g_profile);

    HOOK_API("kernel32.dll", "CreateFileW", HookCreateFileW, g_createFileW);
    HOOK_API("kernel32.dll", "CreateFileA", HookCreateFileA, g_createFileA);
    HOOK_API("kernel32.dll", "GetFileAttributesW", HookGetFileAttributesW, g_getAttrsW);
    HOOK_API("kernel32.dll", "GetFileAttributesA", HookGetFileAttributesA, g_getAttrsA);
    HOOK_API("kernel32.dll", "GetFileAttributesExW", HookGetFileAttributesExW, g_getAttrsExW);
    HOOK_API("kernel32.dll", "FindFirstFileW", HookFindFirstFileW, g_findFirstW);
    HOOK_API("kernel32.dll", "FindFirstFileExW", HookFindFirstFileExW, g_findFirstExW);
    HOOK_API("kernel32.dll", "CreateDirectoryW", HookCreateDirectoryW, g_createDirW);
    HOOK_API("kernel32.dll", "RemoveDirectoryW", HookRemoveDirectoryW, g_removeDirW);
    HOOK_API("kernel32.dll", "DeleteFileW", HookDeleteFileW, g_deleteFileW);
    HOOK_API("kernel32.dll", "MoveFileExW", HookMoveFileExW, g_moveFileW);
    HOOK_API("kernel32.dll", "CopyFileW", HookCopyFileW, g_copyFileW);
    HOOK_API("kernel32.dll", "SetFileAttributesW", HookSetFileAttributesW, g_setAttrsW);
    MH_EnableHook(MH_ALL_HOOKS);
}
