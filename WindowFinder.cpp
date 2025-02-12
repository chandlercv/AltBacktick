#include "WindowFinder.h"

#include "Config.h"

#include <psapi.h>
#include <vector>

using namespace std;

struct EnumWindowsCallbackArgs {
    EnumWindowsCallbackArgs(WindowFinder *windowFinder) : windowFinder(windowFinder) {}
    WindowFinder *windowFinder;
    vector<HWND> windowHandles;
};

wstring WindowFinder::GetProcessNameFromProcessId(DWORD processId) {
    wchar_t filename[MAX_PATH] = {0};
    HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (processHandle != nullptr) {
        GetModuleFileNameEx(processHandle, NULL, filename, MAX_PATH);
        CloseHandle(processHandle);
    }

    return wstring(filename);
}

std::wstring WindowFinder::GetCurrentDesktopId() const {
    if (desktopManager == NULL) {
        return L"";
    }

    GUID desktopId{};
    HRESULT result = desktopManager->GetWindowDesktopId(GetForegroundWindow(), &desktopId);
    if (FAILED(result)) {
        return L"";
    }

    wchar_t guidString[39];
    StringFromGUID2(desktopId, guidString, 39);

    return wstring(guidString);
}

wstring WindowFinder::GetProcessUniqueId(HWND windowHandle) const {
    DWORD processId;
    GetWindowThreadProcessId(windowHandle, &processId);
    wstring processName = GetProcessNameFromProcessId(processId);
    if (processName.empty()) {
        return L"";
    }

    wstring desktopId = GetCurrentDesktopId();

    return desktopId + processName;
}

wstring WindowFinder::GetCurrentProcessUniqueId() const {
    return GetProcessUniqueId(GetForegroundWindow());
}

BOOL WindowFinder::IsWindowOnCurrentDesktop(HWND windowHandle) const {
    if (desktopManager == NULL) {
        return TRUE;
    }

    BOOL isWindowOnCurrentDesktop;
    if (SUCCEEDED(desktopManager->IsWindowOnCurrentVirtualDesktop(windowHandle, &isWindowOnCurrentDesktop)) &&
        !isWindowOnCurrentDesktop) {
        return FALSE;
    }

    return TRUE;
}

BOOL WindowFinder::IsWindowFromCurrentProcess(HWND windowHandle) const {
    return GetProcessUniqueId(windowHandle) == GetCurrentProcessUniqueId();
}

BOOL CALLBACK EnumWindowsCallback(HWND windowHandle, LPARAM parameters) {
    EnumWindowsCallbackArgs *args = (EnumWindowsCallbackArgs *)parameters;

    if (GetForegroundWindow() == windowHandle) {
        return TRUE;
    }

    if (Config::GetInstance()->IgnoreMinimizedWindows() && IsIconic(windowHandle)) {
        return TRUE;
    }

    if (GetWindow(windowHandle, GW_OWNER) != (HWND)0 || !IsWindowVisible(windowHandle)) {
        return TRUE;
    }

    DWORD windowStyle = (DWORD)GetWindowLongPtr(windowHandle, GWL_STYLE);
    DWORD windowExtendedStyle = (DWORD)GetWindowLongPtr(windowHandle, GWL_EXSTYLE);
    BOOL hasAppWindowStyle = (windowExtendedStyle & WS_EX_APPWINDOW) != 0;
    if ((windowStyle & WS_POPUP) != 0 && !hasAppWindowStyle) {
        return TRUE;
    }

    if ((windowExtendedStyle & WS_EX_TOOLWINDOW) != 0) {
        return TRUE;
    }

    WINDOWINFO windowInfo;
    windowInfo.cbSize = sizeof(WINDOWINFO);
    GetWindowInfo(windowHandle, &windowInfo);
    if (windowInfo.rcWindow.right - windowInfo.rcWindow.left <= 1 ||
        windowInfo.rcWindow.bottom - windowInfo.rcWindow.top <= 1) {
        return TRUE;
    }

    // Relying on PID may not be accurate with some programs, instead rely on the process name.
    if (!args->windowFinder->IsWindowFromCurrentProcess(windowHandle)) {
        return TRUE;
    }

    if (!args->windowFinder->IsWindowOnCurrentDesktop(windowHandle)) {
        return TRUE;
    }

    args->windowHandles.push_back(windowHandle);

    return TRUE;
}

std::vector<HWND> WindowFinder::FindCurrentProcessWindows() {
    EnumWindowsCallbackArgs args(this);
    EnumWindows((WNDENUMPROC)&EnumWindowsCallback, (LPARAM)&args);

    return args.windowHandles;
}

WindowFinder::WindowFinder() : desktopManager(nullptr) {
    if (SUCCEEDED(CoInitialize(nullptr))) {
        CoCreateInstance(CLSID_VirtualDesktopManager, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&desktopManager));
    }
}
