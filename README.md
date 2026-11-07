# MultiLaunch

吉星派对（Astral Party）**多开启动器**：一个 exe，双击选账号，就能同时开多个客户端，各自独立登录。

- 默认只隔离 `BnSdk` 登录凭据和 `Player.log`，缓存/更新共用一份
- 可选 `isolate=full` 退回整棵 `LocalLow` 隔离（兼容旧行为）
- 大缓存（Addressables / NextPack）不再复制，也不用逐账号更新
- 自动绕过客户端单实例检查（不同 exe 名 + `_Data` junction）
- 自动补 `steam_appid.txt`，非 Steam 启动也能初始化 Steamworks
- 内置 **Steam 绕过**（可开关）：不依赖 Steam 客户端，多实例互不冲突
- 账号列表自动读取 BnSdk 登录日志里的 `user_name`

## 用法

双击：

```text
MultiLaunch.exe
```

```
已保存账号：
  1) Lyramora
  2) TenkinMori
  0) 新建账号
请选择:
```

- 输入编号 → 用该账号启动
- 输入 `0` → 输入名称 → 新建账号并启动
- 直接回车 → 退出

## 配置

`MultiLaunch.exe` 同目录的 `config.json`：

```json
{
  "steamBypass": true,
  "isolate": "bnsdk"
}
```

| 键 | 默认 | 说明 |
|---|---|---|
| `steamBypass` | `true` | 是否在实例里绕过 Steam。开启后不依赖 Steam 客户端、可同时多开；关闭则恢复原版 Steam 行为（同一 Steam 账号只能单开） |
| `isolate` | `bnsdk` | `bnsdk`=只隔离 BnSdk/Player.log，缓存共用；`full`=整棵 LocalLow 隔离（旧行为） |

## 目录结构

```text
D:\MultiProfiles\<账号名>\
├── name.txt                       # 显示名
├── instance.lock                  # 运行中的进程 PID
├── redirect.log                   # 重定向 / 绕过日志
├── Player.log                     # 该账号独立日志（-logFile）
└── AppData\LocalLow\feimo\AstralParty_CN\
    └── BnSdk\                     # 该账号登录凭据（独立）
```

缓存、Addressables、NextPack、游戏更新都留在真实 `LocalLow` 下，所有实例共用。

## 原理

1. **单实例检查**：客户端按进程名判断（`AstralParty_CN`），所以给每个账号生成
   `AstralParty_CN_XXXXXXXX.exe` 硬链接，并配同名 `_Data` junction。
2. **轻量隔离**：启动时注入 `UserDataRedirect.dll`。
   - 默认只 hook `CreateFileW/A`、`GetFileAttributes*`、`FindFirstFile*`、
     `CreateDirectoryW`、`DeleteFileW`、`MoveFileExW`、`CopyFileW`，
     把 `%LocalLow%\feimo\AstralParty_CN\BnSdk` 替换成 profile 下的 `BnSdk`。
   - `Player.log` 通过 Unity 的 `-logFile <profile>\Player.log` 分开。
   - 缓存/更新不经过 profile，保持一份。
   - `isolate=full` 时才 hook `SHGetKnownFolderPath`，恢复整棵 LocalLow 隔离。
3. **Steam 绕过**：DLL 内静态编译了 steamhack（CesiumLoader 的独立实现），
   通过 il2cpp 找到 AOT 方法并 inline hook：
   - `SteamManager.Awake()` → no-op（不因非 Steam 启动自杀）
   - `SteamAPI_RestartAppIfNecessary` → 0（不被 Steam 重启）
   - `SteamMatchmaking.CreateLobbyAsync / JoinLobbyAsync` → 返回空的已完成 Task
   - `LobbyQuery.RequestAsync` → 返回长度 0 的 lobby 数组
   共 6 个 hook，日志在 `redirect.log` 里以 `[steamhack]` 开头。

## 构建

需要 Visual Studio Build Tools（MSVC）与 .NET Framework 4：

```bat
build_dll.bat
```

产物：

```
build\UserDataRedirect.dll
```

启动器：

```bat
csc /target:exe /out:MultiLaunch.exe src\Launcher\Program.cs
```

## 目录

```
MultiLaunch/
├── src/
│   ├── Launcher/Program.cs            # 账号菜单 / 进程创建 / DLL 注入
│   └── Redirect/
│       ├── UserDataRedirect.cpp       # profile / Steam 绕过 / full 模式
│       ├── bnsdk_hook.cpp             # 只重定向 BnSdk 的文件 API hook
│       └── cesium/steam/              # steamhack（来自 CesiumLoader，MIT/BSD）
├── third_party/minhook/               # BSD-2-Clause
├── third_party/fmt/                   # MIT
├── config.json
├── build_dll.bat
├── MultiLaunch.exe
└── UserDataRedirect.dll
```

## 注意事项

- 账号登录由游戏自身的 SDK 完成；本工具只切换“哪份本地数据”
- 关闭 `steamBypass` 时，登录身份来自 Steam 账号，且 Steam 只允许一个游戏进程
- 游戏更新后若出现 `Data folder not found`，删掉游戏目录下
  `AstralParty_CN_*.exe` 与 `AstralParty_CN_*_Data`（junction）后重新启动即可
- 仅用于个人多账号管理，请遵守游戏用户协议

## 来源与许可

- `src/Redirect/cesium/steam/`（steamhack）来自 **CesiumLoader**
  - 仓库：https://github.com/higashitaniyume/CesiumLoader
  - 路径：`loader/CesiumLoader/steam/`
  - 来源提交：`7a704568b1375027c51ee91eb90b20fa589b3e47`（2026-11-03）
  - 许可证：**GNU AGPL-3.0**（全文见 `src/Redirect/cesium/LICENSE-AGPL-3.0.txt`）
  - 本项目只做了“脱离 loader 独立编译”的最小改动，详见 `src/Redirect/cesium/NOTICE.md`
- `third_party/minhook`：BSD-2-Clause
- `third_party/fmt`：MIT

## License

仓库其余部分：MIT。
**含 `src/Redirect/cesium/` 代码的 `UserDataRedirect.dll` 按 AGPL-3.0 分发**（源码已公开）。
