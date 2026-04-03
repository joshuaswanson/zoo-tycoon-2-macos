/*
 * list_windows.exe - Enumerate all visible windows in Wine
 * Helps debug which dialogs/windows the game is showing
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

static BOOL CALLBACK EnumWinProc(HWND hwnd, LPARAM lParam) {
    char title[256], cls[128];
    RECT r;
    (void)lParam;

    if (!IsWindowVisible(hwnd)) return TRUE;

    GetWindowTextA(hwnd, title, sizeof(title));
    GetClassNameA(hwnd, cls, sizeof(cls));
    GetWindowRect(hwnd, &r);

    if (title[0] || strcmp(cls, "#32770") == 0) {
        DWORD style = GetWindowLongA(hwnd, GWL_STYLE);
        printf("HWND=%p cls='%s' title='%s' rect=(%d,%d,%d,%d) style=0x%08X\n",
            hwnd, cls, title, r.left, r.top, r.right, r.bottom, (unsigned)style);

        /* Also enumerate children */
        {
            HWND child = GetWindow(hwnd, GW_CHILD);
            while (child) {
                char ctitle[256], ccls[128];
                BOOL enabled = IsWindowEnabled(child);
                GetWindowTextA(child, ctitle, sizeof(ctitle));
                GetClassNameA(child, ccls, sizeof(ccls));
                if (ctitle[0]) {
                    printf("  CHILD=%p cls='%s' text='%s' enabled=%d\n",
                        child, ccls, ctitle, enabled);
                }
                child = GetWindow(child, GW_HWNDNEXT);
            }
        }
    }
    return TRUE;
}

int main(void) {
    int i;
    printf("=== Window enumeration (5 rounds, 3s apart) ===\n");
    fflush(stdout);
    for (i = 0; i < 5; i++) {
        printf("\n--- Round %d ---\n", i + 1);
        fflush(stdout);
        EnumWindows(EnumWinProc, 0);
        fflush(stdout);
        Sleep(3000);
    }
    return 0;
}
