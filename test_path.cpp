#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <stdio.h>
int wmain()
{
    PWSTR p = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &p);
    wprintf(L"LocalAppDataLow hr=0x%08X path=%s\n", (unsigned)hr, p ? p : L"(null)");
    if (p) CoTaskMemFree(p);
    p = nullptr;
    hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p);
    wprintf(L"LocalAppData    hr=0x%08X path=%s\n", (unsigned)hr, p ? p : L"(null)");
    if (p) CoTaskMemFree(p);
    return 0;
}
