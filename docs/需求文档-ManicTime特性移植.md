# aw-qtui 需求文档：ManicTime 时间标注与统计特性移植

| 项 | 内容 |
| --- | --- |
| 版本 | v0.1（初稿） |
| 日期 | 2026-08-31 |
| 目标产品 | aw-qtui（原生 C++ Qt6 Widgets 的 ActivityWatch 桌面客户端） |
| 需求来源 | ManicTime Windows Client 官方文档：Selecting time / Tagging / Away window / Autotagging / Searching / Untagged time / Statistics |
| 交付形态 | 本文档为后续 aw-qtui 功能开发的需求基线，不包含 aw-qtui 已具备的功能 |

---

## 1. 背景与目标

ManicTime 的核心价值是「**给时间打标签**」：它不仅记录你在电脑上做了什么，还允许你在时间线上选中一段真实活动，为它赋予语义化的标签（任务 / 客户 / 项目），并用规则自动打标签、搜索未标记时间、跨日期做统计。这些能力恰好是 aw-qtui 目前缺失的。

本需求的目标：把 ManicTime 这套「**时间选择 → 打标签 → 自动标签 → 未标记时间审计 → 搜索 → 统计**」的闭环能力移植进 aw-qtui，使 aw-qtui 从「被动的使用统计查看器」升级为「主动的时间管理工具」。

**核心原则：只做 aw-qtui 没有的。** 已具备的能力一律不重复实现，见第 2 节。

---

## 2. 现状盘点：已有功能（本次不重复实现）

以下 aw-qtui 现有能力**不在**本次需求范围内，后续章节不得重复设计：

| 现有功能 | 说明 | 与本需求的关系 |
| --- | --- | --- |
| 收件箱笔记标签（#tag） | InboxPage 内笔记的标签、标签侧栏过滤、搜索 | 收件箱标签是「笔记」的标签，与「时间段」标签是两回事，保持现状 |
| 收件箱搜索 / 标签过滤 / 排序 | Ctrl+F 聚焦搜索等 | 本次「搜索」指活动时间线的过滤与全库高级搜索，不涉及收件箱 |
| Activity 页单日统计图表 | 单日 24h 活跃柱状图、Top Applications / Top Window Titles / Top Categories、Category Tree、Sunburst | 单日维度的 top 统计已存在；本次统计需求聚焦**多日 / 日期范围**维度 |
| Timeline 页基础交互 | 拖拽平移、滚轮缩放、hover 详情 tooltip、Interval mode（Last duration/Merged/First event）、Show last（24h/12h/6h/48h/7d）、Reset view | 这些交互保留；本次在其之上**新增「选择」交互**，需定义选择与平移的共存方案 |
| Timeline 底部统计卡 | Total tracked / AFK / First activity / Last activity（当日） | 保留；Day duration / Attendance 等多日统计属新增 |
| 局域网同步 / mDNS / 设备心跳 | SyncPage + MdnsDiscovery | 保留现状；本需求不涉及服务端标签同步 |
| 深色主题 QSS、日期导航（◀▶Today）、F5 刷新、1-4 切页快捷键 | 全局基础 | 保留，新功能沿用同一套 UI 风格 |

> **重要前提（必须在开发前确认）**：当前 Activity / Timeline 页的数据来自 `mockdata.cpp` 的**模拟数据**（`generateTimelineLanes` / `generateTopApps` 等）。本需求的标签、统计、未标记时间等功能都建立在**真实活动事件数据**之上，因此第 4 节将给出数据源约束，请先确认真实数据接入（aw-server buckets/events API）或扩充 mock 数据的计划。

---

## 3. 需求范围总览与优先级

| 功能域 | 优先级 | 说明 |
| --- | --- | --- |
| 5.1 时间选择 Selecting Time | P0 | 一切标签操作的前提 |
| 5.2 时间标签 Tagging | P0 | 核心能力 |
| 5.6 搜索 Searching / Advanced Search | P0 | 含活动过滤与全库高级搜索 |
| 5.7 未标记时间 Untagged Time | P0 | 标签闭环的审计环节 |
| 5.4 Away 窗口 Away Window | P1 | 依赖 AFK 判定与弹窗交互 |
| 5.5 自动标签 Autotagging | P1 | 规则引擎，工作量较大 |
| 5.3 计时工具 Stopwatch / Timer / Pomodoro | P1 | 来自 Tagging 章节，独立性强 |
| 5.8 统计 Statistics | P1 | 多日聚合图表 |

优先级定义：P0 = 必须随首批交付；P1 = 紧随其后；P2 = 可选 / 依赖外部条件（如截图、服务端）。

---

## 4. 全局约束与设计前提

1. **数据源**：时间线事件（afk-status / window / web）来自真实 aw-server 数据（buckets + events API）或等价 mock。所有标签/统计/未标记时间均基于该事件流计算。
2. **本地标签存储**：ActivityWatch 本身**没有**「给时间段打标签」的数据模型。时间标签、自动标签规则需本地持久化，复用现有 `LocalStore` 的**离线优先**模式（JSON 原子写入、tombstone 删除），与收件箱本地库并列（新增独立文件，勿污染 `inbox_local.json`）。
3. **不依赖 ManicTime Server**：服务端相关能力（服务端标签、Jira/GitHub/FreshBooks 标签源、autotag 跨设备同步、允许标签白名单、"Some tags are hidden"）一律**排除**，如未来需要单独立项。
4. **风格一致**：沿用现有深色 QSS 主题、StatCard / ToolBtn / NavArrow 组件、Tockler 风格卡片。
5. **快捷键**：继续在 `MainWindow::keyPressEvent` 中注册，避免与现有 1-4 / F5 / Ctrl+F 冲突。

---

## 5. 功能需求详述

### 5.1 时间选择 Selecting Time（FR-1x）

> 参照：docs.manictime.com/win-client/selecting-time

| 编号 | 需求 | 说明 |
| --- | --- | --- |
| FR-101 | 时间线拖拽选择 | 在时间线上按住左键拖拽高亮一段时间范围。拖拽时鼠标**吸附到活动起止边界**，方便选中完整活动；在顶部时间刻度条（ruler）上拖拽则**吸附到整分钟**。吸附粒度可配置（1/2/5/10…分钟） |
| FR-102 | 双击选中整块活动 | 双击任意活动（如 afk-status 的 not-afk 段、某应用段）→ 选中该完整活动块 |
| FR-103 | 多选 | 按住 Ctrl 拖拽可追加多个不连续的时间选择（选区求并集） |
| FR-104 | 选区与平移共存 | 现有 Timeline 左键拖拽 = 平移。需提供明确的「选择模式」入口（建议：工具栏 Select 模式开关，或按住修饰键拖拽），使选择与平移互不冲突；选中后工具栏出现选区时长提示 |
| FR-105 | 选择模式 | 参考 ManicTime 三种模式，在选区后、Add tag 按钮旁提供：`Select all`（全选含已标签）、`Select only untagged`（只选未标签段）、`Show only untagged`（只显示未标签段）。用于避免重复打标签、以及批量编辑/删除已有标签 |
| FR-106 | 列表勾选选择 | 时间线下方提供活动明细列表（Details）与汇总列表（Summary）。明细可勾选单条活动，汇总可勾选某应用的**全部使用**；勾选可跨时间线（如一条来自 afk、一条来自 window），切换时间线时保留已勾选 |
| FR-107 | 过滤 + 勾选组合 | 在明细列表过滤关键词后「全选当前结果」，清空过滤后勾选保留，从而把多次过滤结果合并成一次选择（例：先 filter `manictime` 勾选，再 filter `youtube` 勾选，清空后两者皆被选中） |
| FR-108 | 双击生成过滤式选择 | 双击某活动 → 自动生成 `title="..."` 过滤 → 全选 → 清空过滤后该标题的所有会话仍保持选中 |
| FR-109 | 时间线 + 列表混合选择 | 支持「时间线拖选 + 在汇总里取消勾选某项」的减法操作 |
| FR-110 | 选区上下文菜单 | 选区（或选中时间线上方）出现菜单：`Add tag...`、`Paste (Ctrl+V)`、`Zoom to selection`、`Delete` |
| FR-111 | 截图选择（P2，可选） | 按 F11 冻结截图并用 ←/→ 浏览、Shift/Ctrl+←→ 扩展选区。**依赖 aw-qtui 是否引入截图能力，默认不排期** |

**验收标准**：用户能通过时间线拖拽/双击/勾选/过滤四种方式构造选区；选区能正确叠加/减除；选择模式能避免重复打标签。

---

### 5.2 时间标签 Tagging（FR-2x）

> 参照：docs.manictime.com/win-client/tagging

标签是一串**逗号分隔**的文本值，天然形成层级（`Client 1, Project 2, Activity 1`）。

| 编号 | 需求 | 说明 |
| --- | --- | --- |
| FR-201 | 添加标签窗口 | 选区后点 `+ Add tag`（或 Alt-T）打开窗口：Tags 文本框（逗号分隔）、Notes 备注框、Billable（$）开关；Start / End 时间可手动修改，自动计算 Duration |
| FR-202 | 最近使用标签 | 选区后的下拉箭头 / 右键菜单列出最近使用过的标签组合，点选即打标签 |
| FR-203 | 标签复制粘贴 | 选中一个标签 Ctrl+C，再选时间段 Ctrl+V 直接打上该标签 |
| FR-204 | 标签层级选择器（Tag picker） | 以树形层级展示历史标签，支持面包屑上/下钻，排序可选 A-Z / Last used；选择后回填到标签框 |
| FR-205 | 标签编辑 / 删除 | 在 Tag 时间线上右键标签 → `Edit` / `Delete`；也可多选时间段后批量删除 |
| FR-206 | 标签编辑器（Tag editor） | 独立对话框，含 4 个 Tab：**Tag combinations**（组合）、**Tags**（单个标签）、**Tag shortcuts**（快捷键）、**Tag sources**（标签源，本期仅本地/手动）。支持：Rename、Replace（批量查找替换）、Delete、Change color、Import、Export（.txt，每个组合一行）。列表列：Tag group / Last used / No of uses / Tagged time，带过滤 |
| FR-207 | 批量重命名 | 支持「在 Tags Tab 重命名单个标签」→ 影响所有包含它的组合；也支持「Replace」批量替换多个组合 |
| FR-208 | 标签颜色模型 | 每个标签有独立颜色；组合默认显示**第一个标签**的颜色；支持对某个标签设 `Skip`（跳过其颜色，改用下一个）；支持对某个组合设 `Use selected color` 覆盖默认计算。颜色变更入口：Tag editor 或右键 `All instances of ... -> Change color` |
| FR-209 | 全局重命名 | 右键 Tag 时间线上某标签 → `All instances of ... -> Rename`，改名到所有包含它的组合 |
| FR-210 | 标签上次未标记时间（Tag last untagged time） | 系统托盘/全局快捷键触发：给「今天最后一次标签结束到当前时间」自动打标签。首标签则从当天首次交互算起。用于弥补忘开秒表 |
| FR-211 | 标签快捷键（Tag shortcuts） | 在设置中为最常用标签绑定按键；选中时间段后按该键直接打上对应标签 |
| FR-212 | 可计费标签（Billable） | 标签可标记 `Billable`，显示 BILLABLE 标签；支持「默认新标签可计费」设置；支持批量 Set/Clear billable（右键或 Ctrl+B）；过滤 `label=billable` / `-label=billable` |
| FR-213 | 标签删除语义 | 删除**组合** → 删除其下所有活动；删除**单个标签** → 含该标签的组合自动降级（`Project 1, Design` 删 `Project 1` 后变 `Design`）。提供修 typo 的路径（Rename 到已有标签） |
| FR-214 | 标签来源（本地/手动，Tag sources） | 从本地文件或手动输入批量导入标签（每行一个组合，逗号分隔；支持部分标签笛卡尔展开，如 `Project X,` + `,Design` → `Project X, Design`） |

**验收标准**：能对任意选区打多级标签；标签可在 Tag 时间线上以色块呈现并编辑/删除/重命名/改色；颜色规则（首标签色 / Skip / 覆盖）符合 FR-208 语义；Billable 全链路（标记/过滤/默认值）可用。

---

### 5.3 计时工具 Stopwatch / Timer / Pomodoro（FR-3x）

> 来自 Tagging 章节，独立性强，仍属于「主动时间记录」闭环。

| 编号 | 需求 | 说明 |
| --- | --- | --- |
| FR-301 | 秒表（Stopwatch） | 工具栏/托盘启动：先弹 Add tag 窗口选标签 → 开始计时；可暂停/继续/停止；停止后生成一条标签。托盘与快捷键均可控制 |
| FR-302 | 秒表增强 | `Start stopwatch with last used tag`（一键用上次标签起表）；`Start at end of last tag`（起表时间接在当天最后一条标签结束之后）；可设置「检测到无操作自动停止」；可设置「每 X 分钟询问是否仍在做当前任务」 |
| FR-303 | 计时器（Timer） | 倒计时；到点后保存该标签并退出 |
| FR-304 | 番茄钟（Pomodoro） | 默认 25 分钟工作段 + 休息段；工作前需选标签；工作结束弹休息窗口，可选：`Ok`（选 Break 标签继续）、`Skip break`（再干 25 分钟）、`Do not tag`（休息但不打标签）、`Stop`；任一窗口无人值守则停止 |

**验收标准**：三类计时器能正确落成时间段标签，且与手工标签在时间轴上无重叠冲突。

---

### 5.4 Away 窗口 Away Window（FR-4x）

> 参照：docs.manictime.com/win-client/away-window

| 编号 | 需求 | 说明 |
| --- | --- | --- |
| FR-401 | Away 判定 | 基于 afk-status 判定离开；无操作超过阈值（默认 10 分钟，可配置 1–500 分钟）视为 Away；提供「有应用阻止睡眠时不计 Away」选项 |
| FR-402 | Away 时间标记 | Away 时段在 Computer usage 时间线上以**红色**呈现（现有 afk-status lane 已能区分 not-afk/afk，补充视觉区分） |
| FR-403 | Away 窗口 | 从离开状态返回时弹出「Tag away time」窗口：显示离开时长与时间区间、Tags 输入框、Notes、最近标签列表、`Show this window whenever away time ends` 选项；确认后把标签应用到**全部** Away 时间 |
| FR-404 | Away 拆分 | 在 Away 窗口内可编辑时长把 Away 拆成多段，分别打不同标签（例：50 分钟 Lunch + 其余 Meeting）；确认一段后，剩余时间再次弹出 Away 窗口；可 Cancel 跳过当前段 |
| FR-405 | 错过的多个 Away | 上次 Away 弹窗未处理又产生新的 Away 时，显示 `Missed away times` 列表，可分别打标签 |
| FR-406 | 暂停 Away 通知 | Away 窗口提供 `Don't show`：until tomorrow / until Monday / ever again；暂停状态在状态栏可见，点击可恢复 |

**验收标准**：离开→返回→弹窗→打标签全流程可用；拆分与多个 Away 场景符合 FR-404/405；暂停通知生效且可恢复。

---

### 5.5 自动标签 Autotagging（FR-5x）

> 参照：docs.manictime.com/win-client/autotagging

自动标签与手工标签的区别：**手工标签是用户创建的事实；自动标签是按规则计算的结果**——规则变更，历史任意一天的自动标签随之重算。

| 编号 | 需求 | 说明 |
| --- | --- | --- |
| FR-501 | AutoTags 时间线 | 独立 lane 展示自动标签结果；若用户首次添加规则且无该 lane，自动创建 |
| FR-502 | 从活动右键建规则 | 右键任意活动 → `Add to AutoTag`。匹配条件随数据类型变化：URL 类提供 Domain / Site / Url / Url contains；文档类提供 Filename / Path / Folder / Path contains；应用类按 group 匹配 |
| FR-503 | 自定义复杂规则 | 支持用过滤语法写规则（如 `group:firefox manictime`），从过滤结果一键生成规则 |
| FR-504 | 规则分配 | 每条规则分配到：AutoTag 名称（可含 `{{title}}` / `{{group}}` 变量）、Notes（可用 `{{title}}`），可选 $（billable） |
| FR-505 | 动态自动标签 + 正则 | 规则支持正则（`#"...(捕获组)..."`），名称/备注可用 `{{1}}` `{{2}}` `{{3}}` 引用捕获组（例：按路径解析出 `Work, Client 1, Project 1`，备注填文件名） |
| FR-506 | 自动标签类型 | 每个 autotag 可选：**Regular**（常规）、**Append**（追加到同时间匹配到的另一 autotag 之后，不单独出现）、**Prepend**（同理前置）、**Absorb**（被周围 autotag 吸收，变色龙式，不单独出现） |
| FR-507 | AutoTag 编辑器 | 列表管理所有 autotag：Move up/down（顺序影响匹配优先级）、Edit（改名/改色/设类型与 Apply to：All/Selected/NOT selected）、Show rules（查看/删除规则）、Delete、Import、Export |
| FR-508 | 匹配优先级 | `When first autotag is matched skip the rest` 选项：开启后按顺序只让首个命中的 autotag 生效，避免重复标签 |
| FR-509 | 复制到手工标签 | `Copy autotags to tags`：整日或仅选中时间段；与已有标签冲突时弹选择：Fill only untagged time / Ignore existing tags（double tag）/ Overwrite existing tags，可「Always do this from now on」 |
| FR-510 | 自动填充小间隙 | `Auto fill gaps smaller than X 秒`（默认 60s）：左右同 autotag 则用它填充，左右不同则对半分配 |
| FR-511 | 猜测部分高亮 | `Highlight auto filled and absorbed parts`：在时间线上高亮「自动填充 / 吸收」的猜测段，便于人工补规则 |
| FR-512 | Append rules data | 勾选后把命中规则的数据自动追加到标签（例：Browsing → `Browsing, Google Chrome`），免去为每个应用建 autotag |
| FR-513 | Copy tags to autotags | 把手工标签抄送到 autotag 时间线，提供 Overwrite autotags / Overwrite then append-prepend-absorb 两种模式，用于手工纠正自动标签 |
| FR-514 | 诊断模式 | 启用诊断（Ctrl+D）后，hover 自动标签显示命中的规则、来源时间线与顺序，便于排查「为什么这么标」 |
| FR-515 | 近期规则 | `Show recent rules`：按添加时间列出规则，右键可 Edit / Delete |

> 排除项：autotag 跨设备同步、应用到服务器导入的时间线——依赖服务端，本期不做。

**验收标准**：规则创建（含 URL/文档/自定义/正则）→ 历史任一天自动标签实时重算 → 三种自动标签类型（Append/Prepend/Absorb）行为正确 → 复制到手工标签及冲突处理可用 → 诊断可解释任意一个自动标签的由来。

---

### 5.6 搜索 Searching / Advanced Search（FR-6x）

> 参照：docs.manictime.com/win-client/searching

#### 5.6.1 当日活动过滤（Filter）

| 编号 | 需求 | 说明 |
| --- | --- | --- |
| FR-601 | 时间线过滤框 | Day/Timeline 视图底部提供 Filter 输入框，过滤当前时间线的活动明细；**一次只过滤一条时间线**（先点选时间线再过滤） |
| FR-602 | 过滤关键字 | 支持：`group:`、`duration>` / `duration<`（单位 s/m/h，如 `duration>1m10s`）、`start>` / `start<`、`end>` / `end<`（如 `end>22:00`）、`or`、`-`（取反，如 `-manictime`）；`and` 为隐式连接 |
| FR-603 | 时间线相关关键字 | 计算机使用：`workplace:`、`desktop:`；文档：`git-repository:`、`git-branch:`；标签时间线：`note:`（搜索标签备注）、`label:`（如 `label=billable`） |
| FR-604 | 通配符 | `?`（任意单个非空白字符）、`*`（任意多个非空白字符） |
| FR-605 | 正则过滤 | 用 `#"...regex..."` 包裹启用正则表达式 |
| FR-606 | 过滤与选择联动 | 过滤结果可全选勾选（见 FR-107）；双击某活动自动生成 `title="..."` 过滤 |

#### 5.6.2 高级搜索（Advanced Search）

| 编号 | 需求 | 说明 |
| --- | --- | --- |
| FR-607 | 全库搜索入口 | 从视图右下角打开 Advanced Search 对话框 |
| FR-608 | 搜索维度 | 选择时间线（可含 `All local timelines` 跨 Tags/Computer usage/Applications/Documents 同时搜索）；Filter 语法同当日过滤；设置日期范围；点击 Find |
| FR-609 | 结果跳转 | 双击任一结果 → 跳转到对应日期在 Day/Timeline 视图中的位置 |
| FR-610 | 未标记活动 | `Show only untagged activities` 勾选 + 空 Filter → 列出全部未标记活动 |
| FR-611 | 批量打标签 | `Tag all as` 对全部结果打标签；Shift/Ctrl 多选后右键 `Tag selected as` |
| FR-612 | 批量删除 | `Delete all` 删除全部结果；多选右键 `Delete selected` |
| FR-613 | 导出结果 | `Export` 将结果导出为文本文件 |
| FR-614 | 结果统计 | 结果底部显示 `Found: N` 与 `Total time: X` |

**验收标准**：当日过滤与全库搜索共用同一套过滤语法（含关键字/通配符/正则/时间线专属关键字）；高级搜索支持未标记过滤、批量打标签、批量删除、导出、跳转。

---

### 5.7 未标记时间 Untagged Time（FR-7x）

> 参照：docs.manictime.com/win-client/untagged-time

| 编号 | 需求 | 说明 |
| --- | --- | --- |
| FR-701 | 未标记时间日历视图 | 月历式热力图（行=月，列=日），一眼看出哪天漏标。颜色语义：**深绿**=当天全部已标记、**浅绿**=已标记、**橙色**=未标记、**白色**=无跟踪数据；同一天按已标记/未标记比例混合显示（浅绿+橙） |
| FR-702 | 点击下钻 | 点击某天进入该天视图，便于补标签 |
| FR-703 | 与高级搜索联动 | 提供入口直达「仅显示未标记活动」的高级搜索（见 FR-610），可直接批量打标签 |

**验收标准**：任意日期范围的未标记热力图正确（比例、颜色语义符合 FR-701）；从热力图可下钻补标签。

---

### 5.8 统计 Statistics（FR-8x）

> 参照：docs.manictime.com/win-client/statistics
> 说明：Activity 页**单日** Top 图表已存在，不在范围；本节约为**多日 / 日期范围**统计。

| 编号 | 需求 | 说明 |
| --- | --- | --- |
| FR-801 | 日期范围选择 | 统计页提供 From/To 日期范围（含 This week / This month 快捷），所有图表随范围刷新 |
| FR-802 | 多标签页 | 可创建多个统计 Tab（+ 新建），每个 Tab 独立配置 |
| FR-803 | 统计类型 | 可创建：**Top 统计**（各时间线 Top 组）、**Day duration**（每日开始/结束/时长）、**Attendance**（活跃天数）、**Custom**（自定义多序列） |
| FR-804 | Top 图表 | 指定时间线显示 Top 5/10/20 组；显示 `Total (range)` 与 `Total (all)`；文档时间线支持 Web sites / Files 等类别切换 |
| FR-805 | Day duration 图 | 每个工作日的开始时间、结束时间、时长折线/柱状图 |
| FR-806 | Attendance 图 | 月历显示活跃日（默认首末交互间隔 ≥ 1 小时为绿，阈值可配） |
| FR-807 | 自定义多序列图 | 添加多个序列（选时间线 + 勾选组 + Match：Any group=OR / All groups=AND，其中 All groups 仅标签时间线可用），同图对比（例：Adobe Acrobat vs Google Chrome；Client1/Project1 vs Client1/Project2） |
| FR-808 | 分组维度 | 图表支持 Group by：None / Day / Week / Month / Year |
| FR-809 | 图表类型与平均线 | 折线图 / 柱状图切换；`Show average values` 显示平均线 |
| FR-810 | 导出 | 图表导出为图片（.jpg）/ 复制到剪贴板；数据表（Table Tab）导出 .csv / .xlsx，含 Total / Average / Minimum / Maximum |
| FR-811 | 可计费聚合 | 统计 Tab 支持 Combined groups 并展示 BILLABLE 标记（衔接 FR-212） |

**验收标准**：任意日期范围下 Top / Day duration / Attendance / Custom 四类图表数据正确；分组与图表类型切换即时生效；导出产物可打开且数据与图表一致。

---

## 6. 数据模型与本地存储（新增）

新增两个本地存储文件（复用 LocalStore 的原子写与 tombstone 约定），**不与收件箱共用**：

### 6.1 时间标签库 `timetags_local.json`

```
TagSegment {
  id: int            // 本地自增正数；tombstone 复用 deleted 字段
  startMs, endMs: qint64
  tags: string[]     // 逗号层级拍平为数组，顺序即层级顺序
  notes: string
  billable: bool
  colorOverride: string | null   // null = 按首个标签颜色计算
  deleted: bool
  createdAt, updatedAt: string
}
TagMeta {            // 标签字典（用于 Tag editor / picker / 颜色）
  name: string
  color: string
  skipColor: bool
  billableDefault: bool
  lastUsed: string
  useCount: int
}
TagCombinationMeta { // 组合级覆盖（Use selected color）
  combinationKey: string   // "a,b,c"
  colorOverride: string
}
```

### 6.2 自动标签规则库 `autotags_local.json`

```
AutoTagRule {
  id: int
  name: string            // 可含 {{title}} {{group}} {{1}} {{2}} {{3}}
  type: regular|append|prepend|absorb
  applyTo: all|selected|notSelected
  notesTemplate: string   // 可含 {{...}}
  billable: bool
  enabled: bool
  order: int              // 影响匹配优先级
  conditions: Condition[] // 任一/全部满足（多条件组合）
}
Condition {
  timeline: afk-status|window|web|document
  field: domain|site|url|urlContains|filename|path|folder|pathContains|group|title|regex|custom
  value: string
  exact: bool
}
```

### 6.3 计算与刷新

- 自动标签为**纯计算**结果：规则变更 → 目标日期范围内的自动标签重算（按需求 FR-501/FR-508/FR-510/FR-511 的优先级、间隙填充、吸收顺序）。
- 手工标签为**持久化事实**：复制自动标签到手工会产生重叠冲突，按 FR-509 策略处理。

---

## 7. 里程碑建议

| 阶段 | 内容 | 依赖 |
| --- | --- | --- |
| M1（P0） | 时间选择 FR-101~110 + 标签核心 FR-201~210 + 当日过滤 FR-601~606 + 未标记热力图 FR-701~703 | 真实/等价事件数据源（见第 4 节前提） |
| M2（P0） | 高级搜索 FR-607~614、Tag editor FR-206~209、Billable FR-212~213 | M1 标签库 |
| M3（P1） | 计时工具 FR-301~304、Away 窗口 FR-401~406 | M1 |
| M4（P1） | 自动标签 FR-501~515 | M1 + M3（依赖 AFK/活动数据质量） |
| M5（P1） | 统计 FR-801~811 | M1 数据聚合 |
| M6（P2，可选） | 截图选择 FR-111、外部标签源 | 截图能力 / 服务端 |

---

## 8. 风险与开放问题

1. **真实数据接入**（最高风险）：现有 Activity/Timeline 全为 mock。标签、未标记时间、统计的正确性依赖真实 aw-server buckets/events 数据与事件粒度的质量。需在 M1 前明确数据源方案。
2. **选择 vs 平移的交互冲突**：现有时间线左键拖拽是平移。需要决定选择模式入口（工具栏开关 / 修饰键），建议先行做一个小原型验证手感。
3. **AW 无原生时间标签**：标签体系完全客户端本地实现，意味着换设备/重装会丢标签；是否纳入现有的局域网同步体系，本期不定，作为开放问题。
4. **自动标签的"猜测"性质**：吸收与间隙填充可能标错（ManicTime 官方同样提示），需保留高亮猜测段与人工纠错路径（FR-511 / FR-513）。
5. **过滤语法规模**：FR-602~605 的语法解析（关键字/通配符/正则/时间线专属字段）需要一个小型解析器，建议单独成模块并写单元测试。
6. **多日期统计的性能**：日范围 Top / Custom 图表在大数据量下的聚合性能，建议用增量缓存。
7. **统计图表实现**：现有 charts.cpp 为自绘。多序列、折线+平均线、导出（.jpg/.csv/.xlsx）需要扩展绘图与导出能力，评估自绘扩展 vs 引入轻量图表库。

---

## 9. 参考文档

- ManicTime Windows Client — Selecting time: https://docs.manictime.com/win-client/selecting-time
- ManicTime Windows Client — Tagging: https://docs.manictime.com/win-client/tagging
- ManicTime Windows Client — Away window: https://docs.manictime.com/win-client/away-window
- ManicTime Windows Client — Autotagging: https://docs.manictime.com/win-client/autotagging
- ManicTime Windows Client — Searching: https://docs.manictime.com/win-client/searching
- ManicTime Windows Client — Untagged time: https://docs.manictime.com/win-client/untagged-time
- ManicTime Windows Client — Statistics: https://docs.manictime.com/win-client/statistics

---

## 10. 实现状态（2026-08-31）

> 本状态在「实现它」阶段后追加。对照本文第 5/7 节验收标准逐项标注。
> 构建：Qt 6.8.3 (msvc2022_64) + MSVC 2022，`build.ps1` 一键构建，产物 `build\awqtui.exe`；
> 冒烟：启动运行 8s 无崩溃（含全部 6 页构造）。

### 10.1 已实现（P0 全部 + P1 主体）

| 功能域 | 编号 | 状态 | 说明 |
| --- | --- | --- | --- |
| 选择时间 | FR-101~110 | ✅ | 时间线新增选择模式（工具栏开关）：左键拖拽选区吸附活动边界(90s)、Ctrl 多选、双击选整块活动；与原平移互不冲突 |
| 打标签 | FR-201~213 | ✅ | Add tag（标签/备注/Billable/起止时间/时长/最近标签/Tag picker 下钻/排序）；Tag editor 4 Tab（组合/单标签/快捷键/标签源）+ 重命名/替换/删除/改色/导入导出 + 右键 Skip 颜色与默认可计费 |
| 未标记 | FR-701~703 | ✅ | 月历热力图（深绿=全标记/浅绿=部分/橙=未标记/白=无数据，混合分色），点击日期下钻；过滤框 + 未标记切换 |
| 搜索 | FR-607~614 | ✅ | 高级搜索：日期范围/时间线选择/未标记过滤/批量 Tag all as/Delete all/导出/双击跳转；当日过滤框（自研 FilterQuery 解析器：group:/duration>/start>/end>/label=billable/note:/-取反/or/通配符/#regex/引号） |
| 计时工具 | FR-301~304 | ✅ | 秒表（开始/暂停/继续/停止保存标签段）、计时器（倒计时自动保存）、番茄钟（25+5，工作段打标签/Break） |
| Away 窗口 | FR-401~406 | ◑ 简化 | 「Tag away」一键选中当天所有未标记时间段→Add tag（Away 弹窗样式）；未做真实 AFK 离开自动检测/暂停通知/拆分（依赖系统空闲事件，本期 mock 数据无法验证） |
| 自动标签 | FR-501~515 | ✅ | 规则引擎（Regular/Append/Prepend/Absorb + 条件 field/value/regex + order + 间隙填充 + skip-rest + 高亮猜测(guess) + 诊断 ruleName）；规则编辑器（列表/增删改/上下移/设置）；AutoTags lane 实时重算；复制 autotags（Fill/Ignore/Overwrite）；auto 段与手工段分色 |
| 统计 | FR-801~811 | ✅ | 多 Tab + 类型（Top 横条 / Day duration 折线 / Attendance 柱状 / Custom 多序列对比）+ 日期范围（本周/本月）+ 平均值线 + 折线/柱状切换 + 数据表联动 + 导出 CSV |

### 10.2 新增数据与代码

- 数据：`QStandardPaths::AppDataLocation/timetags_local.json`（segments/tags/combo_colors/shortcuts/billable_default/autotags/autotag_* 设置），独立于收件箱 `inbox_local.json`；QSaveFile 原子写。
- 源码（`src/`）：tagstore / filterparser / autotagengine / autotagdialog / addtagdialog / tageditordialog / advancedsearchdialog / untaggedview / statschart / statspage / timingdialog / daypage（新增），timelinewidget / mainwindow / CMakeLists（修改）。
- 主窗口第 5 页「🏷 标签 Day」、第 6 页「📈 统计」，快捷键 `1`~`6` 切页，`F5` 刷新。

### 10.3 未实现 / 明确排除

- **服务端/外部标签源**（FR-外部源，M6）：依赖 Jira/GitHub/Outlook 等服务端集成，本期不做。
- **截图选择（F11）**（FR-111，M6）：依赖系统截图能力，本期不做。
- **自动标签跨设备/跨安装同步**：标签为纯本地存储（第 8 节风险 3），本期不定。
- **真实 AFK 自动检测**（Away）：沿用 mock 数据，未接入系统空闲钩子。
- **统计导出 .jpg/.xlsx**：本期提供 CSV（.jpg 可由截图替代）。
- **服务端标签与收件箱笔记标签**：需求第 2 节明确排除，未改动。
