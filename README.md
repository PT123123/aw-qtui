# aw-qtui

用 **原生 C++ Qt 6 (Widgets)** 实现的 ActivityWatch 桌面客户端，包含七个页面：
**Activity 统计面板**、**Timeline 可交互时间线**、**收件箱（Inbox）**、**任务（Todo）**、
**局域网同步（LAN Sync）**、**标签 Day（ManicTime 式时间标签）** 与 **多日统计**。

实现参照 `PT123123/aw-android` fork（含 `aw-inbox-rust` 服务端与 `aw-webui`
前端）。本工程是**客户端 UI**，后端复用 aw-server-rust 里挂在 **5600 端口**的
inbox 服务，与 aw-webui 承担同样的角色；不包含 Rust 服务端本体。

标签功能参照 ManicTime Windows Client 特性移植，需求基线见
[需求文档-ManicTime特性移植.md](需求文档-ManicTime特性移植.md)。

## 为什么是 C++ Qt

- 原生编译，启动与渲染性能远高于 Python 绑定（PySide/PyQt）
- 零运行时依赖（除 Qt 动态库），单 exe 可分发
- `QNetworkAccessManager` 异步 HTTP，`QThread` + Win32 DNS-SD 做 mDNS 自动发现

## 页面

| 页面 | 说明 |
| --- | --- |
| 📊  Activity | ActivityWatch 风格统计面板：日期导航 + 24h 活跃柱状图 + Summary/Window/Browser/Editor 标签页 + Top Applications / Top Window Titles / Top Categories 横向条形图 + Timeline (Barchart) 分类彩色时间柱 + Category Tree + Category Sunburst 环形图 |
| ⏱  Timeline | 可交互多行时间线（afk-status / aw-watcher-window / aw-watcher-web），拖拽平移、滚轮缩放、hover 详情 tooltip；顶部 Interval mode / Show last 工具栏，底部 Tockler 风格统计卡片（Total tracked / AFK / First activity / Last activity） |
| 📥 收件箱 | MoeMemos 风格卡片流（头部相对时间 + 置顶旗标 + ⋯ 菜单）、完整 Markdown 渲染（标题/列表/引用/代码块/粗斜体/删除线/链接）、任务清单 ☐ 点击勾选、内联 #标签 高亮、本地置顶优先排序、无限滚动、标签侧栏多选过滤、搜索、排序、评论（离线优先：本地缓存 + 待同步队列，重连自动补推）、复制全部、连接状态徽标、右下角悬浮新建、工具栏 ⚙ 设置（全局快捷键） |
| ☑ 任务 | TickTick / Super Productivity 式 Todo：左侧「收集箱/今天/最近 7 天/全部 + 彩色清单」导航，中间任务列表（快速添加、优先级/期限/标签元信息、已完成折叠区），右侧详情面板（标题/已完成/清单/优先级/截止日期/重复/标签/备注/子任务）。数据源走 `TodoSource` 抽象，当前为本地 mock（`todo_local.json` 持久化 + 种子数据），后续接入 Rust 时新增 `TodoApiStore` 实现同一接口即可，页面零改动 |
| ⇄ 局域网同步 | 设备注册表（device_id/名称/平台/最后在线/最后同步/待同步/版本）、手动同步（POST /inbox/sync，展示拉取数与冲突）、设备心跳注册、mDNS 自动发现（`_activitywatch._tcp.local.`，Win32 原生 DNS-SD） |
| 🏷 标签 Day | ManicTime 式时间标签：时间线选择模式（左键拖拽吸附活动边界、Ctrl 多选、双击选整块）→ Add tag（标签/备注/Billable/起止时间/最近标签/Tag picker）、Tag editor（组合/单标签/快捷键/标签源，重命名/替换/删除/改色/导入导出/右键 Skip 与默认可计费）、自动标签规则引擎（Regular/Append/Prepend/Absorb + 间隙填充 + 高亮猜测 + AutoTags lane 实时重算 + 复制到手工标签）、未标记热力图月历、Tag away 一键给未标记时间段打标签、计时工具（秒表/计时器/番茄钟）、高级搜索（日期范围/时间线选择/未标记过滤/批量打标/删除/导出/双击跳转）、当日过滤框（group:/duration>/start>/end>/label=billable/note:/-取反/or/通配符/#regex）。本地数据 `timetags_local.json`（独立于收件箱） |
| 📈 统计 | 多日统计：多 Tab + 类型（Top / Day duration / Attendance / Custom）、日期范围（本周/本月）、折线/柱状切换、平均值线、多序列应用对比、数据表联动、导出 CSV |

快捷键：窗口内 `1`/`2`/`3`/`4`/`5`/`6`/`7` 切页（Activity / Timeline / 收件箱 / 任务 / 同步 / 标签 Day / 统计），`F5` 刷新当前页，`Ctrl+F` 聚焦搜索，`Ctrl+Enter` 提交笔记。
全局快捷键（收件箱 ⚙ 设置里可改，系统级注册，应用失焦/最小化也生效）：默认 `Alt+N` 添加记录（唤醒窗口 + 跳收件箱 + 弹新建），默认 `Alt+M` 唤醒并跳转收件箱。配置存 `%APPDATA%\aw-qtui\aw-qtui\awqtui.ini`。

## 环境要求

- Windows 10/11（mDNS 用 Win10+ 原生 DNS-SD API）
- MSVC 2022（VS Community 即可，含 C++ 桌面开发工作负载）
- Qt 6.8.x（msvc2022_64）—— 用 aqt 命令行安装，无需 Qt 安装器 GUI：

```powershell
python -m pip install aqtinstall
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt
```

## 构建 & 运行

### 构建客户端（默认）

```bash
make            # Release 客户端 + 服务端（等价于 make release）
make build      # 仅 Release 客户端（不带服务端）
make debug      # Debug 客户端 + 服务端
```

Makefile 自动注入 VC / Windows SDK 环境（**无需先开 Developer Command Prompt**），内部用 `cmake -G Ninja`
生成构建图、`cmake --build` 驱动 ninja→cl 编译，再用 `windeployqt` 部署 Qt 运行库。
产物：`build\awqtui.exe`（Qt DLL 已部署到同目录）；Debug 产物在 `build-dbg\`。
Debug/Release 及各入口用法见 [Debug / Release 构建](#debug--release-构建)。

### 单独构建服务端（aw-inbox-rust.exe）

```bash
make server     # 构建并部署到 build/server/aw-inbox-rust.exe
```

Makefile 自动定位服务端源码（`SERVER_SRC=` → `$$AW_SERVER_SRC` → `vendor\aw-inbox` submodule → `vendor\aw-server-rust\aw-inbox-rust`），
把 `aw-inbox-rust` crate 同步到 `server-src\` 并用 **Windows 原生 MSVC 工具链**（cargo + VC 环境）构建。
产物：`build\server\aw-inbox-rust.exe`。源码来源与缺失行为见
[服务端依赖（aw-inbox-rust）](#服务端依赖aw-inbox-rust)。

### 联合构建（客户端 + 服务端）

```bash
make                 # = make release：Release 客户端 + 服务端
make SERVER=         # 仅客户端（跳过服务端）
```

产物：`build\awqtui.exe` + `build\server\aw-inbox-rust.exe`。
服务端构建配置**跟随客户端**（`make debug` 两端都是 Debug），见
[Debug / Release 构建](#debug--release-构建)。

### Release 打包

```bash
make dist                       # 版本 +0.01 自动递增并写回 CMakeLists.txt
make dist VERSION=0.2.0         # 指定版本号（不自动加、不写回）
make dist SKIP_SERVER=1         # 发布纯客户端包（不含服务端）
```

产物：`dist\aw-qtui-<版本>-win64\`（awqtui.exe + Qt DLL/plugins + aw-inbox-rust.exe + README）
及同名 `.zip`。

**版本号默认自动 +0.01**：不传 `VERSION=` 时，以 `CMakeLists.txt` 当前版本为基准 patch +1（0.1.1 → 0.1.2），
打包成功后写回 `CMakeLists.txt`，下次 release 继续递增（版本不重复）；显式 `VERSION=` 则按给定版本，不自动加、不写回。

**服务端默认必带**：`make dist` 默认执行联合构建并校验 `build\server\aw-inbox-rust.exe`，
服务端产物缺失会直接报错，绝不静默产出不带服务端的包。只有显式 `SKIP_SERVER=1` 才放行纯客户端包。

### Debug / Release 构建

所有构建入口**默认都是 Release**。需要 Debug 时统一用 `make debug`（或 `make build-dbg` 仅客户端），
Debug/Release 分目录（`build` / `build-dbg`）互不干扰：

| 场景 | Release（默认） | Debug |
| --- | --- | --- |
| 仅客户端 | `make build` | `make build-dbg` |
| 客户端 + 服务端 | `make`（或 `make release`） | `make debug` |
| Release 打包 | `make dist`（固定 Release） | 不提供 |

要点：

- **联合构建的配置是一致的**：`make debug` 会把 `CMAKE_BUILD_TYPE=Debug` 传给 CMake，服务端
  cargo 构建也不加 `--release`，不会出现「客户端 Debug / 服务端 Release」的错配；
- **产物同名同路径**：客户端都是 `build\awqtui.exe`，服务端都是 `build\server\aw-inbox-rust.exe`
  （Debug 在 `build-dbg\`）。区分看体积与行为——Debug 无优化、体积明显更大（服务端含调试符号），
  Release 经优化（服务端 `--release`，实测约 6.2MB）；切换 Debug/Release 会触发对应工具链全量重编；
- **Qt 运行库区分 Debug/Release**：`make debug` 用 `windeployqt --debug` 部署 `Qt6*d.dll`，
  Release 用 `--release` 部署 `Qt6*.dll`，CRT（`/MDd` vs `/MD`）一致，避免旧版「Debug 链 Release DLL」的坑；
- **Release 打包固定走 Release**：`make dist` 内部走 Release 联合构建（不带 Debug），
  不提供 Debug 打包；要自打包 Debug 可手动把 `build\` 拷出，但不推荐用于分发。

### 运行

客户端默认**自动管理本地服务端**：启动时通过相对路径定位 `server\aw-inbox-rust.exe` → 端口探测（未监听才拉起）→
后台看护（异常退出自动重新拉起）→ 登录自启（HKCU Run）。直接启动客户端即可（也可用 `make run` 从构建目录启动，可带 `PORT=5620` 指定联调端口）：

```powershell
.\build\awqtui.exe                        # 默认 http://127.0.0.1:5600
```

如需手动管理（调试/自定义地址），可先自行启动服务端：

```powershell
.\build\server\aw-inbox-rust.exe          # 默认 --host 127.0.0.1 --port 5600
.\build\awqtui.exe --url http://127.0.0.1:5600
```

连其它地址：

```powershell
.\build\awqtui.exe --url http://192.168.1.10:5600
```

无 Rust 服务端时，用内置 mock 联调（纯标准库，实现同一套 REST 契约）：

```powershell
python tools\mock_inbox_server.py 5620
.\build\awqtui.exe --url http://127.0.0.1:5620
```

## 服务端依赖（aw-inbox-rust）

### 依赖本质

aw-qtui 对 aw-inbox-rust 是**运行期进程 + REST 契约依赖**，不是编译期链接依赖：

- 客户端经 `QNetworkAccessManager` 访问 `http://127.0.0.1:5600`，服务端是**独立进程**；
- 两者只约定 REST 接口（`/inbox/...`）与 mDNS 服务类型 `_activitywatch._tcp.local.`；
- 构建**客户端**不需要任何 Rust 工具链；只有构建/打包服务端才需要。

### 源码来源（git submodule）

服务端源码以 **git submodule** 形式挂载在 `vendor/` 下，共两个远端（都是底层 Rust 依赖）：

| submodule | 远端 | 分支 / 提交 | 角色 |
| --- | --- | --- | --- |
| `vendor/aw-inbox` | `PT123123/aw-inbox` | master（`38bcf3b`） | **构建源**：独立 crate（`aw-inbox-rust`），Cargo.toml 在仓库根，可单 crate 构建 |
| `vendor/aw-server-rust` | `PT123123/aw-server-rust` | `feature/inbox`（`2f11667`） | 参考工作区：官方集成环境；其 `aw-inbox-rust` 成员即指向 `PT123123/aw-inbox` 的嵌套 submodule |

> `aw-server-rust` 工作区把 `aw-inbox-rust` 作为 **submodule 指向 `PT123123/aw-inbox`（同一提交）**，
> 所以两处代码是同一份，不存在分叉；`aw-server-rust` 还含 `aw-webui`（约 675MB）等嵌套 submodule，
> **构建不需要它们**，无需 `--recursive`。

**初始化 / 拉取 submodule：**

```powershell
# 在本仓库内补齐子模块：
git submodule update --init --recursive

# 全新克隆时直接带上：
git clone --recurse-submodules git@github.com:PT123123/aw-qtui.git

# 更新到上游最新（feature/inbox 会跟随远端分支）：
git submodule update --remote vendor/aw-inbox
git submodule update --remote vendor/aw-server-rust
```

`make server` 的源码定位顺序：
`-ServerSrc` 参数（独立 crate 根或 workspace 根均可）→ 环境变量 `AW_SERVER_SRC`
→ `vendor\aw-inbox`（构建源）→ `vendor\aw-server-rust\aw-inbox-rust`（需先 init 嵌套 submodule）
→ WSL 默认路径 `/home/user/project/aw-android/aw-server-rust`（旧版兜底，可能有本地未提交修改，
可用 `AW_SERVER_SRC_WSL` 覆盖 Linux 路径）。

### 缺失行为

**构建期**

- `make build`：纯客户端构建，不接触服务端，永不失败；
- `make server` / `make`（带服务端）：找不到源码 → **明确报错**并提示定位方式；
  - submodule 已登记但未 checkout（目录为空）→ 提示先执行 `git submodule update --init --recursive`；
  - cargo 构建失败 → 报错退出，不静默降级。

**运行期**（M2 已实现：本地服务端自动管理）

- 本地服务端管理（`src/awserver.{h,cpp}`）：客户端启动时经相对路径定位 `server\aw-inbox-rust.exe` →
  回环端口探测（`127.0.0.1:5600` 已监听则复用）→ 未监听则拉起（`--host 0.0.0.0` 监听所有网卡，
  `--data-dir %APPDATA%\aw-qtui\aw-qtui\server`，局域网多机互通）→
  看护轮询（每 15s 探测，异常退出自动重新拉起）→ 登录自启（HKCU Run，`server/autostart` 配置，默认开）。
  单实例互斥：`QLockFile` 防双开（重复启动直接退出）。
- 防火墙放行：server 监听 `0.0.0.0:5600` 后，首次启动检测入站规则缺失则**主动弹 UAC 请求授权**——
  **提权运行 aw-qtui 自身**（`runas` + `--firewall-allow`，UAC 授权对象是 aw-qtui，而非系统工具 net/netsh），
  提权实例执行 `netsh advfirewall` 添加规则（规则名 `aw-qtui-server`，仅限专用网络 profile）后静默退出；
  用户确认即放行，无需手动；拒绝/未提权则仅本机可用，下次启动重试。
- 服务端未启动/外部地址不可达：UI 保持可用，收件箱/同步页显示离线徽标「已离线 · 本地已存/待同步」，
  断线自动重连，本地数据离线优先（写入待同步队列，恢复后自动补推）。
- 服务端数据库：已改为 **WAL 模式**（崩溃/强杀不损坏，`integrity_check=ok`）+ 数据目录参数化（`--data-dir`），
  不再依赖 CWD；日志落盘到 `--data-dir/server.log`。
- 用 mock 联调（不拉起真实服务端）：`awqtui.exe --url http://127.0.0.1:5620`；
  如需关闭本地自动管理，可在 `awqtui.ini` 设 `server/autoManage=false`。

## 工程结构

```
aw-qtui/
├── CMakeLists.txt
├── Makefile                   # 构建入口（GNU Make 编排 cmake+ninja：客户端/服务端/dist/asan）
├── server-src/                # 构建时同步的服务端源码暂存（aw-inbox-rust crate + target）
├── vendor/                    # git submodule：aw-inbox（构建源）+ aw-server-rust（参考工作区）
├── src/
│   ├── main.cpp               # 入口（--url / --screenshot 测试钩子）
│   ├── config.h/.cpp          # 服务端地址、设备身份（MAC 生成并持久化）
│   ├── appsettings.h/.cpp     # 全局快捷键配置（INI 读写，默认 Alt+N / Alt+M）
│   ├── globalshortcut.h/.cpp  # Windows RegisterHotKey 全局热键（WM_HOTKEY -> nativeEvent）
│   ├── settingsdialog.h/.cpp  # 设置界面（快捷键录入/校验/保存）
│   ├── models.h               # Note/Tag/Comment/DeviceInfo/SyncSummary + JSON
│   ├── apiclient.h/.cpp       # QNetworkAccessManager REST 客户端（/inbox/...）
│   ├── theme.h                # 深色主题 QSS
│   ├── widgets.h/.cpp         # NoteCard / TagChip / StatusBadge / 编辑器 / 评论
│   ├── inboxpage.h/.cpp       # 收件箱页
│   ├── syncpage.h/.cpp        # 局域网同步页
│   ├── mdnsdiscovery.h/.cpp   # Win32 DNS-SD mDNS（QThread 工作线程 + 信号桥接）
│   ├── tagstore.h/.cpp        # 时间标签本地存储（段 CRUD/颜色模型/字典/快捷键/自动标签规则）
│   ├── todomodels.h           # Todo 数据模型（任务/清单/子任务/优先级/重复，字段对齐未来 Rust 契约）
│   ├── todostore.h/.cpp       # Todo 数据源抽象 TodoSource + 本地 mock TodoStore（todo_local.json 持久化 + 种子数据）
│   ├── todopage.h/.cpp        # Todo 页（TickTick 式侧栏/任务列表/详情面板）
│   ├── filterparser.h/.cpp    # 当日过滤/高级搜索共用过滤语法解析器
│   ├── autotagengine.h/.cpp   # 自动标签计算引擎（模板展开/规则匹配/间隙填充）
│   ├── autotagdialog.h/.cpp   # 自动标签规则编辑器
│   ├── addtagdialog.h/.cpp    # Add tag 窗口
│   ├── tageditordialog.h/.cpp # Tag editor（组合/单标签/快捷键/标签源）
│   ├── advancedsearchdialog.h/.cpp # 高级搜索
│   ├── untaggedview.h/.cpp    # 未标记月历热力图
│   ├── statschart.h/.cpp      # 自绘统计图表（多序列折线/柱状/平均线/图例）
│   ├── statspage.h/.cpp       # 多日统计页
│   ├── timingdialog.h/.cpp    # 计时工具（秒表/计时器/番茄钟）
│   ├── daypage.h/.cpp         # 标签 Day 页
│   └── mainwindow.h/.cpp      # 左侧导航 + 页面堆栈
├── tools/
│   ├── mock_inbox_server.py   # 联调用 mock 服务端（纯标准库）
│   └── todostore_selftest.cpp # TodoStore 本地 mock 逻辑自测（回归测试）
└── _prototype_python/         # 早期 PySide6 原型（已归档，可删）
```

## 对接的服务端端点（API 契约核对）

均来自 `aw-inbox`（Rocket，挂载在 `/inbox`）。客户端 apiclient.cpp 调用的 12 个端点
与服务端路由的**方法 / 路径 / 请求体 / 响应字段全部对上**，并已用 submodule 构建出的
`aw-inbox-rust.exe` 实测全链路（create / get / put / delete / comment / heartbeat /
sync / devices / tags 均 200 / 204）：

- 笔记：`GET/POST /inbox/notes`、`PUT/DELETE /inbox/notes/<id>`
- 标签：`GET /inbox/tags`、`GET /inbox/tags/detailed`
- 评论：`GET/POST /inbox/notes/<id>/comments`
- 同步：`POST /inbox/sync`、`GET /inbox/sync/devices`、`POST /inbox/sync/devices/heartbeat`

所有写请求带 `X-Device-ID` 头，与服务端 `DeviceIdGuard` 对齐。

**已知契约缺口（服务端侧，不影响当前单页可用性，列为待办）：**

- `GET /inbox/notes` 的 `offset`、`sort_by` 参数服务端**解析但忽略**（SQL 无 `OFFSET`、
  排序固定 `created_at DESC`，编译警告 `fields offset and sort_by are never read`）。
  已实测：`limit=1&offset=0` 与 `limit=1&offset=1` 返回同一笔记；客户端 infinite scroll
  依赖 offset 分页，笔记数超过单页 limit 时“加载更多”会重复返回同一页；
- `SyncRequest.device_versions` / `last_full_sync_at` 服务端**解析但未使用**（简化版冲突模型）；
- 服务端另有 `POST /inbox/notes/<source_id>/relations/<target_id>`、
  `GET /inbox/notes/<note_id>/relations`、`POST /inbox/route-debug`（调试）——客户端当前未用；
- 无 `/healthz`、无 `/version` 端点（M3 规划补 `/inbox/version`）。

## 任务（Todo）数据层与 Rust 对接点

Todo 页只依赖 `TodoSource` 抽象（`src/todostore.h`），当前实现 `TodoStore` 是**本地 mock**：

- 内存态 + `%APPDATA%\aw-qtui\aw-qtui\todo_local.json` 原子持久化，首次运行写入种子数据；
- 全部写操作「改内存 → 保存 → 异步广播 `dataChanged`」（`QTimer` 后投递），模拟未来
  Rust 服务端 `QNetworkAccessManager` 的异步回包，页面按信号驱动渲染，不假设同步可见；
- 重复任务完成时自动生成下一实例（daily / weekdays / weekly / monthly）。

**接入 Rust**：新增 `class TodoApiStore : public TodoSource`，用 HTTP 实现同一套
方法（lists/tasks 快照 + create/update/delete/completed/subtask）并把服务端响应转为
`dataChanged` 信号，`TodoPage` 代码零改动。`TodoStore` 的字段（`todomodels.h`）即后续
Rust 契约的字段基线。

自测：`tools/todostore_selftest.cpp`（覆盖重复任务/CRUD/子任务/删清单迁移/持久化重载，
编译方式见文件头注释，用独立应用名运行不碰真实数据）。

## mDNS 说明

局域网自动发现使用 Windows 10+ 原生 `DnsServiceBrowse` / `DnsServiceResolve` /
`DnsServiceRegister`（`dnsapi.dll`），不依赖 Bonjour 或第三方库。服务类型
`_activitywatch._tcp.local.`，与 `aw-sync-transport/src/discovery.rs` 一致。
发现/注册在独立 QThread 里运行，结果通过 Qt 信号投递到 UI 线程。
若所在网络屏蔽多播，可用「手动添加对端」兜底。

## 验证

- CMake + MSVC 19.44 编译链接通过，产物 `awqtui.exe`（~270KB）
- 连 mock 服务端启动：收件箱加载 3 条种子笔记、5 个标签、状态「已连接」；
  同步页设备表加载、心跳注册、mDNS 浏览/注册接口可用
- API 全链路：创建 / 标签过滤 / 更新 / 评论 / sync / 心跳 / 设备表 / 删除
