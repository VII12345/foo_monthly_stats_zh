# 仓库开发说明

本仓库是 foo_monthly_stats 的简体中文本地化分支。修改时遵循以下约定：

- 组件使用 C++17 和 x64 Release；依赖优先使用仓库内提供的 foobar2000 SDK、WTL、SQLite 和 pugixml。
- 面向用户的文本需要检查简体中文及英文回退。资源脚本 foo_monthly_stats/foo_monthly_stats.rc 使用带 BOM 的 UTF-16LE。
- C++ 源文件使用 UTF-8；Win32 UI 文本按现有 helper 转换为 UTF-16。
- 数据库写入通过 DbManager 工作线程执行；查询、删除和重建期间先等待待处理写入完成。
- 数据库结构变更必须包含兼容迁移和回归测试；播放次数事件与实际收听时长事件保持独立。
- 除非用户明确要求放弃原版数据兼容，不更改上游组件 DLL 名、配置 GUID 或默认数据库文件名。
- 修改完成后运行根目录的 build.ps1，确认 Release x64 构建、测试和打包通过。
- 对外说明应注明上游 shirafukayayoi/foo_monthly_stats，并保留 MIT、foobar2000 SDK 和第三方库许可及署名。
