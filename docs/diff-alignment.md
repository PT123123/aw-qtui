# Android ↔ Qt UI 端差异与对齐说明

> 本文档基于 `aw-android-native`（Kotlin + ViewBinding + MVVM）与 `aw-qtui`（Qt6 Widgets C++17）的对比，记录两端功能对应关系、差异点，以及已完成的对齐实现。

## 1. 总体架构对比

| 维度 | Android (`aw-android-native`) | Qt UI (`aw-qtui`) |
|------|-------------------------------|-------------------|
| 语言 / UI | Kotlin + XML + ViewBinding | C++17 + Qt6 Widgets |
| 导航 | 抽屉式 NavigationView | 左侧可折叠分组导航栏 |
| 网络 | Retrofit + OkHttp | QNetworkAccessManager |
| 本地存储 | Room / DataStore | SQLite (LocalStore) |
| 架构 | MVVM + LiveData | 直接调用 + 信号槽 |
| 主题 | Material You + 自定义 | 多主题（8 套）纯代码绘制 |

## 2. 功能模块对应关系

### 2.1 已对齐模块

| 功能 | Android | Qt UI | 状态 |
|------|---------|-------|------|
| Inbox（收件箱） | InboxFragment | InboxPage | ✅ 已对齐 |
| 笔记 CRUD | NoteViewModel | ApiClient + InboxPage | ✅ 已对齐 |
| 笔记历史版本 | HistoryFragment | InboxPage 内对话框 | ✅ 已对齐 |
| 笔记评论 | CommentViewModel | ApiClient + 对话框 | ✅ 已对齐 |
| 笔记关系 | RelationDialog | ApiClient + 对话框 | ✅ 已对齐 |
| 标签管理 | TagFragment | TagStore + DayPage | ✅ 已对齐 |
| 任务（Todo） | TodoFragment | TodoPage + TodoSource | ✅ 已对齐 |
| 专注（Focus） | FocusStore + FocusUi | FocusTimerPage + 7 子页 | ✅ 已对齐 |
| 回收站（Trash） | TrashFragment | TrashPage | ✅ 已对齐 |
| Activity 统计 | DashboardFragment | ActivityPage (Summary/Window/Browser/Editor) | ✅ 已对齐 |
| 时间线 | TimelineFragment | TimelinePage + TimelineWidget | ✅ 已对齐 |
| 局域网同步 | SyncFragment | SyncPage | ✅ 已对齐 |
| mDNS 发现 | NsdManager | MdnsDiscovery (Dnsapi) | ✅ 已对齐 |
| 设备配对 | PairingDialog | SyncPage 内对话框 | ✅ 已对齐 |
| WebDAV / S3 云备份 | CloudBackup | SyncPage 内配置 | ✅ 已对齐 |

### 2.2 已补齐模块（Android 有 → Qtui 新增）

| 功能 | Android 实现 | Qt UI 补齐 | 状态 |
|------|--------------|------------|------|
| 秒表（Stopwatch） | StopwatchFragment + FocusStore | StopwatchPage (aw-stopwatch-android bucket) | ✅ 已补齐 |
| Query Explorer | QueryFragment + QueryViewModel | QueryPage (POST /api/0/query) | ✅ 已补齐 |
| Activity 趋势 Tab | DashboardFragment (Overview/Timeline/Trends) | ActivityPage Trends Tab (Top Apps/Cats/Daily) | ✅ 已补齐 |
| 日期快速选择 chips | chipToday/chipYesterday/chipLast7/chipLast30/chipAll | ActivityPage 5 个日期 chip 按钮 | ✅ 已补齐 |
| 同步详情独立页 | SyncDetailsFragment | SyncDetailsPage (日志筛选/分页/明细展开/回收站) | ✅ 已对齐 |

### 2.3 Qtui 独有功能（Android 无对应）

| 功能 | 说明 |
|------|------|
| 多主题系统 | 8 套主题（暗夜蓝/石墨/紫罗兰/森林绿/琥珀暖/海洋青/珊瑚红/明亮） |
| 全局热键 | 系统级快捷键（添加笔记、唤醒窗口等） |
| UI 缩放 | Ctrl+滚轮 / +/- 缩放整体界面 |
| 系统托盘 | 最小化到托盘、托盘菜单 |
| 统计面板（StatsPage） | 标签使用统计、每日趋势等高级图表 |
| 专注 Focus 全集 | 7 个专注子页面（计时/概览/详情/周/热力图/最佳/日历/纪念日） |
| 标记 Day | 每日标签统计页面 |
| 高级搜索对话框 | 多条件组合搜索 |
| 自动标签引擎 | 基于规则的自动分类 |

## 3. 新增实现说明

### 3.1 StopwatchPage（秒表）

- **位置**：`src/stopwatchpage.h` / `src/stopwatchpage.cpp`
- **功能**：手动计时、暂停/继续/停止、历史列表
- **数据存储**：停止时通过 `ApiClient::heartbeat("aw-stopwatch-android", ...)` 写入 AW bucket
- **导航**：ACTIVITYWATCH 分组，⏱ 图标
- **页面索引**：`PAGE_STOPWATCH = 17`

### 3.2 QueryPage（Query Explorer）

- **位置**：`src/querypage.h` / `src/querypage.cpp`
- **功能**：手写/预置 AQL 脚本 → POST /api/0/query → pretty-print JSON
- **预置脚本**：今日活动 Top 应用、活跃时段分布、浏览器 Top 域名
- **导航**：ACTIVITYWATCH 分组，🔍 图标
- **页面索引**：`PAGE_QUERY = 18`

### 3.3 ActivityPage 趋势 Tab + 日期 chips

- **位置**：`src/activitypage.h` / `src/activitypage.cpp`
- **日期 chips**：Today / Yesterday / Last 7 days / Last 30 days / All（单选 chip 组，替代原单一日期切换）
- **趋势 Tab**：Top Applications (Trend) + Top Categories (Trend) + Daily Activity（多日聚合柱状图）
- **数据结构**：`m_dateStart` / `m_dateEnd` 替代原 `m_date` 单一日期，支持范围查询
- **导航**：F5 刷新支持

### 3.4 SyncDetailsPage（同步详情独立页）

- **位置**：`src/syncdetailspage.h` / `src/syncdetailspage.cpp`
- **功能**：日志表格（时间/方向/协议/事件/状态/消息）+ 筛选（方向/协议/事件）+ 分页（50 条/页）+ 双击行展开传输明细 + 回收站管理
- **明细面板**：底部 QPlainTextEdit 展示选中日志的逐条传输记录（actionLabel 中文映射）
- **回收站**：刷新 / 清空 / 恢复单条 / 永久删除
- **导航**：同步分组，📋 图标；"← 返回同步" 按钮返回 SyncPage
- **页面索引**：`PAGE_SYNC_DETAILS = 19`

## 4. 关键 API 对应

| API 路径 | Android 调用 | Qt UI 调用 |
|----------|--------------|------------|
| `/api/0/buckets` | BucketViewModel | `ApiClient::getBuckets()` |
| `/api/0/buckets/<id>/events` | EventViewModel | `ApiClient::getEvents()` |
| `/api/0/buckets/<id>/heartbeat` | FocusStore.heartbeat() | `ApiClient::heartbeat()` |
| `/api/0/query` | QueryViewModel.postQuery() | `ApiClient::postQuery()` |
| `/api/0/sync/*` | SyncViewModel | `ApiClient::*` (30+ 方法) |
| `/inbox/notes` | NoteViewModel | `ApiClient::*` (CRUD + history + comments + relations) |
| `/inbox/todos` | TodoViewModel | `ApiClient::*` (CRUD + restore) |

## 5. 已知差异 / 待评估项

| 差异 | 影响 | 建议 |
|------|------|------|
| 秒表存储 bucket 名 | Android: `aw-stopwatch-android`，已对齐 | 无需改动 |
| 趋势 Tab 多日聚合 | Qtui 当前从单请求全量数据在客户端按天聚合，非逐日请求 | 数据量大时考虑服务端聚合 API |
| 同步详情日志分页 | Qtui 客户端分页，服务端已支持 limit/offset | 已正确使用分页参数 |
| 安卓抽屉导航 vs Qtui 侧栏导航 | 交互习惯差异 | 保持各平台原生习惯，无需强制统一 |
| 主题系统 | Qtui 独有 | 保留，作为桌面端优势 |

## 6. 构建说明

```bash
# 配置 + 构建 Release
bash -c '. tools/vcenv.sh && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release -j'

# 运行
./build/awqtui.exe
```

> 依赖 VC 环境（通过 `tools/vcenv.sh` 注入 INCLUDE/LIB/PATH），Qt 6.8.3 msvc2022_64。
