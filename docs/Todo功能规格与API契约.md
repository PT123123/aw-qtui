# aw-qtui · 任务（Todo）功能规格与 API 契约

> 目标读者：需要在**其他平台 / 技术栈**复刻同一套 Todo 功能的工程或 Agent。
> 本文按「数据模型 → 数据源契约 → REST API → UI 行为 → 边界规则 → 验收清单」组织，
> 所有行为描述均以本仓库现有实现为事实来源，关键处标注源文件。
>
> 事实来源：`src/todomodels.h`、`src/todostore.h/.cpp`、`src/apiclient.h/.cpp`、
> `src/todopage.h/.cpp`、`src/appsettings.h`、`src/mainwindow.cpp`、
> `vendor/aw-server-rust/aw-inbox-rust/src/{lib.rs,models.rs,db.rs}`。

---

## 0. 一句话概览

TickTick / Super Productivity 风格的任务模块：**三栏布局**（侧栏导航 + 任务列表 + 详情面板），
数据访问收敛在一个抽象层 `TodoSource` 之后，因此**本地实现与 REST 实现可互换**，页面代码零改动。

```
TodoPage  ──只依赖──▶  TodoSource（抽象接口 + dataChanged 信号）
                          ├── TodoStore     本地 mock：内存 + todo_local.json
                          └── TodoApiStore  REST：/inbox/todos（Rust 服务端 todo.db）
```

当前默认使用 `TodoApiStore`（`mainwindow.cpp:96`），本地 `TodoStore` 仍作为离线/自测基线保留。

---

## 1. 数据模型

### 1.1 TodoTask（任务）

| 字段 | 类型 | JSON 键 | 约束 / 语义 |
| --- | --- | --- | --- |
| id | int64 | `id` | 正整数，由数据源统一分配（本地为自增 `next_id`，服务端为 SQLite AUTOINCREMENT） |
| title | string | `title` | 必填；快速添加时 trim 后非空才提交 |
| notes | string | `notes` | 备注/描述；服务端字段名为 `content` |
| listId | int64 | `list_id` | `0` = 收集箱（无清单归属）；服务端无此字段，见 §3.5 映射规则 |
| tags | string[] | `tags` | 自由标签；详情页以英文或中文逗号分隔输入 |
| priority | int | `priority` | `0` 无 / `1` 低 / `2` 中 / `3` 高（对齐 TickTick） |
| dueDate | string | `due_date` | ISO `yyyy-MM-dd`；**空串 = 无期限**；服务端为 RFC3339，客户端取前 10 位 |
| completed | bool | `completed` | |
| completedAt | string | `completed_at` | ISO 时间；未完成时必须为空串 |
| recurrence | string | `recurrence` | `""` / `daily` / `weekdays` / `weekly` / `monthly`；服务端暂不支持 |
| createdAt | string | `created_at` | ISO 时间，**由数据源写入，更新时不可被覆盖** |
| updatedAt | string | `updated_at` | ISO 时间，每次写操作由数据源刷新 |
| sortOrder | int | `sort_order` | 同优先级/同期限时的稳定次序；新建任务取当前 max+1 |
| subtasks | TodoSubtask[] | `subtasks` | 子任务列表；服务端暂不支持 |

时间格式：
- 本地 `TodoStore`：`QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)` → `2026-09-02T20:07:58.123Z`
- 服务端：Rust `to_rfc3339()` → 同等格式

辅助方法：`hasDue()` = `dueDate` 非空；`openSubtaskCount()` = 未完成的子任务数。

### 1.2 TodoSubtask（子任务）

| 字段 | 类型 | JSON 键 |
| --- | --- | --- |
| id | int64 | `id` |
| title | string | `title` |
| completed | bool | `completed` |

子任务 id 与任务 id 共用同一个 id 分配器（`nextId()`），全局唯一。

### 1.3 TodoList（清单）

| 字段 | 类型 | JSON 键 | 语义 |
| --- | --- | --- | --- |
| id | int64 | `id` | 正整数；**收集箱是虚拟清单，不在 lists 列表内，以 id=0 表示** |
| name | string | `name` | |
| color | string | `color` | hex（如 `#4c8bf5`）；**空串 = 按名称派生**（`colorForString(name)`） |
| sortOrder | int | `sort_order` | |

### 1.4 枚举与展示文案

优先级（图标 / 颜色）：

| 值 | 含义 | 行内图标 | 颜色语义 |
| --- | --- | --- | --- |
| 0 | 无 | （不显示） | — |
| 1 | 低 | `▼` | ok（绿） |
| 2 | 中 | `◆` | accent（蓝） |
| 3 | 高 | `▲` | danger（红） |

重复规则 ↔ 文案（`recurrenceLabel` / `recurrenceRule`）：

| 规则值 | 中文文案 |
| --- | --- |
| `""` | 不重复 |
| `daily` | 每天 |
| `weekdays` | 每个工作日 |
| `weekly` | 每周 |
| `monthly` | 每月 |

### 1.5 完整 JSON 示例（本地持久化 / 模型序列化通用）

```json
{
  "id": 12,
  "title": "周报：爬虫项目进度同步",
  "notes": "每周一同步上周采集与反爬进展。",
  "list_id": 4,
  "tags": ["工作"],
  "priority": 1,
  "due_date": "2026-09-03",
  "completed": false,
  "completed_at": "",
  "recurrence": "weekly",
  "created_at": "2026-09-02T20:07:58.123Z",
  "updated_at": "2026-09-02T20:07:58.123Z",
  "sort_order": 7,
  "subtasks": [{ "id": 13, "title": "整理采集量数据", "completed": false }]
}
```

---

## 2. 数据源契约 `TodoSource`（核心，跨平台必须等价实现）

页面只认这套接口。换语言/框架时，把它实现为 **interface + 观察者事件**（等价于 Qt 的 `dataChanged` 信号）。

### 2.1 接口定义

```cpp
class TodoSource : public QObject {
public:
    virtual void load() = 0;                 // 初始加载；完成后发 dataChanged
    virtual bool ready() const = 0;          // 是否已加载完成

    // 快照查询：内部已加载，调用方在收到 dataChanged 后读取
    virtual QList<TodoList> lists() const = 0;
    virtual QList<TodoTask> tasks() const = 0;

    // 写操作：异步生效，生效后发 dataChanged
    virtual void createList(const QString &name, const QString &color) = 0;
    virtual void renameList(qint64 listId, const QString &name) = 0;
    virtual void deleteList(qint64 listId) = 0;
    virtual void createTask(const QString &title, qint64 listId, const QString &dueDate) = 0;
    virtual void updateTask(const TodoTask &task) = 0;
    virtual void setTaskCompleted(qint64 taskId, bool completed) = 0;
    virtual void deleteTask(qint64 taskId) = 0;
    virtual void addSubtask(qint64 taskId, const QString &title) = 0;
    virtual void toggleSubtask(qint64 taskId, qint64 subtaskId) = 0;
    virtual void removeSubtask(qint64 taskId, qint64 subtaskId) = 0;

signals:
    void dataChanged();                      // 数据已变化，请重读快照并重渲染
};
```

### 2.2 全局时序约定（必须遵守）

1. **拉取即快照**：`load()` 完成后发一次 `dataChanged`；页面收到信号后调用 `lists()` / `tasks()` 取快照，
   并自行缓存为 `m_lists` / `m_tasks`（`todopage.cpp:966`）。
2. **写后即广播**：每个写操作内部完成持久化/网络请求后发 `dataChanged`。
   本地实现用 `QTimer::singleShot(0, ...)` 后投递，**目的是避免在控件信号处理栈内重入销毁 sender**，
   同时天然对齐未来 HTTP 异步回包模型。
3. **UI 不做乐观更新**：页面不本地改状态，一律等 `dataChanged` 后按新快照重渲染。
4. **失败静默**：`load()` 失败仅打日志、不发信号（当前实现），页面保持上一次状态。

### 2.3 各方法的精确语义（本地 `TodoStore` 基线行为）

| 方法 | 语义 |
| --- | --- |
| `createList(name, color)` | 追加清单，`sortOrder = lists.size()`，id = 自增 |
| `renameList(id, name)` | 找不到 id 则静默返回；仅改 name |
| `deleteList(id)` | `id <= 0` 直接返回；**清单内任务不删除，改为 `listId = 0`（迁回收件箱）并刷新 updatedAt**；随后移除清单 |
| `createTask(title, listId, dueDate)` | 新任务 `sortOrder = max(sortOrder)+1`，`createdAt = updatedAt = now`，priority/tags/recurrence/subtasks 取默认值 |
| `updateTask(task)` | 按 id 整体替换；**`createdAt` 强制保留原值**，防止调用方覆盖；`updatedAt = now` |
| `setTaskCompleted(id, completed)` | 见 §2.4 状态机；重复任务完成时会**追加一条新任务** |
| `deleteTask(id)` | 硬删除（本地）/ 软删除（服务端 `deleted=1`） |
| `addSubtask(taskId, title)` | 追加子任务（新 id），刷新父任务 `updatedAt` |
| `toggleSubtask(taskId, subId)` | 取反 `completed`，刷新父任务 `updatedAt`；**不影响父任务 completed** |
| `removeSubtask(taskId, subId)` | 移除子任务，刷新父任务 `updatedAt` |

### 2.4 完成 / 取消完成 / 重复推进状态机

```
setTaskCompleted(id, true)：
  if 当前未完成：
      if recurrence 非空：
          复制当前任务 → 新 id，completed=false，completedAt=""
          dueDate = nextRecurrenceDate(recurrence, 当前 dueDate)
          所有 subtasks.completed = false
          createdAt = updatedAt = now，追加进数据集        ← 下一实例
      当前任务：completed=true，completedAt=now，updatedAt=now

setTaskCompleted(id, false)：
  if 当前已完成：completed=false，completedAt=""，updatedAt=now
  （不回滚已生成的下一实例）

其他组合（已完成再置 true / 未完成再置 false）：无操作
```

`nextRecurrenceDate(rule, basedOn)`（basedOn 为空则取今天）：

| rule | 计算 | 兜底 |
| --- | --- | --- |
| `daily` | base + 1 天 | 结果 < 今天则钳到今天 |
| `weekdays` | base + 1 天，跳过周六(6)/周日(7) | 同上 |
| `weekly` | base + 7 天 | 同上 |
| `monthly` | base + 1 个月 | 同上 |
| 其它/空 | 返回空串（不生成实例） | — |

---

## 3. REST API 契约（`/inbox/todos`）

### 3.1 传输约定

| 项 | 值 |
| --- | --- |
| Base URL | `http://127.0.0.1:5600`（常量 `kDefaultServerUrl`，`src/config.h`） |
| 请求头 | `X-Device-ID: <稳定设备ID>`、`Accept: application/json`；写请求 `Content-Type: application/json` |
| User-Agent | `aw-qtui/0.1` |
| 服务端实现 | Rocket（Rust），挂载在 `/inbox`，数据落在独立 SQLite 文件 `todo.db` |
| 认证 | 无；`DeviceIdGuard` 只读取并登记 `X-Device-ID` |

### 3.2 端点总表

| 方法 | 路径 | 说明 | 成功响应 |
| --- | --- | --- | --- |
| GET | `/inbox/todos` | 列表（默认全部未软删） | `200` + `TodoResponse[]` |
| GET | `/inbox/todos/<id>` | 单条 | `200` + `TodoResponse`；已软删 → `404` |
| POST | `/inbox/todos` | 创建 | `201` + `Location: /inbox/todos/<id>` + `TodoResponse` |
| PUT | `/inbox/todos/<id>` | **部分更新**（所有字段可选） | `200` + `TodoResponse` |
| DELETE | `/inbox/todos/<id>` | **软删除**（`deleted=1`） | `204` 空体 |

查询参数（`GET /inbox/todos`，全部可选，Rocket `FromForm`）：

| 参数 | 类型 | 说明 |
| --- | --- | --- |
| `completed` | bool | `true` / `false`；不传 = 不过滤 |
| `limit` | int | LIMIT |
| `offset` | int | OFFSET |

服务端排序固定为：`completed ASC, priority DESC NULLS LAST, created_at DESC`，
并恒过滤 `deleted = 0`。

> 客户端现状：`ApiClient::getTodos(includeCompleted)` 只在 `false` 时拼 `?completed=false`；
> 传 `true` 时**不带任何查询参数**，等价于「返回全部（含已完成）」。`TodoApiStore::load()` 走的是这条。

### 3.3 请求 / 响应体字段

**CreateTodoPayload**（POST body）

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `title` | string | ✅ | |
| `content` | string | | 对应模型的 `notes` |
| `priority` | int | | 0..3 |
| `due_date` | DateTime (RFC3339) | | 注意：客户端 `createTodo` **不发 due_date**（见 §3.5） |
| `tags` | string[] | | |
| `created_at` | DateTime | | 不传则服务端取 `now` |

**UpdateTodoPayload**（PUT body）：`title`、`content`、`completed`、`priority`、`due_date`、`tags`
**全部可选**，未提供则保持原值（`payload.x.or(existing.x)`）。

**TodoResponse**（响应体，所有时间均为 RFC3339 字符串）

```
id, title, content, completed, priority(可空), due_date(可空), tags[],
created_at, updated_at, completed_at(可空), version, device_id, deleted,
synced_at(可空), conflict(当前恒为 false)
```

### 3.4 服务端写入细节（`db.rs`，复刻时必须对齐）

- **全局版本号**：`sync_versions` 单行表 `global_version`，每次 create/update/delete 自增并写入该行的 `version`。
- **create**：`completed` 恒为 0，`completed_at` 为 NULL，`synced_at = created_at`，`device_id` 取自请求头。
- **update 的 completed 处理**：
  - `false → true`：`completed_at = now`
  - `true → false`：`completed_at = NULL`
  - 状态未变：保持原 `completed_at`
- **delete**：`UPDATE todos SET deleted=1, version=<新版本号>`（**软删除，行不移除**）。
- **tags 存储**：SQLite 中存 JSON 字符串（`TEXT DEFAULT '[]'`），读写时序列化/反序列化。

表结构（迁移 `migrate_todo`）：

```sql
CREATE TABLE IF NOT EXISTS todos (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL,
    content TEXT,
    completed INTEGER NOT NULL DEFAULT 0,
    priority INTEGER,
    due_date TEXT,
    tags TEXT DEFAULT '[]',
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    completed_at TEXT,
    version INTEGER NOT NULL DEFAULT 1,
    device_id TEXT,
    deleted INTEGER NOT NULL DEFAULT 0,
    synced_at TEXT
);
CREATE INDEX IF NOT EXISTS idx_todos_version   ON todos(version);
CREATE INDEX IF NOT EXISTS idx_todos_device_id ON todos(device_id);
CREATE INDEX IF NOT EXISTS idx_todos_synced_at ON todos(synced_at);
```

### 3.5 客户端 ↔ 服务端字段映射（`TodoApiStore`，务必照抄）

服务端没有「清单 / 子任务 / 重复」概念，客户端做了如下**降级映射**：

| 客户端概念 | 服务端承载方式 |
| --- | --- |
| `notes` | `content` |
| `dueDate` | `due_date` 取 `left(10)`（RFC3339 → `yyyy-MM-dd`） |
| `listId` | **用第一个 tag 模拟**：`listId = qHash(tag) % 1000000 + 1`；无 tag 时为 0（收集箱） |
| `lists()` | 派生：虚拟「收集箱(id=0)」+ 所有任务 tag 去重生成的清单（颜色 = `colorForString(tag)`） |
| `subtasks` | 不支持 → 三个方法均为空操作 |
| `recurrence` | 不支持 → `setTaskCompleted` **不生成下一实例** |
| `createList` | 空操作（清单随任务 tag 在下一次 `load()` 时自然出现） |
| `renameList` / `deleteList` | 遍历持有该 tag 的任务，逐个 `PUT /inbox/todos/<id>` 改 `tags` |
| `createTask` | `POST`（title + tags）→ 若带 dueDate，拿到新 id 后**再补一次 PUT 写 due_date** |
| `updateTask` | `PUT {title, content, priority, tags}`，`due_date` **仅在非空时带上** |
| `setTaskCompleted` | `PUT {completed: true/false}` |
| 每个写操作的收尾 | 请求完成后统一调用 `load()` **全量重新拉取**（无增量/无本地乐观更新） |

### 3.6 错误与解析约定

`ApiClient::parseReply(reply, &doc, &err)` 的判定顺序：

1. 网络层无错误：
   - 响应体为空（如 `204`）→ 视为成功，返回空 `QJsonDocument`
   - JSON 解析失败 → 失败，`err = "响应不是合法 JSON: ..."`
2. 网络层有错误 → 失败，`err = "<errorString> (HTTP <status>): <响应体前 300 字符>"`

> 注意：`parseReply` 内部会调用 `reply->deleteLater()`；`TodoApiStore` 的回调里还有一次
> `reply->deleteLater()`（Qt 允许重复调用，但新实现请只保留一处）。

### 3.7 curl 示例

```bash
BASE=http://127.0.0.1:5600
DEV=my-device-0001

# 列出全部（含已完成）
curl -H "X-Device-ID: $DEV" "$BASE/inbox/todos"

# 只要未完成
curl -H "X-Device-ID: $DEV" "$BASE/inbox/todos?completed=false"

# 创建
curl -X POST -H "Content-Type: application/json" -H "X-Device-ID: $DEV" \
  -d '{"title":"写文档","content":"脱稿","priority":3,"tags":["工作"]}' \
  "$BASE/inbox/todos"

# 更新（部分字段）
curl -X PUT -H "Content-Type: application/json" -H "X-Device-ID: $DEV" \
  -d '{"completed":true}' "$BASE/inbox/todos/12"

# 软删除
curl -X DELETE -H "X-Device-ID: $DEV" "$BASE/inbox/todos/12"
```

### 3.8 已知契约缺口（照搬时请一并复现或改进）

1. 无 `/inbox/lists`：清单只能用 tag 模拟，id 由哈希派生（不稳定于跨设备）。
2. 无子任务端点：`subtasks` 仅存在于本地实现。
3. 无 `recurrence` 字段：重复任务在 REST 模式下退化为普通任务。
4. **无法清空 `due_date`**：`updateTask` 只在非空时提交该字段，所以「取消截止日期」不会同步到服务端。
5. `create` 不支持 `due_date`：需要 POST + PUT 两次请求。
6. 无增量/长连接：任何写操作后全量 `load()`。
7. `conflict` 恒为 `false`，同步冲突模型未接入 Todo 域。

**改进提案（非现网，供新平台直接采用）**：服务端 `todos` 表增加 `list_id INTEGER`、`recurrence TEXT`；
新增 `lists` 表与 `/inbox/lists` CRUD；新增 `/inbox/todos/<id>/subtasks` 子资源；
`UpdateTodoPayload` 用 `Option<Option<T>>` 语义以支持显式清空 `due_date`。

---

## 4. 本地持久化格式（`TodoStore`，离线基线）

- 路径：`QStandardPaths::AppDataLocation/todo_local.json`
  （Windows 实测 `%APPDATA%\aw-qtui\aw-qtui\todo_local.json`；取不到则回落 `~/.aw-qtui`）
- 写入：`QSaveFile` **原子写**（临时文件 + commit），缩进 JSON。
- 首次运行或 `lists` 与 `tasks` 同时为空 → 写入种子数据（3 个清单 + 11 条任务，覆盖各视图与重复规则）。
- `next_id < 1` 视为 1。

```json
{
  "lists": [{ "id": 1, "name": "工作", "color": "#4c8bf5", "sort_order": 0 }],
  "tasks": [ { "...": "见 §1.5" } ],
  "next_id": 42
}
```

---

## 5. UI 行为规格（`TodoPage`）

### 5.1 布局

| 区域 | 内容 | 尺寸 |
| --- | --- | --- |
| 左栏 | 标题「任务」；4 个视图按钮；「清单」分组；「专注」分组；底部「＋ 新建清单」「⚙ 设置」 | 宽 `si(180)` |
| 中栏 | 浮动表面卡片（玻璃渐变 + 圆角 12 + 投影）：视图标题 / 计数、快速添加输入框、任务列表、已完成折叠按钮、全局进度 | 自适应 |
| 右栏 | 详情面板（未选中显示空态「选择任务以查看 / 编辑」） | 宽 `si(280)` |
| 主堆栈 | 第 0 页 = 任务视图；第 1..8 页 = 专注模块页 | — |

（`si()` / `sp()` 为 UI 缩放函数，其它平台按 DPR/字体缩放等价处理即可。）

### 5.2 视图定义（`visibleTasks()`）

| 视图 | 过滤条件 |
| --- | --- |
| 收集箱 `ViewInbox` | `listId == 0` |
| 今天 `ViewToday` | `hasDue() && dueDate 合法 && dueDate <= today`（**含逾期**） |
| 最近 7 天 `ViewNext7` | `hasDue() && dueDate 合法 && dueDate <= today + 6`（**含逾期**） |
| 全部 `ViewAll` | 无过滤 |
| 清单 `ViewList` | `listId == 选中清单 id` |

排序（`taskLessThan`）：

```
未完成组：优先级降序 → 有期限优先于无期限 → dueDate 升序 → sortOrder 升序
已完成组：completedAt 降序
未完成组整体排在已完成组之前
```

### 5.3 任务行 `TodoTaskRow`

- 复选框：点击 → 切换完成（**不触发选中整行**）
- 标题：已完成 → 灰色 + 删除线
- 标签行：`tags.join(" · ")`，次要色
- 优先级图标：`▲` 高 / `◆` 中 / `▼` 低，颜色随级别
- 期限徽章：文案 `今天` / `明天` / `昨天` / `M月d日`（同年）/ `yyyy年M月d日`（跨年）；
  **未完成且逾期 → 文字与边框变红**
- 清单色点：10px 圆点（`listId != 0` 时显示）
- 整行点击 → 选中：右侧加载详情 + 行高亮 `rgba(76,139,245,0.14)`
- 入场动画：视图切换/首次构建时每行 180ms 淡入（`m_animateNext`），数据刷新重建时**不触发**

### 5.4 已完成折叠 / 计数 / 空态

- 默认**隐藏**已完成；底部按钮文案 `显示已完成 (n)` ⇄ `隐藏已完成 (n)`；切换视图时重置为隐藏
- 右上计数：`N 项待办`（当前视图**未完成**数）
- 底部进度：`已完成 X / Y`（**全局**统计，非当前视图）
- 空态：`暂无任务\n在上方输入框回车即可添加`

### 5.5 快速添加

- 单行输入，placeholder `添加任务到「<视图名>」…`，回车提交，提交后清空
- 归属规则：`ViewList` → 该清单；`ViewToday` → `listId=0` 且 `dueDate=今天`；其它视图 → `listId=0`、无期限
- 空标题（trim 后）忽略

### 5.6 详情面板

| 控件 | 行为 | 提交时机 |
| --- | --- | --- |
| 标题 | 单行编辑 | `editingFinished` |
| 已完成 | 复选框 | 立即 |
| 清单 | 下拉（收集箱 + 所有清单） | 变更立即 |
| 优先级 | 无 / 低 / 中 / 高 | 变更立即 |
| 截止 | 勾选框 + 日期选择器（未勾选则 `dueDate` 为空，日期控件禁用） | 变更立即 |
| 重复 | 不重复 / 每天 / 每个工作日 / 每周 / 每月 | 变更立即 |
| 标签 | 单行，逗号分隔（`,` 与 `，` 都切分，trim、去重、去空） | `editingFinished` |
| 备注 | 多行文本 | **250ms 防抖**（`QTimer::singleShot`） |
| 子任务 | 复选框 + 标题 + `✕`；下方输入框回车添加 | 点击即时 |
| 删除任务 | 危险按钮，弹确认框 | 确认后 |

- 提交逻辑 `commitDetail()`：以 `m_tasks` 中当前选中任务为基线拷贝 → 用控件值覆盖 → `updateTask()`
- `m_loadingDetail` 标志：填充控件期间屏蔽提交，避免回环
- 数据刷新后若选中任务仍存在 → 刷新子任务列表；否则清空详情

### 5.7 清单管理

- 新建：弹输入对话框，颜色从 8 色调色板中按索引挑一个**未被占用**的颜色
  （`#4c8bf5 #3fb950 #d29922 #e5534b #a371f7 #39c5cf #f778ba #e3b341`）
- 重命名：右键菜单 → 输入对话框
- 删除：右键菜单 → 确认框「删除清单后，其中任务将移入收集箱。确定删除？」
- 清单按钮：左侧色点图标；选中态蓝色底 + 左侧 3px 色条

### 5.8 专注模块（挂在 Todo 页侧栏，但**不属于 Todo 数据域**）

8 个模块：计时 / 专注记录 / 专注记录详情 / 专注时间线 / 热力图 / 最佳专注时间 / 日历 / 倒数纪念日。
数据来自独立的 `FocusSource`（本地 `focus_local.json`），与 `TodoSource` 解耦。
唯一耦合点：**计时页从 `TodoSource::tasks()` 读任务列表作为可选项**，选中任务时用其标题填充事件名。

显示开关由 `FocusModules`（`appsettings.h:100`，8 个 bool，默认全开）持久化，通过侧栏「⚙ 设置」弹窗修改；
若当前展示的模块被关闭，自动退回任务视图。

---

## 6. 参考实现清单（照此实现即等价）

1. 定义三个数据结构 + JSON 序列化（键名严格用 snake_case，见 §1）
2. 定义 `TodoSource` 接口（11 个方法 + `dataChanged` 事件）
3. 实现本地版：内存态 + 原子持久化 + 首次种子数据 + §2.3/§2.4 的全部语义
4. 实现 REST 版：§3.5 映射表 + 写后全量 `load()`
5. 页面：三栏布局 → 5 种视图 → §5.2 排序 → 任务行 → 详情面板 → 清单管理
6. 全部渲染由 `dataChanged` 驱动，禁止本地乐观更新

---

## 7. 验收清单（等价回归测试，来源 `tools/todostore_selftest.cpp`）

- [ ] `load()` 后 `ready() == true`，首次运行生成种子数据（lists 非空、tasks ≥ 8）
- [ ] 重复任务（weekly）完成 → 原任务 `completed=true`，**新增 1 条任务**，新实例未完成
- [ ] 新实例 `dueDate` 已推进且 `>= today`
- [ ] 取消完成 → `completed=false`、`completedAt=""`
- [ ] `createTask` → 可按标题找回；`addSubtask` 后子任务数为 1
- [ ] `toggleSubtask` 生效；`removeSubtask` 后子任务清空
- [ ] `deleteTask` 后按 id 查不到
- [ ] `deleteList` → 清单内任务 `listId == 0`，清单本身被移除
- [ ] 持久化：新实例重新 `load()` 得到相同的 tasks/lists 数量
- [ ] REST 全链路：GET / POST / PUT / DELETE 分别返回 200 / 201 / 200 / 204
- [ ] 视图过滤：今天/最近 7 天包含逾期任务；清单视图严格按 `listId`
- [ ] 排序：高优先级在前；同优先级有期限在前；同期限按日期升序

---

## 8. 关键源文件索引

| 文件 | 职责 |
| --- | --- |
| `src/todomodels.h` | 数据模型 + JSON 序列化 + 优先级/重复文案 |
| `src/todostore.h/.cpp` | `TodoSource` 抽象、`TodoStore`（本地）、`TodoApiStore`（REST） |
| `src/apiclient.h/.cpp` | REST 客户端（`/inbox/*`、`/api/0/*`）、`parseReply` |
| `src/todopage.h/.cpp` | Todo 页 UI 与交互、视图/排序/详情提交 |
| `src/appsettings.h/.cpp` | `FocusModules` 功能模块开关持久化 |
| `src/mainwindow.cpp` | 组装：`TodoApiStore` + `FocusStore` → `TodoPage`（第 3 页） |
| `tools/todostore_selftest.cpp` | 本地 store 回归自测（编译方式见文件头注释） |
| `vendor/aw-server-rust/aw-inbox-rust/src/lib.rs` | 路由注册与 handler（`/inbox/todos`） |
| `vendor/aw-server-rust/aw-inbox-rust/src/models.rs` | `Todo` / `TodoResponse` / Payload 定义 |
| `vendor/aw-server-rust/aw-inbox-rust/src/db.rs` | 建表迁移 + Todo CRUD + 版本号与软删除 |
