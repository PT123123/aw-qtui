# Android D1 同步恢复问题 — 核对清单

## 问题描述

重装 App 后首次 D1 同步，本地数据拉取为 0。

**根因**：本地 SQLite 被重建（空表），但 D1 云端 `sync_state` 仍保存着旧设备的 checkpoint。增量拉取按 `WHERE updated_at > '{checkpoint}'` 查询，旧 checkpoint 之后无新变更 → 拉取 0 条。

## 需要实现的两层防御

### 层 1：自动检测（建议在 `sync_d1()` pull 阶段前加）

在 pull 操作前，判断本地 DB 是否"看起来是新的但 D1 checkpoint 是旧的"：

```
条件 A：本地 inbox 表 COUNT = 0 → 跳过 checkpoint，全量拉取
条件 B：本地 todo 表 COUNT = 0 → 跳过 checkpoint，全量拉取
条件 C：本地最早笔记的 created_at > checkpoint → 疑似重置，全量拉取
条件 D：本地最早 todo 的 created_at > checkpoint → 疑似重置，全量拉取
```

只要任一条件成立，pull 时就不传 `last_sync`（或传 `null`/`None`），做全量拉取。

### 层 2：手动强制全量同步

提供一个独立的"强制全量同步"入口（设置页按钮 / 调试菜单）。流程：

1. 调用 D1 API 清空当前设备的 checkpoint：
   ```sql
   DELETE FROM sync_state WHERE device_id = ?
   ```
2. 然后执行一次普通同步（push + pull），此时 pull 不传 `last_sync` → 全量拉取

## Android 端具体检查点

| 检查项 | 说明 |
|--------|------|
| ☐ pull 前是否检测本地表为空 | `SELECT COUNT(*) FROM notes` / `todos` = 0 → 全量拉取 |
| ☐ pull 前是否比较 `MIN(created_at)` 与 checkpoint | `MIN(created_at) > checkpoint` → 全量拉取 |
| ☐ 是否有独立 API 清空 checkpoint | `DELETE FROM sync_state WHERE device_id = ?` |
| ☐ 是否有手动触发全量同步的入口 | 按钮 / debug action |

## 核心改动伪代码

```kotlin
// pull 前调用
fun shouldFullPull(db: SQLiteDatabase, table: String, checkpoint: String?): Boolean {
    // 条件 A/B：空表
    val count = db.rawQuery("SELECT COUNT(*) FROM $table", null).use { it.getInt(0) }
    if (count == 0) return true

    // 条件 C/D：本地最早记录晚于 checkpoint
    if (checkpoint != null) {
        val oldest = db.rawQuery(
            "SELECT MIN(created_at) FROM $table WHERE created_at IS NOT NULL AND created_at != ''",
            null
        ).use { if (it.moveToFirst()) it.getString(0) else null }
        if (oldest != null && oldest > checkpoint) return true
    }

    return false
}

// 同步入口
fun syncD1() {
    val checkpoint = if (shouldFullPull(db, "notes", savedCheckpoint)) null else savedCheckpoint
    pullNotes(checkpoint)  // null = 全量
    pushNotes()
    saveCheckpoint(now)
}
```

## 恢复已发生问题的设备

若 Android 端已有用户遇到重装后数据拉不到：

1. **临时**：通过 `wrangler d1 execute` 手动清空 `sync_state` 中该设备的行
2. **长期**：上线上述自动检测 + 手动全量同步
