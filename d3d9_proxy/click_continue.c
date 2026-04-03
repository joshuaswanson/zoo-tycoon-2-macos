/*
 * click_continue.exe - Auto-click dialogs for Zoo Tycoon 2
 * Loops checking for Warning/Error dialogs and clicks Continue/OK.
 * Runs from inside Wine - doesn't steal macOS focus.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

/* Window titles to look for */
static const char *titles[] = {
    "Zoo Tycoon 2 Ultimate Collection End User License Agreement",
    "Zoo Tycoon 2 Warning",
    "Zoo Tycoon 2 Error",
    "Zoo Tycoon 2",
    NULL
};

/* Button texts to try clicking - prefer Safe Mode on Error dialogs */
static const char *buttons_normal[] = {
    "Accept", "&Accept", "Continue", "&Continue", "OK", "&OK", "Yes", "&Yes", NULL
};
static const char *buttons_error[] = {
    "Safe Mode", "&Safe Mode", NULL
};

typedef BOOL (WINAPI *ENUMCHILDPROC)(HWND, LPARAM);

static BOOL found_and_clicked = 0;
static char last_action[512] = {0};

static int g_useErrorButtons = 0; /* 1 = use Safe Mode buttons for Error dialog */

static BOOL CALLBACK EnumChildProc(HWND child, LPARAM lParam) {
    char text[256];
    const char **buttons = g_useErrorButtons ? buttons_error : buttons_normal;
    (void)lParam;
    if (GetWindowTextA(child, text, sizeof(text))) {
        int i;
        for (i = 0; buttons[i]; i++) {
            if (strcmp(text, buttons[i]) == 0 ||
                (g_useErrorButtons && strstr(text, "Safe")) ||
                (!g_useErrorButtons && (strstr(text, "Continue") || strstr(text, "Accept") || strstr(text, "OK")))) {
                /* Try to enable it first in case it's grayed out */
                if (!IsWindowEnabled(child)) {
                    EnableWindow(child, TRUE);
                    wsprintfA(last_action, "Enabled + clicked '%s'", text);
                } else {
                    wsprintfA(last_action, "Clicked '%s'", text);
                }
                SendMessageA(child, BM_CLICK, 0, 0);
                found_and_clicked = 1;
                return FALSE; /* stop enumeration */
            }
        }
    }
    return TRUE;
}

int main(void) {
    int loops = 0;
    int max_loops = 300; /* 5 minutes at 1 second intervals */
    int clicks = 0;

    printf("click_continue: waiting for Zoo Tycoon 2 dialogs...\n");
    fflush(stdout);

    while (loops < max_loops) {
        int ti;
        for (ti = 0; titles[ti]; ti++) {
            HWND hwnd = FindWindowA(NULL, titles[ti]);
            if (hwnd) {
                char cls[64];
                GetClassNameA(hwnd, cls, sizeof(cls));
                /* Only act on dialog boxes, not the main game window */
                if (strcmp(cls, "#32770") == 0) {
                    found_and_clicked = 0;
                    g_useErrorButtons = (strstr(titles[ti], "Error") != NULL);
                    EnumChildWindows(hwnd, EnumChildProc, 0);
                    if (found_and_clicked) {
                        clicks++;
                        printf("click_continue: [%d] %s on '%s' dialog\n", clicks, last_action, titles[ti]);
                        fflush(stdout);
                        Sleep(2000); /* wait a bit after clicking */
                    }
                }
            }
        }
        Sleep(1000);
        loops++;
    }

    printf("click_continue: timeout after %d loops, %d clicks\n", loops, clicks);
    return 0;
}
