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


---

# 2026-11-08 更新：档位取消，实时 write-kick（LocalSend 式，quire 同款）

> 背景：quire 已把同步改成「内容写入落定即推」（write-kick，WRITE_KICK_SECS=1s + 60s 对账兜底），
> 不再按用户选的周期档位轮询。本次把 aw 对齐到同一模型。

## 服务端（aw-sync-rust / aw-server）

| 位置 | 改动 |
|------|------|
| `aw-sync-rust/src/manager.rs` | 新增 `kick_content()`（AtomicU64 时间戳 + Condvar）：HTTP 层内容写入成功时打点并唤醒自动同步线程 |
| `aw-sync-rust/src/manager.rs` | `spawn_auto_sync` 重写：不再按 `sync_interval` 周期轮询。触发点变为 ①内容 kick 落定 2s（去抖合并连续保存）→ 实时推一轮；②开关 false→true；③设备在线 0→1；④每 60s 兜底对账（接住 activity 流与漏推）。等待用 Condvar（按落定/对账最近到期计算，封顶 5s），新 kick 立即唤醒 |
| `aw-sync-rust/src/models.rs` | `sync_interval` 标记废弃：字段仅为旧 sync.db 反序列化兼容保留，`= 0`（仅手动）仍作为自动同步总开关 |
| `aw-server/src/endpoints/mod.rs`（`build_rocket`） | 响应 fairing：`/inbox/**` 的非 GET 请求 2xx 即 `kick_content()` |
| ~~`aw-server/src/main.rs`~~ | ~~原挂载点~~ 已删除：只挂桌面 main 会漏掉 Android（见下方「2026-11-08 实测修正」） |

**两条纪律**（quire 踩过的坑，原样遵守）：
- activity 事件流**不** kick —— 心跳量大且连续，秒推等于整库快照对刷；由 60s 对账轮带走。
- 远端合并落地（apply_snapshot / /sync/push）**不** kick —— 合并数据算「本机新写入」会让两端互推成回声死循环（quire `record_quiet` 的教训）。

## 客户端（aw-qtui）

| 位置 | 改动 |
|------|------|
| `src/syncpage.ui` | 「自动同步频率」四档下拉（实时10s/标准1min/省电5min/仅手动）移除，换成一行说明文字 |
| `src/syncpage.{h,cpp}` | `m_cmbSyncInterval` 删除；`m_syncInterval` 只透传服务端原值（保住历史「仅手动」=0 设置不被重置） |
| `src/models.h` | `fromJson` 的 `syncInterval==0 → 10` 强转移除（会破坏「仅手动」透传） |
| `src/syncservice.cpp` | 客户端侧 4s 去抖推送**保留未动**：与服务器 kick 冗余但幂等无害 |

## 端到端延迟（改后）

| 场景 | 延迟 |
|------|------|
| Inbox/Todo 写入 → 对端 | 去抖 2s 落定 + 传输（原：档位周期，平和档 5 分钟） |
| activity 事件 → 对端 | ≤ 60s（原：档位周期，狂暴档 10s——为避免整库快照高频对刷，接受变长） |
| 对端回到局域网 | 在线翻转立即同步（不变） |

## 2026-11-08 实测修正：fairing 挂错了层

双机实测准备时发现，第一版把 fairing 挂在 `aw-server/src/main.rs`（桌面二进制），
而 **Android 根本不走 main.rs**：`android/mod.rs` 直接 `endpoints::build_rocket()` 起服务。
后果是 Android 上 write-kick 从未触发，自动同步只剩「开启翻转 / 在线翻转 / 60s 对账」，
内容写入最长等 60s——比它还慢于旧版的 10s 周期。

- 修复：fairing 移到 `endpoints/mod.rs::build_rocket()`（桌面与 Android 共用入口），`main.rs` 删除重复挂载。
- 自证：Redmi 上写笔记后 2.1s，`/api/0/sync/debuglog` 出现 `[auto] 本机内容有写入且已落定，实时推送一轮`。
- 教训：**凡是「覆盖所有客户端」的钩子，必须挂在所有客户端共用的构建函数上，不能挂在某一个 main 上。**

## 双机实测（2026-11-08）

两台 Android（Redmi release + Lenovo 平板 debug，0.13.38 + 本改动）跑完 6 个用例，
完整方案、原始数据与结果见 `aw-android-native/docs/lan-sync-dual-device-test-2026-11-08.md`：

| 场景 | 实测 |
|------|------|
| 在线写入 → 对端可见（笔记/todo，双向） | p50 2.30–2.43s、p95 ≤ 2.45s（2s 落定去抖 + 传输） |
| 对端进程销毁期间写 7 项，重启追平 | 全部追平、0 重复；从 `am start` 算 1.44s（含 ~0.8s 启动） |
| 同步中途强杀（2 轮） | 重启后 0.98–1.05s 收敛，0 重复 |
| 双端离线各写，重启合并 | 2.18s 两端一致 |
| 同笔记并发改（LWW） | 收敛到后写版本，旧版进 trash；两端各 1 条 |
| 冷启动到对端数据可见 | 1.68–2.52s（启动 ~0.78s，服务显示后 6–12ms 可用，其余是重发现/全量快照） |

实测还暴露一个与 write-kick 无关的独立问题：网络切换导致 IP 变化后，重新配对会沿用设备记录里的旧 IP
并返回 500；自动同步链路有重发现可自愈，配对链路没有。详见测试文档「发现 1」。

---

# 2026-11-08 真机测试报告：write-kick 实时同步（commit 7f95165 / 4f00386 之后）

> 环境：桌面端部署新二进制（`C:\workshop\aw-qtui-0.1.41\server\aw-server.exe`，旧 exe 备份为
> `aw-server.exe.bak-0.1.41`），数据目录不动（配对与业务库原样）；对端为已重装的手机
> （新设备 id `d023f393`，192.168.88.148:5600，桌面侧登记为已配对·明文）。全程 HTTP API 实测。

## 结果一览（7/7 通过）

| # | 场景 | 结果 | 延迟 |
|---|------|------|------|
| T1 | 桌面插入 → 手机 | ✅ kick 日志「本机内容有写入且已落定，实时推送一轮」+ 手机查到该笔记 | 写入→kick 2s；端到端 11s（大头是 12MB 拉取 + 15MB 推送的全量快照传输） |
| T2 | 桌面修改 → 手机 | ✅ 手机显示新内容（version 145） | kick 2-4s 触发；端到端 ~19s（15MB 推送占 ~7s） |
| T3 | 桌面删除 → 手机 | ✅ 墓碑传播，手机 20s 内查不到 | ~20s |
| T4 | 手机插入 → 桌面 | ✅ 60s 兜底对账轮拉到（手机装的是旧构建、无 kick） | ≤53s；手机升级到含 kick 的构建后会缩短到秒级 |
| T5 | 写后 10s 杀 server（推送中断） | ✅ 停机期间手机查不到；qtui 自动重启 server（+5s），启动轮「立即同步一轮」补推成功 | 重启后 ~25s 手机收到 |
| T6 | server 停机期间手机写入 | ✅ server 重启后启动轮拉到 | 重启后 ~20s |
| T7 | 空清单闸门（quire 事故场景实测） | ✅ 对 `/sync/apply` 发 `{todos:[],lists:[]}` 快照：`applied=0`，错误信息「远端清单为空而本地有 1 条，拒绝整表替换」，本地清单完好 | 即时 |

## 观察与遗留

1. **全量快照是延迟大头**：每轮拉+推 ~27MB（activity 12MB + 推送 15MB 明文），kick 本身只花 2-4s。
   要把端到端压到 LocalSend 水平，下一步应做增量快照（按 last_sync 游标）——架构上 `?from=<device_id>`
   参数已预留，尚未实现增量语义。
2. **kick 唤醒对齐**：Condvar 唤醒 + 5s 封顶等待，实测 kick 触发点稳定在写入后 2-4s。
3. **第二台手机（192.168.88.155）全程 HTTP 无响应**（息屏/休眠？），本轮未测到双端互写；
   其旧配对记录（Android-572adf，encrypted）IP 已过期。
4. **桌面侧两条旧配对记录**（Android-b44fce / Android-572adf）指向旧网段死 IP，每轮触发
   30s 限流的重发现窗口（无害但有日志噪音），可在同步页「一键清理过期设备」处理。
5. 手机为重装后的新设备 id，桌面侧以明文已配对登记；要上加密信封需在手机端重新走一次配对码。
6. 测试数据已全部清理（两端 search=SYNC-TEST 均为 0，闸门测试的假设备记录已删）。
   测试前业务库备份在 `%TEMP%\aw-sync-test-backup\`。
