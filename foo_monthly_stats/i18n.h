#pragma once
// i18n.h – lightweight internationalization helper
// Dialog resources are localized in the .rc file; generated UI and reports use tr().

namespace fms
{

    // Returns true when the UI language is Japanese
    inline bool isJapanese()
    {
        LANGID lang = GetUserDefaultUILanguage();
        return PRIMARYLANGID(lang) == LANG_JAPANESE;
    }

    // Simplified-Chinese UI selection follows the Windows user interface language.
    inline bool isChinese()
    {
        LANGID lang = GetUserDefaultUILanguage();
        return PRIMARYLANGID(lang) == LANG_CHINESE && SUBLANGID(lang) == SUBLANG_CHINESE_SIMPLIFIED;
    }

    inline const char *tr(const char *english, const char *simplifiedChinese)
    {
        return isChinese() ? simplifiedChinese : english;
    }

    inline void setDialogTextUtf8(HWND hwnd, int controlId, const char *text)
    {
        const std::wstring wide = pfc::stringcvt::string_wide_from_utf8(text);
        ::SetDlgItemTextW(hwnd, controlId, wide.c_str());
    }

    // String table IDs (must match values in .rc string tables)
    enum StringId
    {
        STR_PLUGIN_NAME = 100,
        STR_MONTHLY_STATS,
        STR_RESET,
        STR_EXPORT,
        STR_PREFERENCES,
        STR_PREV_MONTH,
        STR_NEXT_MONTH,
        STR_COL_RANK,
        STR_COL_TITLE,
        STR_COL_ARTIST,
        STR_COL_ALBUM,
        STR_COL_PLAYS,
        STR_COL_DELTA,
        STR_EXPORT_SUCCESS,
        STR_EXPORT_FAIL,
        STR_CHROME_NOT_SET,
        STR_DB_PATH_LABEL,
        STR_ART_SIZE_LABEL,
        STR_AUTO_REPORT_LABEL,
        STR_CHROME_PATH_LABEL,
        STR_BROWSE,
        STR_PREF_PAGE_NAME,
        STR_REMOVE_SELECTED,
    };

    // Load a string from the resource of the running DLL.
    // Uses Windows resource-language fallback when a localized string is unavailable.
    inline pfc::string8 LoadStr(StringId id)
    {
        HMODULE hMod = core_api::get_my_instance();
        wchar_t buf[512] = {};
        if (::LoadStringW(hMod, static_cast<UINT>(id), buf, 512) > 0)
            return pfc::stringcvt::string_utf8_from_wide(buf);
        return "";
    }

} // namespace fms
