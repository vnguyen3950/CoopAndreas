#pragma once
#include <string>
#include <vector>
#include <windows.h>

class Launcher
{
public:
    static void LaunchProcess(const std::string& cmdLine)
    {
        // Get the launcher's actual location; shortcuts and shells can supply a different cwd.
        std::vector<wchar_t> modulePath(MAX_PATH);
        DWORD moduleLength = 0;
        for (;;)
        {
            moduleLength = GetModuleFileNameW(NULL, modulePath.data(), static_cast<DWORD>(modulePath.size()));
            if (moduleLength == 0)
            {
                const DWORD err = GetLastError();
                const std::wstring message = L"Could not determine the launcher folder (Error Code: " +
                    std::to_wstring(err) + L").\n\nGTA San Andreas was not launched.";
                MessageBoxW(NULL, message.c_str(), L"CoopAndreas Launcher Error", MB_ICONERROR | MB_OK);
                return;
            }
            if (moduleLength < modulePath.size())
                break;
            if (modulePath.size() >= 32768)
            {
                MessageBoxW(NULL, L"The launcher path is too long or was truncated.\n\nGTA San Andreas was not launched.",
                    L"CoopAndreas Launcher Error", MB_ICONERROR | MB_OK);
                return;
            }
            modulePath.resize(modulePath.size() > 16384 ? 32768 : modulePath.size() * 2);
        }

        const std::wstring launcherPath(modulePath.data(), moduleLength);
        const size_t lastSeparator = launcherPath.find_last_of(L"\\/");
        if (lastSeparator == std::wstring::npos)
        {
            MessageBoxW(NULL, L"Could not determine the launcher folder from its module path.\n\nGTA San Andreas was not launched.",
                L"CoopAndreas Launcher Error", MB_ICONERROR | MB_OK);
            return;
        }
        const std::wstring launcherFolder = launcherPath.substr(0, lastSeparator + 1);
        const std::wstring gamePath = launcherFolder + L"gta_sa.exe";

        // Preserve the caller's argument suffix, replacing only argv[0] with the absolute game path.
        // The existing caller uses ANSI strings; the executable and cwd use Unicode Windows paths.
        if (cmdLine.empty() || cmdLine.size() > 32766)
        {
            MessageBoxW(NULL, L"The game command line is empty or too long.\n\nGTA San Andreas was not launched.",
                L"CoopAndreas Launcher Error", MB_ICONERROR | MB_OK);
            return;
        }
        const int wideLength = MultiByteToWideChar(CP_ACP, 0, cmdLine.data(), static_cast<int>(cmdLine.size()), NULL, 0);
        std::wstring wideCommand(wideLength, L'\0');
        if (wideLength == 0 || MultiByteToWideChar(CP_ACP, 0, cmdLine.data(), static_cast<int>(cmdLine.size()),
                &wideCommand[0], wideLength) != wideLength)
        {
            MessageBoxW(NULL, L"Could not convert the game command line.\n\nGTA San Andreas was not launched.",
                L"CoopAndreas Launcher Error", MB_ICONERROR | MB_OK);
            return;
        }
        size_t argumentStart = wideCommand.find_first_not_of(L" \t");
        bool quoted = false;
        if (argumentStart != std::wstring::npos)
        {
            for (; argumentStart < wideCommand.size(); ++argumentStart)
            {
                const wchar_t c = wideCommand[argumentStart];
                if (c == L'"')
                    quoted = !quoted;
                else if (!quoted && (c == L' ' || c == L'\t'))
                    break;
            }
        }
        if (argumentStart == std::wstring::npos || quoted)
        {
            MessageBoxW(NULL, L"The game command line has an invalid executable argument.\n\nGTA San Andreas was not launched.",
                L"CoopAndreas Launcher Error", MB_ICONERROR | MB_OK);
            return;
        }
        const std::wstring childCommand = L"\"" + gamePath + L"\"" + wideCommand.substr(argumentStart);
        if (childCommand.size() > 32766)
        {
            MessageBoxW(NULL, L"The game command line is too long.\n\nGTA San Andreas was not launched.",
                L"CoopAndreas Launcher Error", MB_ICONERROR | MB_OK);
            return;
        }
        std::vector<wchar_t> args(childCommand.begin(), childCommand.end());
        args.push_back(L'\0');

        STARTUPINFOW si = {sizeof(si)};
        PROCESS_INFORMATION pi = {0};

        BOOL success = CreateProcessW(gamePath.c_str(), args.data(), NULL, NULL, FALSE, 0, NULL,
            launcherFolder.c_str(), &si, &pi);

        if (success)
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        else
        {
            std::string outErrorMessage;
            DWORD err = GetLastError();
            switch (err)
            {
                case ERROR_FILE_NOT_FOUND:
                case ERROR_PATH_NOT_FOUND:
                    if (GetFileAttributesW((launcherFolder + L"gta-sa.exe").c_str()) != INVALID_FILE_ATTRIBUTES)
                    {
                        outErrorMessage = "Steam version of GTA San Andreas detected (gta-sa.exe)!\n\n"
                                          "This mod requires GTA:SA v1.0 US HOODLUM.\n\n"
                                          "Please use a downgrader to convert your game to v1.0 US HOODLUM.";
                    }
                    else
                    {
                        outErrorMessage =
                            "Could not find gta_sa.exe!\n\n"
                            "Please make sure the launcher is placed in your GTA San Andreas installation folder.";
                    }
                    break;

                case ERROR_ACCESS_DENIED:
                case ERROR_ELEVATION_REQUIRED:
                    outErrorMessage = "Windows blocked GTA San Andreas from launching.\n\n"
                                      "Try right-clicking the launcher and selecting \"Run as administrator\".";
                    break;

                case ERROR_BAD_EXE_FORMAT:
                    outErrorMessage =
                        "The gta_sa.exe file appears to be corrupted or invalid.\n\n"
                        "If you are using a modified executable, make sure it is a valid v1.0 US HOODLUM gta_sa.exe.";
                    break;

                case ERROR_CANCELLED:
                    outErrorMessage = "Launch canceled. The Windows Administrator prompt (UAC) was declined.";
                    break;

                case ERROR_SHARING_VIOLATION:
                case ERROR_LOCK_VIOLATION:
                    outErrorMessage =
                        "GTA San Andreas is already running!\n\n"
                        "Please close any open instances of GTA:SA or check Task Manager before trying again.";
                    break;

                default:
                    outErrorMessage = "Failed to launch GTA San Andreas (Error Code: " + std::to_string(err) +
                                      ").\n\n"
                                      "If this keeps happening, ask for help in our Discord server!";
                    break;
            }
            MessageBoxA(NULL, outErrorMessage.c_str(), "CoopAndreas Launcher Error", MB_ICONERROR | MB_OK);
        }
    }
};
