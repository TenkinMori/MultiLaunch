// UserDataRedirect.dll
// 按环境变量 ASTRAL_PROFILE_DIR 重定向 LocalLow 路径：
//   <profile>\AppData\LocalLow
// 通过 inline hook SHGetKnownFolderPath(FOLDERID_LocalAppDataLow) 实现。
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <string>
#include <cstdio>
#include <winternl.h>
#include "MinHook.h"

static std::wstring g_localLow;
static BOOL g_enabled = FALSE;
static void LogLine(const wchar_t* msg)
{
    wchar_t path[MAX_PATH] = {0};
    DWORD n = GetEnvironmentVariableW(L"ASTRAL_LOG", path, MAX_PATH);
    if (n == 0) return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"a, ccs=UTF-8") != 0 || !f) return;
    fwprintf(f, L"%s\n", msg);
    fclose(f);
}

typedef HRESULT(WINAPI* PFN_SHKFP)(REFKNOWNFOLDERID, DWORD, HANDLE, PWSTR*);
static PFN_SHKFP g_origSHKFP = nullptr;

// ---- ntdll mutants (catch everything) ----
typedef NTSTATUS(NTAPI* PFN_NtCreateMutant)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, BOOLEAN);
typedef NTSTATUS(NTAPI* PFN_NtOpenMutant)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
static PFN_NtCreateMutant g_origNtCreateMutant = nullptr;
static PFN_NtOpenMutant g_origNtOpenMutant = nullptr;
static thread_local wchar_t g_nameBuf[512];

static POBJECT_ATTRIBUTES RewriteAttrs(POBJECT_ATTRIBUTES attrs)
{
    if (!g_enabled || attrs == nullptr || attrs->ObjectName == nullptr) return attrs;
    UNICODE_STRING* us = attrs->ObjectName;
    if (us->Buffer == nullptr || us->Length == 0 || g_suffix.empty()) return attrs;
    std::wstring name(us->Buffer, us->Length / sizeof(wchar_t));
    if (!ShouldRename(name)) return attrs;
    std::wstring full = name + g_suffix;
    if (full.size() >= 500) return attrs;
    memcpy(g_nameBuf, full.c_str(), full.size() * sizeof(wchar_t));
    g_nameBuf[full.size()] = 0;
    us->Buffer = g_nameBuf;
    us->Length = (USHORT)(full.size() * sizeof(wchar_t));
    us->MaximumLength = (USHORT)((full.size() + 1) * sizeof(wchar_t));
    LogLine((std::wstring(L"mutant: ") + name).c_str());
    return attrs;
}

static NTSTATUS NTAPI Hook_NtCreateMutant(PHANDLE h, ACCESS_MASK a, POBJECT_ATTRIBUTES oa, BOOLEAN owner)
{
    return g_origNtCreateMutant(h, a, RewriteAttrs(oa), owner);
}

static NTSTATUS NTAPI Hook_NtOpenMutant(PHANDLE h, ACCESS_MASK a, POBJECT_ATTRIBUTES oa)
{
    return g_origNtOpenMutant(h, a, RewriteAttrs(oa));
}

// ---- single-instance mutex rename ----
static std::wstring g_suffix;
typedef HANDLE(WINAPI* PFN_CreateMutexW)(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR);
typedef HANDLE(WINAPI* PFN_CreateMutexExW)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
typedef HANDLE(WINAPI* PFN_OpenMutexW)(DWORD, BOOL, LPCWSTR);
static PFN_CreateMutexW g_origCreateMutexW = nullptr;
static PFN_CreateMutexExW g_origCreateMutexExW = nullptr;
static PFN_OpenMutexW g_origOpenMutexW = nullptr;

static bool ShouldRename(const std::wstring& name)
{
    if (name.empty()) return false;
    if (name.find(L"steam") != std::wstring::npos) return false;
    if (name.find(L"Steam") != std::wstring::npos) return false;
    if (name.find(L"STEAM") != std::wstring::npos) return false;
    return true;
}

static std::wstring RenameMutex(const wchar_t* name)
{
    if (name && *name) { LogLine((std::wstring(L"mutex: ") + name).c_str()); }
    if (!g_enabled || name == nullptr || g_suffix.empty()) return name ? std::wstring(name) : std::wstring();
    std::wstring n(name);
    if (!ShouldRename(n)) return n;
    return n + g_suffix;
}

static HANDLE WINAPI Hook_CreateMutexW(LPSECURITY_ATTRIBUTES sa, BOOL initial, LPCWSTR name)
{
    std::wstring n = RenameMutex(name);
    return g_origCreateMutexW(sa, initial, name ? n.c_str() : nullptr);
}

static HANDLE WINAPI Hook_CreateMutexExW(LPSECURITY_ATTRIBUTES sa, LPCWSTR name, DWORD flags, DWORD access)
{
    std::wstring n = RenameMutex(name);
    return g_origCreateMutexExW(sa, name ? n.c_str() : nullptr, flags, access);
}

// ---- ANSI 变体 ----
typedef HANDLE(WINAPI* PFN_CreateMutexA)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
typedef HANDLE(WINAPI* PFN_OpenMutexA)(DWORD, BOOL, LPCSTR);
static PFN_CreateMutexA g_origCreateMutexA = nullptr;
static PFN_OpenMutexA g_origOpenMutexA = nullptr;

// ---- broad logging of other sync objects ----
typedef HANDLE(WINAPI* PFN_CreateEventW)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR);
typedef HANDLE(WINAPI* PFN_OpenEventW)(DWORD, BOOL, LPCWSTR);
typedef HANDLE(WINAPI* PFN_CreateFileMappingW)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, LPCWSTR);
typedef HANDLE(WINAPI* PFN_OpenFileMappingW)(DWORD, BOOL, LPCWSTR);
static PFN_CreateEventW g_origCreateEventW = nullptr;
static PFN_OpenEventW g_origOpenEventW = nullptr;
static PFN_CreateFileMappingW g_origCreateFileMappingW = nullptr;
static PFN_OpenFileMappingW g_origOpenFileMappingW = nullptr;

static void LogEventName(const wchar_t* kind, const wchar_t* name)
{
    if (name && *name) LogLine((std::wstring(kind) + L": " + name).c_str());
}
static HANDLE WINAPI Hook_CreateEventW(LPSECURITY_ATTRIBUTES a, BOOL m, BOOL i, LPCWSTR n)
{ LogEventName(L"event-create", n); return g_origCreateEventW(a, m, i, n); }
static HANDLE WINAPI Hook_OpenEventW(DWORD d, BOOL i, LPCWSTR n)
{ LogEventName(L"event-open", n); return g_origOpenEventW(d, i, n); }
static HANDLE WINAPI Hook_CreateFileMappingW(HANDLE f, LPSECURITY_ATTRIBUTES a, DWORD p, DWORD hi, DWORD lo, LPCWSTR n)
{ LogEventName(L"map-create", n); return g_origCreateFileMappingW(f, a, p, hi, lo, n); }
static HANDLE WINAPI Hook_OpenFileMappingW(DWORD d, BOOL i, LPCWSTR n)
{ LogEventName(L"map-open", n); return g_origOpenFileMappingW(d, i, n); }

static HANDLE WINAPI Hook_CreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL initial, LPCSTR name)
{
    if (name) {
        int n = MultiByteToWideChar(CP_ACP, 0, name, -1, nullptr, 0);
        std::wstring w(n ? n : 0, L'\0');
        if (n) MultiByteToWideChar(CP_ACP, 0, name, -1, &w[0], n);
        std::wstring r = RenameMutex(w.c_str());
        int m = WideCharToMultiByte(CP_ACP, 0, r.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string a(m ? m : 0, '\0');
        if (m) WideCharToMultiByte(CP_ACP, 0, r.c_str(), -1, &a[0], m, nullptr, nullptr);
        return g_origCreateMutexA(sa, initial, a.c_str());
    }
    return g_origCreateMutexA(sa, initial, nullptr);
}

static HANDLE WINAPI Hook_OpenMutexA(DWORD access, BOOL inherit, LPCSTR name)
{
    if (name) {
        int n = MultiByteToWideChar(CP_ACP, 0, name, -1, nullptr, 0);
        std::wstring w(n ? n : 0, L'\0');
        if (n) MultiByteToWideChar(CP_ACP, 0, name, -1, &w[0], n);
        std::wstring r = RenameMutex(w.c_str());
        int m = WideCharToMultiByte(CP_ACP, 0, r.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string a(m ? m : 0, '\0');
        if (m) WideCharToMultiByte(CP_ACP, 0, r.c_str(), -1, &a[0], m, nullptr, nullptr);
        return g_origOpenMutexA(access, inherit, a.c_str());
    }
    return g_origOpenMutexA(access, inherit, nullptr);
}

static HANDLE WINAPI Hook_OpenMutexW(DWORD access, BOOL inherit, LPCWSTR name)
{
    std::wstring n = RenameMutex(name);
    return g_origOpenMutexW(access, inherit, name ? n.c_str() : nullptr);
}


static std::wstring WideFromEnv(const wchar_t* name)
{
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return L"";
    std::wstring buf(n - 1, L'\0');
    GetEnvironmentVariableW(name, &buf[0], n);
    return buf;
}

static void EnsureDir(const std::wstring& path)
{
    if (path.empty()) return;
    // 逐级创建（不追求 UWP 完整性，普通路径够用）
    std::wstring cur;
    for (size_t i = 0; i < path.size(); i++)
    {
        cur += path[i];
        if (path[i] == L'\\' || i + 1 == path.size())
        {
            std::wstring d = cur;
            while (!d.empty() && d.back() == L'\\') d.pop_back();
            if (d.size() > 3) CreateDirectoryW(d.c_str(), nullptr);
        }
    }
}

static HRESULT WINAPI Hook_SHGetKnownFolderPath(REFKNOWNFOLDERID rfid, DWORD flags, HANDLE token, PWSTR* out)
{
    if (!g_enabled || out == nullptr) return g_origSHKFP ? g_origSHKFP(rfid, flags, token, out) : E_FAIL;

    const bool wantLow = (rfid == FOLDERID_LocalAppDataLow);
    const bool wantLocal = (rfid == FOLDERID_LocalAppData);
    if (!wantLow && !wantLocal)
        return g_origSHKFP(rfid, flags, token, out);

    std::wstring path = wantLow ? g_localLow : g_localLow.substr(0, g_localLow.find_last_of(L'\\'));
    size_t bytes = (path.size() + 1) * sizeof(wchar_t);
    PWSTR mem = (PWSTR)CoTaskMemAlloc(bytes);
    if (!mem) return E_OUTOFMEMORY;
    memcpy(mem, path.c_str(), bytes);
    *out = mem;
    return S_OK;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;

    std::wstring root = WideFromEnv(L"ASTRAL_PROFILE_DIR");
    if (root.empty()) return TRUE;
    if (!root.empty() && root.back() == L'\\') root.pop_back();

    g_localLow = root + L"\\AppData\\LocalLow";
    EnsureDir(g_localLow);
    g_enabled = TRUE;

    // 多开：给当前 profile 一个唯一后缀
    {
        wchar_t buf[64] = {0};
        unsigned h = 0;
        for (size_t i = 0; i < g_localLow.size(); i++) h = h * 131 + (unsigned)g_localLow[i];
        swprintf_s(buf, L"_P%08X", h);
        g_suffix = buf;
        LogLine((L"profile=" + g_localLow + L" suffix=" + g_suffix).c_str());
    }

    if (MH_Initialize() != MH_OK) return TRUE;
    MH_STATUS st1 = MH_CreateHookApi(L"kernel32.dll", "CreateMutexW", &Hook_CreateMutexW, reinterpret_cast<LPVOID*>(&g_origCreateMutexW));
    MH_STATUS st2 = MH_CreateHookApi(L"kernel32.dll", "CreateMutexExW", &Hook_CreateMutexExW, reinterpret_cast<LPVOID*>(&g_origCreateMutexExW));
    MH_STATUS st3 = MH_CreateHookApi(L"kernel32.dll", "OpenMutexW", &Hook_OpenMutexW, reinterpret_cast<LPVOID*>(&g_origOpenMutexW));
    MH_CreateHookApi(L"kernel32.dll", "CreateMutexA", &Hook_CreateMutexA, reinterpret_cast<LPVOID*>(&g_origCreateMutexA));
    MH_CreateHookApi(L"ntdll.dll", "NtCreateMutant", &Hook_NtCreateMutant, reinterpret_cast<LPVOID*>(&g_origNtCreateMutant));
    MH_CreateHookApi(L"ntdll.dll", "NtOpenMutant", &Hook_NtOpenMutant, reinterpret_cast<LPVOID*>(&g_origNtOpenMutant));
    MH_CreateHookApi(L"kernel32.dll", "CreateEventW", &Hook_CreateEventW, reinterpret_cast<LPVOID*>(&g_origCreateEventW));
    MH_CreateHookApi(L"kernel32.dll", "OpenEventW", &Hook_OpenEventW, reinterpret_cast<LPVOID*>(&g_origOpenEventW));
    MH_CreateHookApi(L"kernel32.dll", "CreateFileMappingW", &Hook_CreateFileMappingW, reinterpret_cast<LPVOID*>(&g_origCreateFileMappingW));
    MH_CreateHookApi(L"kernel32.dll", "OpenFileMappingW", &Hook_OpenFileMappingW, reinterpret_cast<LPVOID*>(&g_origOpenFileMappingW));
    MH_CreateHookApi(L"kernel32.dll", "OpenMutexA", &Hook_OpenMutexA, reinterpret_cast<LPVOID*>(&g_origOpenMutexA));
    wchar_t dbg[256];
    swprintf_s(dbg, L"hooks: cm=%d cme=%d om=%d", (int)st1, (int)st2, (int)st3);
    LogLine(dbg);
    if (MH_CreateHookApi(L"shell32.dll", "SHGetKnownFolderPath",
                         &Hook_SHGetKnownFolderPath, reinterpret_cast<LPVOID*>(&g_origSHKFP)) == MH_OK)
        MH_EnableHook(MH_ALL_HOOKS);

    return TRUE;
}
