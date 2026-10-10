#include "stdafx.h"
#include "i18n.h"

// Component declaration
namespace
{
    const char *getComponentAbout()
    {
        static const std::string english =
            "foo_monthly_stats\n"
            "Visualizes monthly play counts for foobar2000 1.6+\n"
            "License: MIT\n"
            "https://github.com/shirafukayayoi/foo_monthly_stats";
        static const std::string simplifiedChinese =
            "foo_monthly_stats\n"
            "统计并可视化每月及每年的音乐播放记录。\n"
            "许可证：MIT\n"
            "https://github.com/shirafukayayoi/foo_monthly_stats";
        return fms::isChinese() ? simplifiedChinese.c_str() : english.c_str();
    }
}

DECLARE_COMPONENT_VERSION_COPY(
    fms::tr("Monthly Stats", "月度统计"),
    "1.4.0",
    getComponentAbout());

VALIDATE_COMPONENT_FILENAME("foo_monthly_stats.dll");

FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;
