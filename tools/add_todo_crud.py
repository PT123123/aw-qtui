path = 'vendor/aw-inbox/src/db.rs'

todo_code = '''

// ── Todo CRUD ──────────────────────────────────────────────────

fn map_row_to_todo(row: &rusqlite::Row) -> Result<Todo, Error> {
    let tags_json: String = row.get("tags")?;
    let tags: Vec<String> = serde_json::from_str(&tags_json).unwrap_or_default();
    let created_at: DateTime<Utc> = row.get("created_at")?;
    let updated_at: DateTime<Utc> = row.get("updated_at")?;
    let completed: i64 = row.get("completed")?;
    let deleted: i64 = row.get("deleted")?;

    Ok(Todo {
        id: row.get("id")?,
        title: row.get("title")?,
        content: row.get("content")?,
        completed: completed != 0,
        priority: row.get("priority")?,
        due_date: row.get("due_date")?,
        tags,
        created_at,
        updated_at,
        completed_at: row.get("completed_at")?,
        version: row.get("version")?,
        device_id: row.get("device_id")?,
        deleted: deleted != 0,
        synced_at: row.get("synced_at")?,
    })
}

pub fn create_todo_db(
    conn: &mut DbConnection,
    payload: CreateTodoPayload,
    device_id: Option<String>,
) -> Result<Todo, Error> {
    let created_at = payload.created_at.unwrap_or_else(Utc::now);
    let updated_at = created_at;
    let tags_json =
        serde_json::to_string(&payload.tags.unwrap_or_default()).map_err(map_serde_error)?;

    let tx = conn.transaction()?;
    let global_version: i64 = tx.query_row(
        "UPDATE sync_versions SET global_version = global_version + 1 RETURNING global_version",
        [],
        |row| row.get(0),
    )?;

    tx.execute(
        r#"
        INSERT INTO todos (title, content, completed, priority, due_date, tags,
                           created_at, updated_at, completed_at, version, device_id, deleted, synced_at)
        VALUES (?1, ?2, 0, ?3, ?4, ?5, ?6, ?7, NULL, ?8, ?9, 0, ?10)
        "#,
        params![
            payload.title,
            payload.content,
            payload.priority,
            payload.due_date,
            tags_json,
            created_at,
            updated_at,
            global_version,
            device_id,
            created_at,
        ],
    )?;

    let id = tx.last_insert_rowid();
    tx.commit()?;

    let parsed_tags: Vec<String> = serde_json::from_str(&tags_json).map_err(map_serde_error)?;

    Ok(Todo {
        id,
        title: payload.title,
        content: payload.content,
        completed: false,
        priority: payload.priority,
        due_date: payload.due_date,
        tags: parsed_tags,
        created_at,
        updated_at,
        completed_at: None,
        version: global_version,
        device_id,
        deleted: false,
        synced_at: Some(created_at),
    })
}

pub fn get_todos_db(
    conn: &DbConnection,
    completed: Option<bool>,
    limit: Option<i64>,
    offset: Option<i64>,
) -> Result<Vec<Todo>, Error> {
    let mut sql = String::from(
        "SELECT id, title, content, completed, priority, due_date, tags,
                created_at, updated_at, completed_at, version, device_id, deleted, synced_at
         FROM todos WHERE deleted = 0",
    );
    if let Some(c) = completed {
        sql.push_str(&format!(" AND completed = {}", if c { 1 } else { 0 }));
    }
    sql.push_str(" ORDER BY completed ASC, priority DESC NULLS LAST, created_at DESC");
    if let Some(l) = limit {
        sql.push_str(&format!(" LIMIT {}", l));
    }
    if let Some(o) = offset {
        sql.push_str(&format!(" OFFSET {}", o));
    }

    let mut stmt = conn.prepare(&sql)?;
    let todos_iter = stmt.query_map([], map_row_to_todo)?;
    let mut todos = Vec::new();
    for todo_result in todos_iter {
        todos.push(todo_result?);
    }
    Ok(todos)
}

pub fn get_todo_by_id_db(conn: &DbConnection, todo_id: i64) -> Result<Todo, Error> {
    let todo = conn.query_row(
        "SELECT id, title, content, completed, priority, due_date, tags,
                created_at, updated_at, completed_at, version, device_id, deleted, synced_at
         FROM todos WHERE id = ?1",
        params![todo_id],
        map_row_to_todo,
    )?;
    Ok(todo)
}

pub fn update_todo_db(
    conn: &mut DbConnection,
    todo_id: i64,
    payload: UpdateTodoPayload,
) -> Result<Todo, Error> {
    let existing = get_todo_by_id_db(conn, todo_id)?;
    let updated_at = Utc::now();

    let title = payload.title.unwrap_or(existing.title);
    let content = payload.content.or(existing.content);
    let priority = payload.priority.or(existing.priority);
    let due_date = payload.due_date.or(existing.due_date);
    let tags = payload.tags.unwrap_or(existing.tags);
    let tags_json = serde_json::to_string(&tags).map_err(map_serde_error)?;

    let (completed, completed_at) = if let Some(c) = payload.completed {
        if c && !existing.completed {
            (true, Some(updated_at))
        } else if !c && existing.completed {
            (false, None)
        } else {
            (c, existing.completed_at)
        }
    } else {
        (existing.completed, existing.completed_at)
    };

    let tx = conn.transaction()?;
    let global_version: i64 = tx.query_row(
        "UPDATE sync_versions SET global_version = global_version + 1 RETURNING global_version",
        [],
        |row| row.get(0),
    )?;

    tx.execute(
        r#"
        UPDATE todos SET
            title = ?1, content = ?2, completed = ?3, priority = ?4,
            due_date = ?5, tags = ?6, updated_at = ?7, completed_at = ?8, version = ?9
        WHERE id = ?10
        "#,
        params![
            title,
            content,
            if completed { 1 } else { 0 },
            priority,
            due_date,
            tags_json,
            updated_at,
            completed_at,
            global_version,
            todo_id,
        ],
    )?;
    tx.commit()?;

    Ok(Todo {
        id: todo_id,
        title,
        content,
        completed,
        priority,
        due_date,
        tags,
        created_at: existing.created_at,
        updated_at,
        completed_at,
        version: global_version,
        device_id: existing.device_id,
        deleted: false,
        synced_at: Some(updated_at),
    })
}

pub fn delete_todo_db(conn: &mut DbConnection, todo_id: i64) -> Result<(), Error> {
    let tx = conn.transaction()?;
    let global_version: i64 = tx.query_row(
        "UPDATE sync_versions SET global_version = global_version + 1 RETURNING global_version",
        [],
        |row| row.get(0),
    )?;
    tx.execute(
        "UPDATE todos SET deleted = 1, version = ?1 WHERE id = ?2",
        params![global_version, todo_id],
    )?;
    tx.commit()?;
    Ok(())
}
'''

with open(path, 'a', encoding='utf-8') as f:
    f.write(todo_code)
print('Todo CRUD added')
