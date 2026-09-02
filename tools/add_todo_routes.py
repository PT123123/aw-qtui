path = 'vendor/aw-inbox/src/lib.rs'

with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

# 1. 在 import 里加 Todo 模型
old_import = '''use crate::models::{
    CreateCommentPayload, CreateNoteRelationPayload, NoteRelation,
    SyncRequest, SyncResponse, DeviceHeartbeat, DeviceListResponse,
    PushChange, PushResult, SyncConflict, DeviceState, DeviceInfo,
};'''
new_import = '''use crate::models::{
    CreateCommentPayload, CreateNoteRelationPayload, NoteRelation,
    SyncRequest, SyncResponse, DeviceHeartbeat, DeviceListResponse,
    PushChange, PushResult, SyncConflict, DeviceState, DeviceInfo,
};
use crate::models::{Todo, TodoResponse, CreateTodoPayload, UpdateTodoPayload};'''

if old_import in content:
    content = content.replace(old_import, new_import)
    print('Import added')
else:
    print('Import pattern not found')

# 2. 在 routes! 宏里加 Todo 路由
old_routes = '''            // 调试路由
            inbox_route_debug,
        ],'''
new_routes = '''            // Todo 路由
            get_todos,
            get_todo,
            create_todo,
            update_todo,
            delete_todo,
            // 调试路由
            inbox_route_debug,
        ],'''

if old_routes in content:
    content = content.replace(old_routes, new_routes)
    print('Routes registered')
else:
    print('Routes pattern not found')

# 3. 在 info! 日志里加 Todo 路由说明
old_info = '''    info!("  - POST   /inbox/sync/devices/heartbeat (format=json)");'''
new_info = '''    info!("  - POST   /inbox/sync/devices/heartbeat (format=json)");
    info!("  - GET    /inbox/todos");
    info!("  - GET    /inbox/todos/<id>");
    info!("  - POST   /inbox/todos (format=json)");
    info!("  - PUT    /inbox/todos/<id> (format=json)");
    info!("  - DELETE /inbox/todos/<id>");'''

if old_info in content:
    content = content.replace(old_info, new_info)
    print('Info log added')
else:
    print('Info pattern not found')

with open(path, 'w', encoding='utf-8') as f:
    f.write(content)
print('Done')
