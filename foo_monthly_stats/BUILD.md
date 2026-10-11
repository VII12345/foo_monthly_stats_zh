# 构建指南

## 环境要求

- Windows 10/11，x64
- Visual Studio 2022 或更新版本的 Build Tools
- 安装 C++ 桌面开发，并包含当前 MSVC 工具集对应的 ATL/MFC 组件
- Python 3，用于组件打包

foobar2000 SDK、WTL、SQLite、pugixml 和 Catch2 均随仓库提供。

## 一键构建、测试和打包

在仓库根目录运行：

    .\build.ps1 -Config Release -Platform x64

脚本依次构建 DLL、构建并运行测试，然后打包安装文件。成功后：

- DLL：foo_monthly_stats\_result\x64_Release\bin\foo_monthly_stats.dll
- 安装包：foo_monthly_stats\_result\foo_monthly_stats.fb2k-component
- 测试程序：foo_monthly_stats\tests\_result\x64_Release\tests\foo_monthly_stats_tests.exe

## 编码注意事项

- C++ 源文件使用 UTF-8；项目通过 /utf-8 编译。
- foo_monthly_stats\foo_monthly_stats.rc 必须保留 UTF-16LE BOM，以便 Windows Resource Compiler 正确读取中文资源。
- ATL include 路径由 Directory.Build.targets 根据 MSBuild 当前选择的 MSVC 工具集自动解析；不要写入开发机专属的绝对路径。
