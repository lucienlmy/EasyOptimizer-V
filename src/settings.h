/* ============================================================================
 *  settings.h — persisted UI preferences
 * ----------------------------------------------------------------------------
 *  Stored under HKCU\Software\EasyOptimizer. The registry (rather than an .ini
 *  next to the executable) because the app is routinely run from Program Files
 *  or a read-only share, where writing beside the .exe silently fails — and a
 *  preference that silently fails to save is worse than none.
 *
 *  Header-only so adding a preference never means touching build.bat's explicit
 *  source list in both the working tree and the mirror.
 * ==========================================================================*/
#ifndef EO_SETTINGS_H
#define EO_SETTINGS_H

#include <windows.h>
#include <stdbool.h>

/* Pulled in here rather than in build.bat's link line so that adding this header
 * stays a one-file change across the working tree and the mirror. */
#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#endif

#define EO_SETTINGS_KEY L"Software\\EasyOptimizer"

/* Reads a DWORD preference. Returns false when unset (caller keeps its default). */
static inline bool eo_setting_get_dword(const wchar_t *name, DWORD *out) {
    if (!name || !out) return false;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, EO_SETTINGS_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    DWORD type = 0, value = 0, size = sizeof(value);
    LONG r = RegQueryValueExW(key, name, NULL, &type, (LPBYTE)&value, &size);
    RegCloseKey(key);
    if (r != ERROR_SUCCESS || type != REG_DWORD) return false;
    *out = value;
    return true;
}

/* Writes a DWORD preference. Best effort: a failure here must never block the
 * UI action that triggered it. */
static inline void eo_setting_set_dword(const wchar_t *name, DWORD value) {
    if (!name) return;
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, EO_SETTINGS_KEY, 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
        return;
    RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
    RegCloseKey(key);
}

#endif /* EO_SETTINGS_H */
