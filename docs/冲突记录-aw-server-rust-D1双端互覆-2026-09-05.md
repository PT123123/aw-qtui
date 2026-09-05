# 冲突记录 · aw-server-rust D1 云同步双端互覆（2026-09-05）

> 本文档记录 2026-09-05 凌晨两台机器对 `vendor/aw-server-rust/aw-sync-rust/src/d1_sync.rs`
> 的覆写冲突：起因、各自提交的问题、根因分析、合并修复方案，以及**为防复发而立的协作约定**。
> 改动 `d1_sync.rs`（尤其是网络层）之前，先读第 6 节。

---

## 1. TL;DR

桌面机为修「D1 连接失败」覆写重写了 `d1_sync.rs`，丢掉了 7 分钟前刚在 Android 真机验证过的
全部网络韧性逻辑，且该提交**本身编译不过**。合并修复 `c31a812` 在保留桌面侧有价值改进的
前提下，把韧性层按平台条件化加回、TLS 恢复 rustls 两端统一，并实测确认桌面 rustls 直连
D1 正常——之前桌面的连接失败与 TLS 后端无关。

## 2. 时间线

| 时间（09-05） | 提交 | 内容 |
|---|---|---|
| 03:22 | 子模块 `128c659` | **Android 韧性版**：强制 IPv4 resolver（getaddrinfo 重试×3 + 60s 进程缓存 + CF anycast 兜底池 + 2s TCP 探测）、rustls-tls-webpki-roots、全套 dbglog。真机验证通过 |
| 03:29 | 子模块 `661c478` | **桌面修复**：覆写式重写 `d1_sync.rs`（±1171 行），换 native-tls，丢掉上述全部韧性逻辑 |
| 03:31 | 主仓库 `55a79f2` | 指针更新到 `661c478` |
| 10:40 | 子模块 `c31a812` | **合并修复**（本文档第 5 节），主仓库指针随之更新 |

两台机器在 7 分钟内先后提交，互相不知道对方的存在。

## 3. `661c478` 的问题清单

1. **丢失 Android 韧性层**：强制 IPv4 resolver、anycast 兜底池、TCP 探测、dbglog 全部删除
2. **整个 commit 编译不过**：重写时误删 `manager.rs` 依赖的公共 API——
   `d1_test` / `d1_status` / `d1_sync_now` / `D1TestResult` / `D1Status`，
   以及 `D1SyncResult` 的 `ok` 字段与 `Serialize` 实现（说明提交前没跑过编译）
3. **TLS 换 native-tls**：rust-native-tls 在 Android 需要 openssl 交叉编译；
   `ef16c71` 当年正是为躲这个坑才换成 rustls 的——这一刀把 Android 构建又推回坑里
4. **死代码**：新增的 `batch()` 方法请求 `{base}/batch`——Cloudflare D1 HTTP API
   根本没有 `/batch` 端点（其自身注释都写着「D1 没有 /batch，需逐条执行 SQL」），幸未调用
5. **同时也有真改进（已在合并中保留）**：
   - `/raw` 端点拉取（行列格式，效率高于逐行 JSON）
   - `init_schema` 补齐 `note_relations` / `sync_state` 表与索引（`128c659` 的 D1 同步不同步 relations）
   - `sync_state` 持久化 last_sync

## 4. 根因分析

`661c478` 要修的「桌面 D1 连接失败」，真凶是 `128c659` 的**强制 IPv4 + 直连 IP TCP 探测**
在部分桌面网络环境下被防火墙/代理策略拦截——不是 rustls，也不是端点。

证据：合并修复后在本机（Windows，rustls）实测 `POST /api/0/sync/d1/test`
返回 `{"ok":true,"message":"D1 连接成功"}`。native-tls 那次切换是不必要的。

## 5. `c31a812` 修复方案

在 `661c478` 之上合并（不回退指针、不覆盖任何一端）：

- **韧性层平台限定**：
  - 全平台：getaddrinfo 重试×3 + 60s 进程级 DNS 缓存（请求失败时清缓存重新选路）
  - 仅 Android（`cfg(target_os = "android")`）：CF anycast IPv4 兜底池、TCP 探测选路、
    强制本地 IPv4 绑定（`local_address`）。桌面走系统默认解析路径，规避直连 IP 探测
- **TLS 恢复 `rustls-tls-webpki-roots` 两端统一**（Android 免 openssl 交叉编译）
- **补回误删公共 API**：`d1_test` / `d1_status` / `d1_sync_now` / `D1TestResult` /
  `D1Status`，`D1SyncResult` 恢复 `ok` 字段与 `Serialize`（修复编译）
- **删除死代码 `batch()`**；恢复全部 dbglog 诊断日志

验证记录：

- `cargo check --workspace`（Windows host）通过；剩余 warning 均为既有问题
- Android cfg 分支：临时对调 cfg 条件 + `RUSTFLAGS '--cfg TMP_ANDROID'` 类型检查通过
  （NDK 交叉编译需在 Android 侧跑 `compile-android.sh`）
- 起服务实测 `POST /api/0/sync/d1/test` → `{"ok":true}`

## 6. 协作约定（防复发）

1. **改 `d1_sync.rs` 网络层前先看本文档第 5 节**：resolver 分平台行为以
   `cfg(target_os = "android")` 为准——不要为了自己那端「修好」而删除对端的行为
2. **动手前先 fetch**：多端协作改子模块，先
   `git -C vendor/aw-server-rust fetch && git log origin/feature/inbox -1`
   确认远端没有别人刚推的新提交；有就在其基础上改，不要覆写
3. **提交前必须编译**：`cargo check -p aw-sync-rust`（`661c478` 的教训：没编译就推，
   编译错误直接污染所有下游）
4. **本机验证 cfg(android) 分支**的技巧：临时把 `cfg(target_os = "android")` 对调为
   自定义 cfg（如 `cfg(TMP_ANDROID)`）+ `RUSTFLAGS '--cfg TMP_ANDROID' cargo check`，
   验完还原——Windows host 上默认编译不到 android 分支
5. **一次提交只解决一端的问题时，说明清楚影响面**，commit message 里写明
   「动了哪些共享路径、对另一端的影响」

## 7. 受影响文件索引

- `vendor/aw-server-rust/aw-sync-rust/src/d1_sync.rs` —— 冲突主战场
- `vendor/aw-server-rust/aw-sync-rust/Cargo.toml` —— TLS feature（rustls ↔ native-tls）
- `vendor/aw-server-rust/aw-sync-rust/src/manager.rs` —— 依赖被误删 API 的调用方
- `src/d1syncpage.cpp` —— Qt 端消费同步结果（`.value()` 容错解析，未受影响）
