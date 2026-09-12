# 同步逻辑实时性现状分析

> 记录时间：2026-09-06（基于 main 分支代码现状）
> 范围：Qt 客户端（aw-qtui）+ 本机 aw-server-rust 的两条同步链路（D1 云同步 / 局域网同步）

## 结论先行

当前实时性水平是**"周期轮询 + 手动触发"的准实时，没有写入即推（push-on-write）**。更关键的问题：

**桌面端 D1 周期同步线程根本没有启动**，`d1_sync_interval` 配置在桌面端实际不生效——桌面端目前是纯手动同步；只有 Android 端有后台周期同步。

## 架构链路

```
Qt 客户端 (aw-qtui)
   │  QProcess::startDetached 拉起本机服务端   （src/awserver.cpp:64）
   ▼
aw-server-rust（同步由服务端承担）
   ├── D1 云同步：服务端 ↔ Cloudflare D1，端点 /api/0/sync/d1/*   （src/apiclient.h:80）
   └── 局域网同步：设备配对 + snapshot 推拉，端点 /api/0/sync/devices/<id>/sync
```

- D1 同步覆盖的数据：Inbox 笔记 + Todo（不含 ActivityWatch 事件）。
- 客户端只是 REST 封装（`d1SyncNow` / `d1FullSync` / `d1Status` / `d1Test`），所有同步逻辑在服务端。

## D1 同步的实际机制

### 数据面：增量 checkpoint + LWW

- 双向 push + pull，靠 checkpoint（`sync_state.last_sync_at`）做增量：
  - 推送只处理 `updated_at > last_sync` 的记录（d1_sync.rs:508）；
  - 拉取用 SQL `WHERE updated_at > '{checkpoint}'`（d1_sync.rs:599）。
- 冲突解决是 LWW（时间戳新者胜）：upsert 时
  `WHERE excluded.updated_at > notes.updated_at OR (= AND device_id > ...)`（d1_sync.rs:521），
  同时间戳用 device_id 决胜。
- 已知坑（重装后拉 0 条）见 `docs/android-d1-sync-checklist.md`，已有"强制全量同步"兜底。

### 触发面：三个口子，各有缺口

| 触发方式 | 现状 | 缺口 |
|---------|------|------|
| 周期线程 | `spawn_d1_sync`（manager.rs:557）：`loop { 读配置 → 同步 → sleep(interval) }`，每轮重读配置，改间隔能生效。UI 间隔最小 30 秒（默认 300 秒），服务端 clamp 到最小 10 秒（manager.rs:575） | **只在 Android 启动路径被调用**（android/mod.rs:505）；桌面端 main.rs:203-215 只 spawn 了 `spawn_discovery`，没调 `spawn_d1_sync` |
| 手动触发 | D1SyncPage 的"立即同步"/"强制全量同步" → `POST /d1/sync`、`/d1/full_sync`，在 `spawn_blocking` 里执行（endpoints.rs:38） | 依赖用户点按钮 |
| 写入触发 | 无。服务端 inbox/todo 写端点不联动触发 D1 同步，也没有 SSE/WebSocket/长轮询等变更通知 | 一次编辑最早要等下一个整周期才上云 |

## 其他实时性相关短板

1. **UI 感知滞后于同步**：同步完成只更新 D1SyncPage 自己的日志和状态标签，不发任何信号；MainWindow 没有接线 `d1SyncPage()`。Inbox/Todo 页面在线时**不轮询**远端数据（src/inboxpage.cpp:61 的 10 秒定时器只在离线时跑，用于重连），刷新全是事件驱动（自己的增删改、初始加载）。服务端 pull 到别的设备的数据后，客户端界面要等用户下一次操作才能看到。
2. **局域网同步同样是纯手动**：manager 里只有 D1 循环、在线探测循环（只更新 is_online，不同步数据）和 UDP 发现循环，没有周期数据同步；数据同步只有 UI 按钮触发的 `POST /devices/<id>/sync`。
3. **D1 页面状态刷新偏被动**：`refreshStatus` 只在构造、保存后调用，不周期显示 last_sync。

## 实时性量化

| 场景 | 端到端延迟 |
|------|-----------|
| Android 端，后台周期 | 最差 ≈ `d1_sync_interval`（可配 30s 起，服务端允许到 10s）+ 一次 D1 API 往返；且 UI 不一定刷新 |
| 桌面端 | **无限期**——除非手动点"立即同步"（周期线程未启动） |
| 编辑后到上云 | 下一个整周期（没有 push-on-write） |

## 改进方向（按性价比排序）

1. **桌面端补上周期线程**：main.rs 的同步挂载块里加 `let _ = g.spawn_d1_sync();`（与 android/mod.rs:505 对齐），让 `d1_sync_interval` 在桌面端生效。改动最小，收益最大。
2. **写入即推（push-on-write）**：服务端在 inbox/todo 写端点后触发一次**防抖**的 D1 同步（例如 2~5 秒 debounce，避免连续编辑打爆 D1 API）。
3. **同步完成后刷新 UI**：D1SyncPage 同步成功后发信号（或在 MainWindow 里接线），让 Inbox/Todo 页面 `refreshAll`；或客户端低频轮询 `/d1/status` 的 last_sync 变化来触发刷新。

---

# 2026-09-12 更新：局域网同步「不自动传」的实测与修复

> 背景：手机离网期间记的内容，回到同一局域网后电脑没自动拿到，必须进「局域网同步」页点一次同步。
> 实测方式：读 `/sync/config`、`/sync/devices`、`/sync/status` + 抓 `/sync/debuglog` 实时环形日志。

## 先纠正本文档前面的一处过时结论

- 「局域网同步同样是纯手动」**不成立**：`spawn_auto_sync` 早就在桌面端 `main.rs` 里启动了
  （与 `spawn_d1_sync` / `spawn_probe` 同一挂载块），实测每 10 秒一轮 `[pull]/[push]` 稳定运行。
- 「桌面端 D1 周期同步线程根本没有启动」也已过时：`main.rs` 已调用 `spawn_d1_sync()`。
- **不要用 sync_log 判断自动同步有没有跑**：`sync_to_unlocked(mgr, id, log_noop)` 中自动轮传
  `false`，无变更时刻意不写日志（静默模式防刷屏）；只有手动同步传 `true` 才必定落日志。

## 真正的四个断点

1. **静默跳过**：自动轮的目标过滤是 `(force || d.is_online)`，`is_online=0` 时**连日志都不写**。
   探测线程一旦没把标志刷回 1，就每轮静默跳过，用户毫无线索，只能手动点。
2. **发现广播绑在 UI 页面上**：`discovery/start` 由「进入同步页」触发、`discovery/stop` 由「离开」触发，
   页面外不广播也不监听 → 对端换 IP / 换 device id / 上下线全部失明，只剩 HTTP 探活硬猜。
   实测 `last_seen_at` 冻结在 9-08（4 天没收到过广播），而 `discovery_running=false`。
3. **本机自报端口的 0 值**：`self_device_info()` 直接取 `cfg.listen_port`，而库里是 0
   → 广播/配对传出去的是 `http://<ip>:0/...`，对端拿到的端点是坏的 → 反向同步彻底不通。
   实测一天内**零条 inbound 同步**，全是本机主动拉。
4. **UI 不感知远端落地**：服务端 pull 到数据后不发信号，Inbox/Todo 事件驱动不轮询，
   「数据到了」和「没同步」在界面上长得一模一样。

## 本次修复

| 位置 | 改动 |
|------|------|
| `aw-sync-rust/src/storage.rs` | `get_config` 把 `listen_port=0` / `udp_port=0` 归一化为默认值（5600 / 46000），修断点 3 |
| `aw-sync-rust/src/models.rs` | 新增 `DEFAULT_HTTP_PORT` 常量 |
| `aw-sync-rust/src/manager.rs` | `stop_discovery()` 在桌面端为 no-op（`discovery_persistent()`），发现常驻，修断点 2 |
| `aw-sync-rust/src/manager.rs` | `spawn_auto_sync` 逐设备跟踪在线翻转：0→1 **立即同步一轮**并记日志；离线设备每 30s 补探一次（自愈）；在线/离线翻转各记一条日志，修断点 1 |
| `aw-sync-rust/src/manager.rs` | 新增 `data_revision()`：快照合并有新应用/归档时 +1 |
| `aw-sync-rust/src/endpoints.rs` | 新增 `GET /api/0/sync/revision`；`config_save` 不再 `reset_discovery_started_for_testing()`（那会让下次 start 重复 spawn listener 抢 UDP 端口，bind 必然 10048 失败 → 发现监听哑掉），改为在桌面端恢复广播 |
| `aw-sync-rust/src/discovery.rs` | `listener_loop` 绑定失败改为 5 秒重试，不再直接退出线程 |
| `src/apiclient.{h,cpp}` | 新增 `getSyncRevision()` |
| `src/mainwindow.{h,cpp}` | 15 秒轮询 revision，变化即静默刷新 Inbox/Todo（活动页仅当前显示时刷），修断点 4 |
| `src/awserver.cpp` | 防火墙规则改为覆盖 `profile=private,public`；添加前先删同名旧规则；`firewallRuleExists()` 同时校验配置文件覆盖（只看「配置文件」行，其余字段恒为「任何」会误判） |

## 遗留（需人工处理，未自动改数据）

- 配对表里的设备 id 与对端当前自报 id 不一致：本机记录 `Android-b44fce(b44fce7d…)`，
  而对端快照 `source_device` 是 `Android-7f19a8(7f19a89b…)` —— 对端重装/重置过。
  同步靠 IP 仍能工作，但 LWW 的 device_id 决胜会错位（历史上反复出现「13 条进入回收站」）。
  发现恢复后会看到该手机以**未配对**身份出现，重新配对一次并删掉旧记录即可。

