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
            "Upstream: https://github.com/shirafukayayoi/foo_monthly_stats\n"
            "Chinese localization: https://github.com/VII12345/foo_monthly_stats_zh";
        static const std::string simplifiedChinese =
            "foo_monthly_stats\n"
            "简体中文本地化版本；统计并可视化日、月、年的播放次数与实际收听时长。\n"
            "许可证：MIT\n"
            "上游：https://github.com/shirafukayayoi/foo_monthly_stats\n"
            "汉化版：https://github.com/VII12345/foo_monthly_stats_zh";
        return fms::isChinese() ? simplifiedChinese.c_str() : english.c_str();
    }
}

DECLARE_COMPONENT_VERSION_COPY(
    fms::tr("Monthly Stats", "月度统计"),
    "1.7.0",
    getComponentAbout());

VALIDATE_COMPONENT_FILENAME("foo_monthly_stats.dll");

FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;
