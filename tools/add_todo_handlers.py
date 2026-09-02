path = 'vendor/aw-inbox/src/lib.rs'

handlers = '''

// ── Todo handlers ──────────────────────────────────────────────

fn todo_to_response(todo: &Todo) -> TodoResponse {
    TodoResponse {
        id: todo.id,
        title: todo.title.clone(),
        content: todo.content.clone(),
        completed: todo.completed,
        priority: todo.priority,
        due_date: todo.due_date.map(|dt| dt.to_rfc3339()),
        tags: todo.tags.clone(),
        created_at: todo.created_at.to_rfc3339(),
        updated_at: todo.updated_at.to_rfc3339(),
        completed_at: todo.completed_at.map(|dt| dt.to_rfc3339()),
        version: todo.version,
        device_id: todo.device_id.clone(),
        deleted: todo.deleted,
        synced_at: todo.synced_at.map(|dt| dt.to_rfc3339()),
        conflict: false,
    }
}

#[derive(FromForm)]
struct TodoQuery {
    completed: Option<bool>,
    limit: Option<i64>,
    offset: Option<i64>,
}

#[get("/todos?<query..>")]
async fn get_todos(
    db_state: &State<SharedDb>,
    query: TodoQuery,
) -> Result<Json<Vec<TodoResponse>>, Status> {
    let db = db_state.lock().map_err(|_| Status::InternalServerError)?;
    let todos = task::spawn_blocking(move || {
        db::get_todos_db(&db.conn, query.completed, query.limit, query.offset)
    })
    .await
    .map_err(handle_spawn_error)?
    .map_err(handle_db_error)?;
    Ok(Json(todos.iter().map(todo_to_response).collect()))
}

#[get("/todos/<todo_id>")]
async fn get_todo(
    db_state: &State<SharedDb>,
    todo_id: i64,
) -> Result<Json<TodoResponse>, Status> {
    let db = db_state.lock().map_err(|_| Status::InternalServerError)?;
    let todo = task::spawn_blocking(move || db::get_todo_by_id_db(&db.conn, todo_id))
        .await
        .map_err(handle_spawn_error)?
        .map_err(handle_db_error)?;
    if todo.deleted {
        return Err(Status::NotFound);
    }
    Ok(Json(todo_to_response(&todo)))
}

#[post("/todos", format = "json", data = "<payload>")]
async fn create_todo(
    db_state: &State<SharedDb>,
    device: DeviceIdGuard,
    payload: Json<CreateTodoPayload>,
) -> Result<Created<Json<TodoResponse>>, Status> {
    let device_id = device.0;
    let mut db = db_state.lock().map_err(|_| Status::InternalServerError)?;
    let todo = task::spawn_blocking(move || {
        db::create_todo_db(&mut db.conn, payload.into_inner(), device_id)
    })
    .await
    .map_err(handle_spawn_error)?
    .map_err(handle_db_error)?;
    info!("Created todo #{}: {}", todo.id, todo.title);
    Ok(Created::new(format!("/inbox/todos/{}", todo.id)).body(Json(todo_to_response(&todo))))
}

#[put("/todos/<todo_id>", format = "json", data = "<payload>")]
async fn update_todo(
    db_state: &State<SharedDb>,
    todo_id: i64,
    payload: Json<UpdateTodoPayload>,
) -> Result<Json<TodoResponse>, Status> {
    let mut db = db_state.lock().map_err(|_| Status::InternalServerError)?;
    let todo = task::spawn_blocking(move || {
        db::update_todo_db(&mut db.conn, todo_id, payload.into_inner())
    })
    .await
    .map_err(handle_spawn_error)?
    .map_err(handle_db_error)?;
    info!("Updated todo #{}: {} (completed={})", todo.id, todo.title, todo.completed);
    Ok(Json(todo_to_response(&todo)))
}

#[delete("/todos/<todo_id>")]
async fn delete_todo(
    db_state: &State<SharedDb>,
    todo_id: i64,
) -> Result<Status, Status> {
    let mut db = db_state.lock().map_err(|_| Status::InternalServerError)?;
    task::spawn_blocking(move || db::delete_todo_db(&mut db.conn, todo_id))
        .await
        .map_err(handle_spawn_error)?
        .map_err(handle_db_error)?;
    info!("Deleted todo #{}", todo_id);
    Ok(Status::NoContent)
}
'''

with open(path, 'a', encoding='utf-8') as f:
    f.write(handlers)
print('Todo handlers added')
