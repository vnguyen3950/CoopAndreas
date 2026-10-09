#include "pch.h"
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <errhandlingapi.h>
#include <windows.h>

#pragma comment(linker, "/export:DllCanUnloadNow=eax_orig.DllCanUnloadNow,@1")
#pragma comment(linker, "/export:DllGetClassObject=eax_orig.DllGetClassObject,@2")
#pragma comment(linker, "/export:DllRegisterServer=eax_orig.DllRegisterServer,@3")
#pragma comment(linker, "/export:DllUnregisterServer=eax_orig.DllUnregisterServer,@4")
#pragma comment(linker, "/export:EAXDirectSoundCreate=eax_orig.EAXDirectSoundCreate,@5")
#pragma comment(linker, "/export:EAXDirectSoundCreate8=eax_orig.EAXDirectSoundCreate8,@6")
#pragma comment(linker, "/export:GetCurrentVersion=eax_orig.GetCurrentVersion,@7")

bool NeedToInjectCoopAndreas()
{
    LPTSTR cmd = GetCommandLine();
    return strstr(cmd, "--coop") != nullptr;
}

HMODULE hCoopAndreas = NULL;
HMODULE hWindowedMode = NULL;

void LoadOptionalWindowedMode(HMODULE proxyModule)
{
    wchar_t modulePath[32768]{};
    DWORD length = GetModuleFileNameW(proxyModule, modulePath, _countof(modulePath));
    if (!length || length >= _countof(modulePath))
        return;

    std::wstring pluginPath(modulePath, length);
    size_t separator = pluginPath.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
        return;
    pluginPath.resize(separator + 1);
    pluginPath += L"III.VC.SA.WindowedMode.asi";

    DWORD attributes = GetFileAttributesW(pluginPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return;

    std::wstring configPath = pluginPath;
    configPath.replace(configPath.size() - 4, 4, L".ini");
    if (!GetPrivateProfileIntW(L"loader", L"enabled", 1, configPath.c_str()))
        return;

    // Its window/device hooks must remain loaded until the game process exits.
    hWindowedMode = LoadLibraryW(pluginPath.c_str());
    if (!hWindowedMode)
    {
        DWORD error = GetLastError();
        wchar_t message[256];
        swprintf_s(message, _countof(message),
            L"Could not load the optional windowed-mode helper.\n\nWindows error: %lu\n\n"
            L"CoopAndreas will continue without it.", error);
        MessageBoxW(NULL, message, L"CoopAndreas Windowed Mode", MB_OK | MB_ICONERROR);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
            LoadOptionalWindowedMode(hModule);
            if (!NeedToInjectCoopAndreas())
            {
                break;
            }
            hCoopAndreas = LoadLibrary("CoopAndreasSA.dll");
            if (!hCoopAndreas)
            {
                char msg[256];
                sprintf(msg,
                    "Failed to load CoopAndreasSA.dll.\n\n"
                    "To uninstall CoopAndreas properly:\n"
                    "1. Delete 'eax.dll'.\n"
                    "2. Rename 'eax_orig.dll' back to 'eax.dll'.\n\n"
                    "To play CoopAndreas again:\n"
                    "Reinstall the mod.\n\nGetLastError() == %u",
                    GetLastError());

                MessageBox(0, msg, "CoopAndreas Loader", MB_OK | MB_ICONERROR);
            }
            break;
        case DLL_PROCESS_DETACH:
            if (!NeedToInjectCoopAndreas())
            {
                break;
            }
            if (hCoopAndreas)
            {
                FreeLibrary(hCoopAndreas);
            }
            break;
    }
    return TRUE;
}
