#define STRICT
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdbool.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

#define WM_APP_TRAYICON       (WM_APP + 1)
#define WM_APP_SWITCH_DESKTOP (WM_APP + 2)
#define WM_APP_TRIGGER_ACTION (WM_APP + 3)

#define IDM_DESKTOP_SWITCH 1001
#define IDM_WIN_GESTURE    1002
#define IDM_EXIT           1003

HHOOK g_hKbdHook = NULL;
HHOOK g_hMouseHook = NULL;
HWND g_hwnd = NULL;
NOTIFYICONDATA nid = {0};

bool g_bWinKeyDown = false;
bool g_bHasComboHappened = false;
bool g_bScrollHandled = false;
ULONGLONG g_lastWinUpTime = 0;
bool g_bDesktopSwitchEnabled = true;
bool g_bWinGestureEnabled = true;

static UINT WM_TASKBARCREATED = 0;
const ULONG_PTR INJECT_TAG = 0xDEADBEEF;

void ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    InsertMenu(hMenu, 0, MF_BYPOSITION | MF_STRING | (g_bDesktopSwitchEnabled ? MF_CHECKED : MF_UNCHECKED), IDM_DESKTOP_SWITCH, TEXT("Desktop Switch"));
    InsertMenu(hMenu, 1, MF_BYPOSITION | MF_STRING | (g_bWinGestureEnabled ? MF_CHECKED : MF_UNCHECKED), IDM_WIN_GESTURE, TEXT("Win Gestures"));
    InsertMenu(hMenu, 2, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenu(hMenu, 3, MF_BYPOSITION | MF_STRING, IDM_EXIT, TEXT("Exit"));
    
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
    PostMessage(hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

void TriggerAction(int actionType) {
    INPUT inputs[4] = {0};
    for(int i = 0; i < 4; i++) {
        inputs[i].type = INPUT_KEYBOARD;
        inputs[i].ki.dwExtraInfo = INJECT_TAG;
    }

    if (actionType == 0) {
        inputs[0].ki.wVk = VK_LWIN;
        inputs[1].ki.wVk = VK_TAB;
        
        inputs[2].ki.wVk = VK_TAB;
        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
        
        inputs[3].ki.wVk = VK_LWIN;
        inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    } else if (actionType == 1) {
        inputs[0].ki.wVk = VK_LCONTROL;
        inputs[1].ki.wVk = VK_ESCAPE;
        
        inputs[2].ki.wVk = VK_ESCAPE;
        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
        
        inputs[3].ki.wVk = VK_LCONTROL;
        inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    }
    
    SendInput(4, inputs, sizeof(INPUT));
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_TASKBARCREATED && WM_TASKBARCREATED != 0) {
        Shell_NotifyIcon(NIM_ADD, &nid);
        return 0;
    }

    switch(msg) {
        case WM_CREATE:
            WM_TASKBARCREATED = RegisterWindowMessage(TEXT("TaskbarCreated"));
            break;
        case WM_APP_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
                ShowContextMenu(hwnd);
            }
            break;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDM_DESKTOP_SWITCH) {
                g_bDesktopSwitchEnabled = !g_bDesktopSwitchEnabled;
            } else if (LOWORD(wParam) == IDM_WIN_GESTURE) {
                g_bWinGestureEnabled = !g_bWinGestureEnabled;
            } else if (LOWORD(wParam) == IDM_EXIT) {
                PostQuitMessage(0);
            }
            break;
        case WM_APP_SWITCH_DESKTOP: {
            int direction = (int)wParam;
            WORD vkDir = (direction == 0) ? VK_LEFT : VK_RIGHT;
            
            INPUT inputs[4] = {0};
            for(int i = 0; i < 4; i++) {
                inputs[i].type = INPUT_KEYBOARD;
                inputs[i].ki.dwExtraInfo = INJECT_TAG;
            }

            inputs[0].ki.wVk = VK_LCONTROL;
            
            inputs[1].ki.wVk = vkDir;
            inputs[1].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
            
            inputs[2].ki.wVk = vkDir;
            inputs[2].ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP;
            
            inputs[3].ki.wVk = VK_LCONTROL;
            inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

            SendInput(4, inputs, sizeof(INPUT));
            break;
        }
        case WM_APP_TRIGGER_ACTION:
            TriggerAction((int)wParam);
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT *pKey = (KBDLLHOOKSTRUCT *)lParam;
        
        if (pKey->dwExtraInfo == INJECT_TAG) {
            return CallNextHookEx(g_hKbdHook, nCode, wParam, lParam);
        }
        
        if (!g_bWinGestureEnabled) {
            return CallNextHookEx(g_hKbdHook, nCode, wParam, lParam);
        }
        
        bool isWinKey = (pKey->vkCode == VK_LWIN || pKey->vkCode == VK_RWIN);
        
        if (isWinKey) {
            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                if (!g_bWinKeyDown) {
                    g_bWinKeyDown = true;
                    g_bHasComboHappened = false;
                    g_bScrollHandled = false;
                }
                return CallNextHookEx(g_hKbdHook, nCode, wParam, lParam);
            } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                g_bWinKeyDown = false;
                
                if (g_bScrollHandled) {
                    INPUT inputs[3] = {0};
                    inputs[0].type = INPUT_KEYBOARD;
                    inputs[0].ki.wVk = VK_LCONTROL;
                    inputs[0].ki.dwExtraInfo = INJECT_TAG;
                    
                    inputs[1].type = INPUT_KEYBOARD;
                    inputs[1].ki.wVk = VK_LCONTROL;
                    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    inputs[1].ki.dwExtraInfo = INJECT_TAG;
                    
                    inputs[2].type = INPUT_KEYBOARD;
                    inputs[2].ki.wVk = (WORD)pKey->vkCode;
                    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
                    inputs[2].ki.dwExtraInfo = INJECT_TAG;
                    
                    SendInput(3, inputs, sizeof(INPUT));
                    return 1;
                } else if (!g_bHasComboHappened) {
                    ULONGLONG now = GetTickCount64();
                    UINT threshold = GetDoubleClickTime();
                    
                    if (g_lastWinUpTime != 0 && (now - g_lastWinUpTime <= threshold)) {
                        PostMessage(g_hwnd, WM_APP_TRIGGER_ACTION, 1, 0);
                        g_lastWinUpTime = 0;
                    } else {
                        PostMessage(g_hwnd, WM_APP_TRIGGER_ACTION, 0, 0);
                        g_lastWinUpTime = now;
                    }

                    INPUT inputs[3] = {0};
                    inputs[0].type = INPUT_KEYBOARD;
                    inputs[0].ki.wVk = VK_LCONTROL;
                    inputs[0].ki.dwExtraInfo = INJECT_TAG;
                    
                    inputs[1].type = INPUT_KEYBOARD;
                    inputs[1].ki.wVk = VK_LCONTROL;
                    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    inputs[1].ki.dwExtraInfo = INJECT_TAG;
                    
                    inputs[2].type = INPUT_KEYBOARD;
                    inputs[2].ki.wVk = (WORD)pKey->vkCode;
                    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
                    inputs[2].ki.dwExtraInfo = INJECT_TAG;
                    
                    SendInput(3, inputs, sizeof(INPUT));
                    return 1;
                }
            }
        } else if (g_bWinKeyDown) {
            g_bHasComboHappened = true;
        }
    }
    return CallNextHookEx(g_hKbdHook, nCode, wParam, lParam);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_MOUSEWHEEL) {
        if (!g_bDesktopSwitchEnabled) {
            return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
        }
        
        bool realWinDown = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
        if (g_bWinKeyDown && !realWinDown) {
            g_bWinKeyDown = false; 
        }
        
        if (g_bWinKeyDown) {
            MSLLHOOKSTRUCT *pMouse = (MSLLHOOKSTRUCT *)lParam;
            short zDelta = (short)HIWORD(pMouse->mouseData);
            
            g_bScrollHandled = true;
            
            static DWORD s_lastScrollTime = 0;
            DWORD now = GetTickCount();
            
            if (now - s_lastScrollTime > 100) {
                s_lastScrollTime = now;
                PostMessage(g_hwnd, WM_APP_SWITCH_DESKTOP, (zDelta > 0) ? 0 : 1, 0);
            }
            
            return 1;
        }
    }
    return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = TEXT("WinWheelClass");
    RegisterClass(&wc);

    g_hwnd = CreateWindowEx(0, wc.lpszClassName, TEXT("WinWheel"), 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    lstrcpy(nid.szTip, TEXT("WinWheel v2.0"));
    Shell_NotifyIcon(NIM_ADD, &nid);

    g_hKbdHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInstance, 0);
    g_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, hInstance, 0);

    MSG msg;
    int bRet;
    while ((bRet = GetMessage(&msg, NULL, 0, 0)) != 0) {
        if (bRet == -1) break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(g_hKbdHook);
    UnhookWindowsHookEx(g_hMouseHook);
    
    nid.uFlags = 0;
    Shell_NotifyIcon(NIM_DELETE, &nid);

    return (int)msg.wParam;
}