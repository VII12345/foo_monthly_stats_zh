# foo_monthly_stats 汉化版

[![构建状态](https://github.com/VII12345/foo_monthly_stats_zh/actions/workflows/build.yml/badge.svg)](https://github.com/VII12345/foo_monthly_stats_zh/actions)

这是 [shirafukayayoi/foo_monthly_stats](https://github.com/shirafukayayoi/foo_monthly_stats) 的简体中文本地化版本，并包含逐曲实际收听时长统计修正。组件用于查看 foobar2000 的日、月、年播放统计，并导出 HTML 报告。

![统计面板示例](./image/sample.png)

## 功能

- 按日、月、年查看歌曲播放次数和收听时长
- 每首歌累计显示实际收听时长，暂停和 seek 跳过的区间不计
- 按标题和艺术家合并歌曲；文件移动后仍可沿用同一统计。缺少标题或艺术家时按文件路径识别
- 播放次数遵循 foobar2000 Playback Statistics 规则：播放满 60 秒，或曲目结束且已播放至少三分之一，计为一次
- 导出桌面版或手机长图 HTML 报告，包含专辑封面、艺术家和歌曲排行
- SQLite 本地数据库；可从统计面板移除所选期间的歌曲统计
- 简体中文 Windows 界面下显示中文；其他系统界面语言使用英文

## 安装

从本仓库的 Releases 页面下载最新的 fb2k-component 文件，然后在 foobar2000 中选择 文件 → 首选项 → 组件 → 安装，安装后重启。

也可以在 Actions 页面下载最近一次成功构建的 foo_monthly_stats_zh-component artifact，解压后安装其中的 fb2k-component 文件。

升级提示：新版本会保留原数据库中的播放次数，但旧版本记录的时长不可靠，因此升级后会从零开始累计准确时长。请先备份 foobar2000 配置目录中的 foo_monthly_stats.db。

## 使用

在 foobar2000 主菜单选择 视图 → 月度统计 打开面板。面板中可以切换日、月、年视图、按列排序、导出报告或移除当前期间的选中统计。

每次播放至少累计 1 秒后，时长会在切歌、停止或曲目结束时保存。播放次数与收听时长分别记录，因此短听后切歌的歌曲可以显示时长而播放次数为 0。

## 构建

要求 Windows 10/11、Visual Studio Build Tools（含 C++ 桌面开发、ATL/MFC）、Python 3。仓库包含 foobar2000 SDK 和所需依赖。

在仓库根目录运行以下命令：

    .\build.ps1 -Config Release -Platform x64

脚本会构建组件、运行数据库和计时回归测试并生成安装包。输出位于 foo_monthly_stats\_result\foo_monthly_stats.fb2k-component。

## 数据与兼容性

- 默认数据库位于 foobar2000 用户配置目录，文件名为 foo_monthly_stats.db；也可在组件设置中指定其他路径。
- 本汉化版保留上游 DLL 名称、组件标识、设置 GUID 和数据库位置，以兼容原版设置及数据。
- 因此汉化版不能与上游原版同时安装；它应作为原版的替换或升级版本安装。
- 本地化版遵循 foobar2000 的播放次数回调规则；实际收听时长按经过时间累计，暂停不计，seek 跳过的区间不计。

## 上游与许可

本仓库是上游项目的本地化分支，保留上游作者及其代码历史。功能开发、上游更新和原始发行记录请参阅上游仓库：https://github.com/shirafukayayoi/foo_monthly_stats

插件代码遵循 MIT License；foobar2000 SDK、pfc、libPPUI 及第三方依赖各自适用的许可和署名要求。请保留仓库中的 LICENSE、sdk-license.txt、依赖目录许可文件和 THIRD_PARTY_NOTICES.md。
