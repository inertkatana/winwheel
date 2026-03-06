#define STRICT
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdbool.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

#define WM_APP_TRAYICON       (WM_APP + 1)
#define WM_APP_SWITCH_DESKTOP (WM_APP + 2)

#define IDM_PAUSE 1001
#define IDM_EXIT  1002

HHOOK g_hKbdHook = NULL;
HHOOK g_hMouseHook = NULL;
HWND g_hwnd = NULL;
NOTIFYICONDATA nid = {0};

bool g_bWinKeyDown = false;
bool g_bScrolledWithWin = false;
bool g_bPaused = false;

static UINT WM_TASKBARCREATED = 0;

void ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    InsertMenu(hMenu, 0, MF_BYPOSITION | MF_STRING | (g_bPaused ? MF_CHECKED : MF_UNCHECKED), IDM_PAUSE, TEXT("Pause/Resume"));
    InsertMenu(hMenu, 1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenu(hMenu, 2, MF_BYPOSITION | MF_STRING, IDM_EXIT, TEXT("Exit"));
    
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
    PostMessage(hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
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
            if (LOWORD(wParam) == IDM_PAUSE) {
                g_bPaused = !g_bPaused;
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
        bool isInjected = (pKey->flags & LLKHF_INJECTED) != 0;
        
        if (!isInjected && !g_bPaused) {
            if (pKey->vkCode == VK_LWIN || pKey->vkCode == VK_RWIN) {
                if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                    g_bWinKeyDown = true;
                } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                    g_bWinKeyDown = false;
                    
                    if (g_bScrolledWithWin) {
                        g_bScrolledWithWin = false;
                        
                        INPUT inputs[3] = {0};
                        
                        inputs[0].type = INPUT_KEYBOARD;
                        inputs[0].ki.wVk = VK_LCONTROL;
                        
                        inputs[1].type = INPUT_KEYBOARD;
                        inputs[1].ki.wVk = VK_LCONTROL;
                        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
                        
                        inputs[2].type = INPUT_KEYBOARD;
                        inputs[2].ki.wVk = (WORD)pKey->vkCode;
                        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
                        
                        SendInput(3, inputs, sizeof(INPUT));
                        
                        return 1;
                    }
                }
            }
        }
    }
    return CallNextHookEx(g_hKbdHook, nCode, wParam, lParam);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_MOUSEWHEEL) {
        if (!g_bPaused) {
            bool realWinDown = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
            if (g_bWinKeyDown && !realWinDown) {
                g_bWinKeyDown = false; 
            }
            
            if (g_bWinKeyDown) {
                MSLLHOOKSTRUCT *pMouse = (MSLLHOOKSTRUCT *)lParam;
                short zDelta = (short)HIWORD(pMouse->mouseData);
                
                g_bScrolledWithWin = true;
                
                static DWORD s_lastScrollTime = 0;
                DWORD now = GetTickCount();
                
                if (now - s_lastScrollTime > 100) {
                    s_lastScrollTime = now;
                    PostMessage(g_hwnd, WM_APP_SWITCH_DESKTOP, (zDelta > 0) ? 0 : 1, 0);
                }
                
                return 1;
            }
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
    lstrcpy(nid.szTip, TEXT("WinWheel - Virtual Desktop Switcher"));
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