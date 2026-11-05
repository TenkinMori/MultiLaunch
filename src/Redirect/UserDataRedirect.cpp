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
#include "runtime/il2cpp_safe.h"
#include "steam/steamhack.h"

void log_line(const char* msg);
void log_line(const std::string& msg);
void log_line(const std::wstring& msg);

static void InstallSteamHack();

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
static std::wstring g_suffix;

static bool ShouldRename(const std::wstring& name);

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


static std::wstring WideFromUtf8(const std::string& s)
{
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

void log_line(const char* msg) { log_line(std::string(msg ? msg : "")); }
void log_line(const std::wstring& msg) { LogLine(msg.c_str()); }
void log_line(const std::string& msg) { LogLine(WideFromUtf8(msg).c_str()); }

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

// ---- 可选：Steam 绕过（ASTRAL_STEAM_BYPASS=1）----
static BOOL g_steamBypass = FALSE;

// ---- 独立实现：把 AOT 的 SteamManager.Awake() 改成 ret（不依赖任何 loader）----
typedef void* (*FnDomainGet)();
typedef void** (*FnDomainGetAssemblies)(void*, size_t*);
typedef void* (*FnAssemblyGetImage)(void*);
typedef void* (*FnClassFromName)(void*, const char*, const char*);
typedef void* (*FnClassGetMethodFromName)(void*, const char*, int);
typedef void* (*FnThreadAttach)(void*);

static bool PatchRet(void* code)
{
    if (!code) return false;
    DWORD old = 0;
    if (!VirtualProtect(code, 1, PAGE_EXECUTE_READWRITE, &old)) return false;
    *(unsigned char*)code = 0xC3; // ret
    VirtualProtect(code, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), code, 1);
    return true;
}

static void PatchSteamManagerAwake()
{
    HMODULE ga = GetModuleHandleW(L"GameAssembly.dll");
    if (!ga) return;
    auto domain_get = (FnDomainGet)GetProcAddress(ga, "il2cpp_domain_get");
    auto domain_get_assemblies = (FnDomainGetAssemblies)GetProcAddress(ga, "il2cpp_domain_get_assemblies");
    auto assembly_get_image = (FnAssemblyGetImage)GetProcAddress(ga, "il2cpp_assembly_get_image");
    auto class_from_name = (FnClassFromName)GetProcAddress(ga, "il2cpp_class_from_name");
    auto class_get_method = (FnClassGetMethodFromName)GetProcAddress(ga, "il2cpp_class_get_method_from_name");
    auto thread_attach = (FnThreadAttach)GetProcAddress(ga, "il2cpp_thread_attach");
    if (!domain_get || !domain_get_assemblies || !assembly_get_image || !class_from_name || !class_get_method)
    {
        LogLine(L"bypass: il2cpp exports missing");
        return;
    }
    void* domain = domain_get();
    if (thread_attach) thread_attach(domain);
    size_t count = 0;
    void** assemblies = domain_get_assemblies(domain, &count);
    if (!assemblies) { LogLine(L"bypass: no assemblies"); return; }

    for (size_t i = 0; i < count; i++)
    {
        void* image = assembly_get_image(assemblies[i]);
        if (!image) continue;
        void* klass = class_from_name(image, "", "SteamManager");
        if (!klass) continue;
        void* method = class_get_method(klass, "Awake", 0);
        if (!method) { LogLine(L"bypass: SteamManager.Awake not found"); return; }
        void* code = *(void**)method; // MethodInfo.methodPointer 在偏移 0
        bool ok = PatchRet(code);
        wchar_t buf[128];
        swprintf_s(buf, L"bypass: SteamManager.Awake patched=%d code=%p", (int)ok, code);
        LogLine(buf);
        return;
    }
    LogLine(L"bypass: SteamManager class not found");
}

typedef bool(WINAPI* PFN_Init)();
typedef int(WINAPI* PFN_InitFlat)();
typedef bool(WINAPI* PFN_Restart)(unsigned int);
typedef void(WINAPI* PFN_Shutdown)();
static PFN_Init g_origInit = nullptr;
static PFN_InitFlat g_origInitFlat = nullptr;
static PFN_Restart g_origRestart = nullptr;
static PFN_Shutdown g_origShutdown = nullptr;

static bool WINAPI Hook_SteamInit() { LogLine(L"steam: Init -> true"); return true; }
static int WINAPI Hook_SteamInitFlat() { LogLine(L"steam: InitFlat -> 0"); return 0; }
static bool WINAPI Hook_SteamRestart(unsigned int) { LogLine(L"steam: RestartAppIfNecessary -> false"); return false; }
static void WINAPI Hook_SteamShutdown() { LogLine(L"steam: Shutdown (noop)"); }

static DWORD WINAPI SteamBypassThread(LPVOID)
{
    // 先等 GameAssembly/il2cpp 可用，再改 SteamManager.Awake
    for (int i = 0; i < 600; i++)
    {
        if (GetModuleHandleW(L"GameAssembly.dll")) { InstallSteamHack(); break; }
        Sleep(100);
    }
    for (int i = 0; i < 600; i++)
    {
        HMODULE mod = GetModuleHandleW(L"steam_api64.dll");
        if (mod)
        {
            MH_CreateHookApi(L"steam_api64.dll", "SteamAPI_Init", &Hook_SteamInit, reinterpret_cast<LPVOID*>(&g_origInit));
            MH_CreateHookApi(L"steam_api64.dll", "SteamAPI_InitFlat", &Hook_SteamInitFlat, reinterpret_cast<LPVOID*>(&g_origInitFlat));
            MH_CreateHookApi(L"steam_api64.dll", "SteamAPI_RestartAppIfNecessary", &Hook_SteamRestart, reinterpret_cast<LPVOID*>(&g_origRestart));
            MH_CreateHookApi(L"steam_api64.dll", "SteamAPI_Shutdown", &Hook_SteamShutdown, reinterpret_cast<LPVOID*>(&g_origShutdown));
            MH_EnableHook(MH_ALL_HOOKS);
            LogLine(L"steam bypass: steam_api64 hooks installed");
            return 0;
        }
        Sleep(100);
    }
    LogLine(L"steam bypass: steam_api64 not loaded");
    return 0;
}

static void* Proc(HMODULE ga, const char* name)
{
    return (void*)GetProcAddress(ga, name);
}

static bool FillIl2CppTable(cesium_safe::il2cpp_tbl& t)
{
    HMODULE ga = GetModuleHandleW(L"GameAssembly.dll");
    if (!ga) return false;
#define F(field, name) t.field = (decltype(t.field))Proc(ga, name)
    F(domain_get, "il2cpp_domain_get");
    F(assembly_get_image, "il2cpp_assembly_get_image");
    F(image_get_assembly, "il2cpp_image_get_assembly");
    F(class_from_name, "il2cpp_class_from_name");
    F(class_get_method_from_name, "il2cpp_class_get_method_from_name");
    F(class_get_methods, "il2cpp_class_get_methods");
    F(method_get_name, "il2cpp_method_get_name");
    F(method_get_param, "il2cpp_method_get_param");
    F(method_get_param_count, "il2cpp_method_get_param_count");
    F(runtime_invoke, "il2cpp_runtime_invoke");
    F(thread_attach, "il2cpp_thread_attach");
    F(domain_get_assemblies, "il2cpp_domain_get_assemblies");
    F(image_get_name, "il2cpp_image_get_name");
    F(get_corlib, "il2cpp_get_corlib");
    F(type_get_name, "il2cpp_type_get_name");
    F(array_class_get, "il2cpp_array_class_get");
    F(array_new, "il2cpp_array_new");
    F(array_object_header_size, "il2cpp_array_object_header_size");
    F(exception_get_message, "il2cpp_exception_get_message");
    F(string_chars, "il2cpp_string_chars");
    F(method_get_return_type, "il2cpp_method_get_return_type");
    F(type_get_object, "il2cpp_type_get_object");
    F(class_from_system_type, "il2cpp_class_from_system_type");
    F(object_new, "il2cpp_object_new");
    F(value_box, "il2cpp_value_box");
    F(class_get_property_from_name, "il2cpp_class_get_property_from_name");
    F(property_get_get_method, "il2cpp_property_get_get_method");
    F(object_get_class, "il2cpp_object_get_class");
    F(class_get_name, "il2cpp_class_get_name");
    F(class_is_valuetype, "il2cpp_class_is_valuetype");
    F(object_unbox, "il2cpp_object_unbox");
    F(class_get_parent, "il2cpp_class_get_parent");
    F(class_get_field_from_name, "il2cpp_class_get_field_from_name");
    F(field_get_offset, "il2cpp_field_get_offset");
    F(field_get_value, "il2cpp_field_get_value");
    F(field_get_value_object, "il2cpp_field_get_value_object");
    F(class_get_fields, "il2cpp_class_get_fields");
    F(field_get_name, "il2cpp_field_get_name");
    F(class_value_size, "il2cpp_class_value_size");
    F(array_length, "il2cpp_array_length");
    F(gchandle_new, "il2cpp_gchandle_new");
#undef F
    return t.complete();
}

static void InstallSteamHack()
{
    cesium_safe::il2cpp_tbl t;
    if (!FillIl2CppTable(t)) { LogLine(L"steamhack: il2cpp table incomplete"); return; }
    bool ok = steamhack_install(t, true, true, true, true, "auto", false, true);
    wchar_t buf[128];
    swprintf_s(buf, L"steamhack: install=%d hooks=%d", (int)ok, steamhack_hook_count());
    LogLine(buf);
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

    wchar_t buf[64] = {0};
    unsigned h = 0;
    for (size_t i = 0; i < g_localLow.size(); i++) h = h * 131 + (unsigned)g_localLow[i];
    swprintf_s(buf, L"_P%08X", h);
    g_suffix = buf;
    LogLine((L"profile=" + g_localLow + L" suffix=" + g_suffix).c_str());

    wchar_t bp[8] = {0};
    if (GetEnvironmentVariableW(L"ASTRAL_STEAM_BYPASS", bp, 8) > 0 && bp[0] == L'1')
    {
        g_steamBypass = TRUE;
        CreateThread(nullptr, 0, SteamBypassThread, nullptr, 0, nullptr);
    }

    // 只 hook SHGetKnownFolderPath：加载器初始化阶段不要动 ntdll
    if (MH_Initialize() != MH_OK) return TRUE;
    MH_STATUS st = MH_CreateHookApi(L"shell32.dll", "SHGetKnownFolderPath",
                                    &Hook_SHGetKnownFolderPath, reinterpret_cast<LPVOID*>(&g_origSHKFP));
    wchar_t dbg[128];
    swprintf_s(dbg, L"hook SHGetKnownFolderPath=%d", (int)st);
    LogLine(dbg);
    if (st == MH_OK) MH_EnableHook(MH_ALL_HOOKS);
    return TRUE;
}
