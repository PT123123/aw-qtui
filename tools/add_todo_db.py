import sys

path = 'vendor/aw-inbox/src/db.rs'
with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

old = """        CREATE TABLE IF NOT EXISTS devices (
            device_id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            platform TEXT NOT NULL,
            last_seen_at TEXT NOT NULL,
            last_synced_at TEXT,
            pending_changes INTEGER NOT NULL DEFAULT 0,
            version INTEGER NOT NULL DEFAULT 0,
            is_current INTEGER NOT NULL DEFAULT 0,
            status TEXT NOT NULL DEFAULT 'OFFLINE'
        );

        "#,"""

new = """        CREATE TABLE IF NOT EXISTS devices (
            device_id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            platform TEXT NOT NULL,
            last_seen_at TEXT NOT NULL,
            last_synced_at TEXT,
            pending_changes INTEGER NOT NULL DEFAULT 0,
            version INTEGER NOT NULL DEFAULT 0,
            is_current INTEGER NOT NULL DEFAULT 0,
            status TEXT NOT NULL DEFAULT 'OFFLINE'
        );

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

        "#,"""

if old in content:
    content = content.replace(old, new)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)
    print('Table added')
else:
    print('Pattern not found')
    sys.exit(1)
