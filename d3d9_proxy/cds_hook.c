/*
 * cds_hook.c - Hook ChangeDisplaySettingsA/W to always succeed
 * Compiled as a separate DLL, loaded by our d3d9 proxy
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* We hook by patching the game's IAT (Import Address Table) */

typedef LONG (WINAPI *PFN_CDS_A)(DEVMODEA*, DWORD);
typedef LONG (WINAPI *PFN_CDS_W)(DEVMODEW*, DWORD);
typedef LONG (WINAPI *PFN_CDSE_A)(LPCSTR, DEVMODEA*, HWND, DWORD, LPVOID);
typedef LONG (WINAPI *PFN_CDSE_W)(LPCWSTR, DEVMODEW*, HWND, DWORD, LPVOID);

static PFN_CDS_A  real_CDS_A = NULL;
static PFN_CDS_W  real_CDS_W = NULL;
static PFN_CDSE_A real_CDSE_A = NULL;
static PFN_CDSE_W real_CDSE_W = NULL;

static LONG WINAPI hook_ChangeDisplaySettingsA(DEVMODEA *dm, DWORD flags) {
    /* Try the real call first */
    LONG result = real_CDS_A ? real_CDS_A(dm, flags) : DISP_CHANGE_SUCCESSFUL;
    /* If it fails, pretend it succeeded */
    if (result != DISP_CHANGE_SUCCESSFUL) {
        result = DISP_CHANGE_SUCCESSFUL;
    }
    return result;
}

static LONG WINAPI hook_ChangeDisplaySettingsW(DEVMODEW *dm, DWORD flags) {
    LONG result = real_CDS_W ? real_CDS_W(dm, flags) : DISP_CHANGE_SUCCESSFUL;
    if (result != DISP_CHANGE_SUCCESSFUL) {
        result = DISP_CHANGE_SUCCESSFUL;
    }
    return result;
}

static LONG WINAPI hook_ChangeDisplaySettingsExA(LPCSTR dev, DEVMODEA *dm, HWND hw, DWORD flags, LPVOID lp) {
    LONG result = real_CDSE_A ? real_CDSE_A(dev, dm, hw, flags, lp) : DISP_CHANGE_SUCCESSFUL;
    if (result != DISP_CHANGE_SUCCESSFUL) {
        result = DISP_CHANGE_SUCCESSFUL;
    }
    return result;
}

static LONG WINAPI hook_ChangeDisplaySettingsExW(LPCWSTR dev, DEVMODEW *dm, HWND hw, DWORD flags, LPVOID lp) {
    LONG result = real_CDSE_W ? real_CDSE_W(dev, dm, hw, flags, lp) : DISP_CHANGE_SUCCESSFUL;
    if (result != DISP_CHANGE_SUCCESSFUL) {
        result = DISP_CHANGE_SUCCESSFUL;
    }
    return result;
}

static void PatchIAT(HMODULE target, const char *dllName, const char *funcName, void *hookFunc, void **origFunc) {
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER*)target;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS*)((BYTE*)target + dos->e_lfanew);
    DWORD impDir = nt->OptionalHeader.DataDirectory[1].VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *imp;

    if (!impDir) return;
    imp = (IMAGE_IMPORT_DESCRIPTOR*)((BYTE*)target + impDir);

    while (imp->Name) {
        char *name = (char*)((BYTE*)target + imp->Name);
        if (_stricmp(name, dllName) == 0) {
            IMAGE_THUNK_DATA *origThunk = (IMAGE_THUNK_DATA*)((BYTE*)target + imp->OriginalFirstThunk);
            IMAGE_THUNK_DATA *iatThunk = (IMAGE_THUNK_DATA*)((BYTE*)target + imp->FirstThunk);

            while (origThunk->u1.AddressOfData) {
                if (!(origThunk->u1.Ordinal & 0x80000000)) {
                    IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME*)
                        ((BYTE*)target + origThunk->u1.AddressOfData);
                    if (strcmp(ibn->Name, funcName) == 0) {
                        DWORD oldProt;
                        *origFunc = (void*)iatThunk->u1.Function;
                        VirtualProtect(&iatThunk->u1.Function, 4, PAGE_READWRITE, &oldProt);
                        iatThunk->u1.Function = (DWORD_PTR)hookFunc;
                        VirtualProtect(&iatThunk->u1.Function, 4, oldProt, &oldProt);
                        return;
                    }
                }
                origThunk++;
                iatThunk++;
            }
        }
        imp++;
    }
}

void InstallCDSHook(void) {
    HMODULE game = GetModuleHandleA(NULL);
    if (!game) return;

    PatchIAT(game, "user32.dll", "ChangeDisplaySettingsA",
        hook_ChangeDisplaySettingsA, (void**)&real_CDS_A);
    PatchIAT(game, "user32.dll", "ChangeDisplaySettingsW",
        hook_ChangeDisplaySettingsW, (void**)&real_CDS_W);
    PatchIAT(game, "user32.dll", "ChangeDisplaySettingsExA",
        hook_ChangeDisplaySettingsExA, (void**)&real_CDSE_A);
    PatchIAT(game, "user32.dll", "ChangeDisplaySettingsExW",
        hook_ChangeDisplaySettingsExW, (void**)&real_CDSE_W);
}
