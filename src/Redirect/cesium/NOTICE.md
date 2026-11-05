# 来源与许可（src/Redirect/cesium）

本目录下的 steamhack 相关源码（`steam/`、`runtime/il2cpp_safe.h`、
`platform/loader.h` 的替身）来自：

- 项目：CesiumLoader
- 仓库：https://github.com/higashitaniyume/CesiumLoader
- 路径：`loader/CesiumLoader/steam/`、`loader/CesiumLoader/runtime/il2cpp_safe.h`
- 来源提交：`7a704568b1375027c51ee91eb90b20fa589b3e47`（2026-11-03）
- 许可证：**GNU AGPL-3.0**（全文见同目录 `LICENSE-AGPL-3.0.txt`）

## 本项目的改动

仅为“脱离 loader 独立编译”所做的最小改动：

1. `platform/loader.h` 替换为精简替身（只保留 `log_line` 系列声明），
   实现放在 `src/Redirect/UserDataRedirect.cpp`（写到本工具自己的日志文件）。
2. 新增 il2cpp 函数表填充（`FillIl2CppTable`），从 `GameAssembly.dll` 导出解析，
   然后调用 `steamhack_install(...)`。
3. 未修改 steamhack 的 hook 逻辑与算法。

## 许可影响

包含本目录代码的构建产物（`UserDataRedirect.dll`）整体按 **AGPL-3.0** 分发；
仓库其余部分仍为 MIT。源码已公开在本仓库，符合 AGPL 的源码提供要求。
