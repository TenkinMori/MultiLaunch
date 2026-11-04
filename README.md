# MultiLaunch

吉星派对（Astral Party）**多开启动器**：一个 exe，双击选账号，就能同时开多个客户端，各自独立登录。

- 每个账号独立 `LocalLow`（BnSdk 登录凭据、Player.log 各自一份）
- 大缓存（Addressables / NextPack）用 junction 共享，不重复下载
- 自动绕过客户端单实例检查（不同 exe 名 + `_Data` junction）
- 自动补 `steam_appid.txt`，非 Steam 启动也能初始化 Steamworks
- 账号列表自动读取 BnSdk 登录日志里的 `user_name`

## 用法

```text
MultiLaunch.exe
```

双击后选择：

```
已保存账号：
  1) Lyramora
  2) TenkinMori
  0) 新建账号
请选择:
```

- 输入编号 → 用该账号启动
- 输入 `0` → 输入名称 → 新建账号并启动
- 回车 / 乱输 → 退出

## 目录结构

```text
D:\MultiProfiles\<账号名>\
├── name.txt                       # 显示名
├── instance.lock                  # 运行中的进程 PID
└── AppData\LocalLow\feimo\AstralParty_CN\
    ├── BnSdk\                     # 该账号登录凭据（独立）
    ├── Player.log                 # 该账号日志（独立）
    ├── com.unity.addressables     # junction → 官方缓存（共享）
    ├── NextPack                   # junction
├── NextPack.full-33               # junction
└── Temp                           # junction
```

## 原理

1. **单实例检查**：客户端按进程名判断（`AstralParty_CN`），所以给每个账号生成
   `AstralParty_CN_XXXXXXXX.exe` 硬链接，并配同名 `_Data` junction。
2. **数据隔离**：启动时注入 `UserDataRedirect.dll`，hook
   `SHGetKnownFolderPath(FOLDERID_LocalAppDataLow)`，把 `LocalLow` 指到账号目录。
3. **Steam 初始化**：非 Steam 启动会因 `SteamManager` 判定“非 Steam 客户端”退出，
   在游戏目录放 `steam_appid.txt` 即可继续。

## 构建

需要 Visual Studio Build Tools（MSVC）：

```bat
build_dll.bat
```

产物：

```
build\UserDataRedirect.dll
```

启动器（C#，.NET Framework 4）：

```bat
csc /target:exe /out:MultiLaunch.exe src\Launcher\Program.cs
```

## 目录

```
MultiLaunch/
├── src/
│   ├── Launcher/Program.cs        # 启动器：账号菜单 / 进程创建 / DLL 注入
│   └── Redirect/UserDataRedirect.cpp  # 原生 DLL：LocalLow 重定向
├── third_party/minhook/           # MinHook（BSD-2-Clause）
├── build_dll.bat
├── MultiLaunch.exe                # 已构建
└── UserDataRedirect.dll           # 已构建
```

## 注意事项

- 账号登录仍由游戏自身的 SDK 完成；本工具只切换“哪份本地数据”
- 同一 Steam 客户端对应同一个 Steam 身份，如需不同 Steam 账号需另开 Steam
- 游戏更新后若出现 `Data folder not found`，删掉游戏目录下
  `AstralParty_CN_*.exe` 与 `AstralParty_CN_*_Data`（junction）后重新启动即可
- 仅用于个人多账号管理，请遵守游戏用户协议

## License

MIT（`third_party/minhook` 为 BSD-2-Clause）
