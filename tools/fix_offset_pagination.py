#!/usr/bin/env python3
"""Fix GET /inbox/notes offset pagination in aw-inbox.

Changes:
1. db.rs: add offset + sort_by params to get_notes_db, add OFFSET to SQL,
   whitelist-based sort_by handling.
2. lib.rs: pass offset + sort_by from query into get_notes_db call.
"""
import sys
from pathlib import Path

ROOT = Path(r"C:\Users\ted\Desktop\aw-qtui\vendor\aw-inbox")
DB_RS = ROOT / "src" / "db.rs"
LIB_RS = ROOT / "src" / "lib.rs"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def write(path: Path, content: str) -> None:
    path.write_text(content, encoding="utf-8")


def fix_db_rs() -> None:
    src = read(DB_RS)

    # 1. Function signature: add offset + sort_by
    old_sig = (
        "pub fn get_notes_db(\n"
        "    conn: &DbConnection,\n"
        "    limit: Option<i64>,\n"
        "    tag: Option<String>,\n"
        "    created_after: Option<DateTime<Utc>>,\n"
        "    created_before: Option<DateTime<Utc>>,\n"
        "    search: Option<String>,\n"
        ") -> Result<Vec<Note>, Error> {"
    )
    new_sig = (
        "pub fn get_notes_db(\n"
        "    conn: &DbConnection,\n"
        "    limit: Option<i64>,\n"
        "    offset: Option<i64>,\n"
        "    tag: Option<String>,\n"
        "    created_after: Option<DateTime<Utc>>,\n"
        "    created_before: Option<DateTime<Utc>>,\n"
        "    search: Option<String>,\n"
        "    sort_by: Option<String>,\n"
        ") -> Result<Vec<Note>, Error> {"
    )
    if old_sig not in src:
        print("ERROR: db.rs signature not found", file=sys.stderr)
        sys.exit(1)
    src = src.replace(old_sig, new_sig, 1)

    # 2. SQL: replace fixed ORDER BY + add OFFSET
    old_sql = (
        '    query_str.push_str(" ORDER BY created_at DESC");\n'
        "\n"
        "    if let Some(l) = limit {\n"
        '        query_str.push_str(&format!(" LIMIT {}", l));\n'
        "    }"
    )
    new_sql = (
        "    // 排序：白名单字段（created_at / updated_at），默认 created_at DESC\n"
        "    let (sort_field, sort_dir) = match sort_by.as_deref() {\n"
        '        Some("updated_at") | Some("updated_at:desc") => ("updated_at", "DESC"),\n'
        '        Some("updated_at:asc") => ("updated_at", "ASC"),\n'
        '        Some("created_at:asc") => ("created_at", "ASC"),\n'
        '        _ => ("created_at", "DESC"),\n'
        "    };\n"
        '    query_str.push_str(&format!(" ORDER BY {} {}", sort_field, sort_dir));\n'
        "\n"
        "    if let Some(l) = limit {\n"
        '        query_str.push_str(&format!(" LIMIT {}", l));\n'
        "    }\n"
        "    if let Some(o) = offset {\n"
        '        query_str.push_str(&format!(" OFFSET {}", o));\n'
        "    }"
    )
    if old_sql not in src:
        print("ERROR: db.rs SQL block not found", file=sys.stderr)
        sys.exit(1)
    src = src.replace(old_sql, new_sql, 1)

    write(DB_RS, src)
    print("db.rs: OK")


def fix_lib_rs() -> None:
    src = read(LIB_RS)

    # 1. Extract offset + sort_by in get_notes
    old_extract = (
        "    // 接收查询参数\n"
        "    let limit = query.limit;\n"
        "    let tag = query.tag;\n"
        "    let search = query.search;"
    )
    new_extract = (
        "    // 接收查询参数\n"
        "    let limit = query.limit;\n"
        "    let offset = query.offset;\n"
        "    let tag = query.tag;\n"
        "    let search = query.search;\n"
        "    let sort_by = query.sort_by;"
    )
    if old_extract not in src:
        print("ERROR: lib.rs extract block not found", file=sys.stderr)
        sys.exit(1)
    src = src.replace(old_extract, new_extract, 1)

    # 2. Pass offset + sort_by into get_notes_db call
    old_call = "db::get_notes_db(&conn, limit, tag, None, None, search)"
    new_call = "db::get_notes_db(&conn, limit, offset, tag, None, None, search, sort_by)"
    if old_call not in src:
        print("ERROR: lib.rs call not found", file=sys.stderr)
        sys.exit(1)
    src = src.replace(old_call, new_call, 1)

    write(LIB_RS, src)
    print("lib.rs: OK")


if __name__ == "__main__":
    fix_db_rs()
    fix_lib_rs()
    print("Done.")
