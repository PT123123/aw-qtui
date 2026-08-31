"""内置 mock Inbox 服务端（内存实现，契约对齐 aw-inbox Rust 服务）。

用于在无 Rust 服务端时联调 aw-qtui 客户端。仅实现收件箱 / 局域网同步
所需的最小端点集合：

  GET/POST  /inbox/notes(..)   PUT/DELETE /inbox/notes/<id>
  GET       /inbox/tags, /inbox/tags/detailed
  GET/POST  /inbox/notes/<id>/comments
  POST      /inbox/sync                     GET /inbox/sync/devices
  POST      /inbox/sync/devices/heartbeat
"""
from __future__ import annotations

import json
import threading
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

_NOTES: dict[int, dict] = {}
# 评论即笔记：comment_note_id -> 被评论的父笔记 id（对齐真实 aw-inbox 的 note_relations Comment 关系）
_COMMENT_TARGETS: dict[int, int] = {}
_DEVICES: dict[str, dict] = {}
_GLOBAL_VERSION = 0
_NEXT_ID = 1
_LOCK = threading.Lock()


def _now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _resp_note(n: dict) -> dict:
    return {
        "id": n["id"],
        "content": n["content"],
        "tags": n["tags"],
        "created_at": n["created_at"],
        "updated_at": n["updated_at"],
        "version": n["version"],
        "device_id": n["device_id"],
        "deleted": n["deleted"],
        "synced_at": n["synced_at"],
        "conflict": False,
    }


def _bump_version():
    global _GLOBAL_VERSION
    _GLOBAL_VERSION += 1
    return _GLOBAL_VERSION


def _make_note(content: str, tags: list[str], device_id=None, note_id=None) -> dict:
    global _NEXT_ID
    if note_id is None:
        note_id = _NEXT_ID
        _NEXT_ID += 1
    now = _now()
    return {
        "id": note_id,
        "content": content,
        "tags": list(tags),
        "created_at": now,
        "updated_at": now,
        "version": _bump_version(),
        "device_id": device_id,
        "deleted": False,
        "synced_at": None,
    }


def _update_device_heartbeat(device_id, name, platform, pending_changes, local_version):
    d = _DEVICES.get(device_id, {})
    d.update(
        {
            "device_id": device_id,
            "name": name,
            "platform": platform,
            "last_seen_at": _now(),
            "last_synced_at": d.get("last_synced_at"),
            "pending_changes": pending_changes,
            "version": local_version,
            "status": "online",
        }
    )
    _DEVICES[device_id] = d


class _Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):  # 静默
        pass

    # ---- helpers ---- #
    def _send(self, code, obj):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self):
        length = int(self.headers.get("Content-Length", 0) or 0)
        raw = self.rfile.read(length) if length else b"{}"
        try:
            return json.loads(raw.decode("utf-8"))
        except Exception:
            return {}

    def _device_id(self):
        return self.headers.get("X-Device-ID") or "unknown"

    # ---- routing ---- #
    def do_GET(self):
        with _LOCK:
            self._handle_GET()

    def do_POST(self):
        with _LOCK:
            self._handle_POST()

    def do_PUT(self):
        with _LOCK:
            self._handle_PUT()

    def do_DELETE(self):
        with _LOCK:
            self._handle_DELETE()

    @staticmethod
    def _note_id(path: str):
        """从 /inbox/notes/<id>[/...] 提取 id，失败返回 None。"""
        parts = path.split("/")
        if len(parts) < 4:
            return None
        try:
            return int(parts[3])
        except ValueError:
            return None

    def _handle_GET(self):
        url = urlparse(self.path)
        path = url.path
        q = parse_qs(url.query)

        if path == "/inbox/notes":
            notes = sorted(_NOTES.values(), key=lambda n: n["created_at"], reverse=True)
            tag = q.get("tag", [None])[0]
            search = q.get("search", [None])[0]
            sort_by = q.get("sort_by", ["created"])[0]
            if tag:
                notes = [n for n in notes if tag in n["tags"]]
            if search:
                notes = [n for n in notes if search.lower() in n["content"].lower()]
            notes = sorted(notes, key=lambda n: n.get("updated_at") if sort_by == "updated" else n["created_at"], reverse=True)
            return self._send(200, [_resp_note(n) for n in notes])

        if path.startswith("/inbox/notes/") and path.endswith("/comments"):
            note_id = self._note_id(path)
            if note_id is None:
                return self._send(400, {"error": "bad note id"})
            # 评论是笔记：返回指向该父笔记的所有评论笔记（NoteResponse 形态）
            comments = [
                _resp_note(_NOTES[cid])
                for cid, target in _COMMENT_TARGETS.items()
                if target == note_id and cid in _NOTES
            ]
            comments.sort(key=lambda n: n.get("created_at", ""), reverse=True)
            return self._send(200, comments)

        if path == "/inbox/tags":
            tags = sorted({t for n in _NOTES.values() for t in n["tags"]})
            return self._send(200, tags)

        if path == "/inbox/tags/detailed":
            counts: dict[str, int] = {}
            last: dict[str, str] = {}
            for n in _NOTES.values():
                for t in n["tags"]:
                    counts[t] = counts.get(t, 0) + 1
                    last[t] = max(last.get(t, ""), n["updated_at"])
            out = [{"name": t, "count": c, "last_modified": last.get(t)} for t, c in counts.items()]
            out.sort(key=lambda x: -x["count"])
            return self._send(200, out)

        if path == "/inbox/sync/devices":
            devices = list(_DEVICES.values())
            out = []
            for d in devices:
                out.append(
                    {
                        "device_id": d["device_id"],
                        "name": d.get("name", ""),
                        "platform": d.get("platform", ""),
                        "last_seen_at": d.get("last_seen_at", ""),
                        "last_synced_at": d.get("last_synced_at"),
                        "pending_changes": d.get("pending_changes", 0),
                        "version": d.get("version", 0),
                        "is_current": d["device_id"] == self._device_id(),
                        "status": d.get("status", "unknown"),
                    }
                )
            return self._send(200, {"devices": out, "global_version": _GLOBAL_VERSION})

        return self._send(404, {"error": "not found"})

    def _handle_POST(self):
        url = urlparse(self.path)
        path = url.path

        if path == "/inbox/notes":
            body = self._read_json()
            note = _make_note(
                body.get("content", ""),
                body.get("tags") or [],
                device_id=self._device_id(),
            )
            _NOTES[note["id"]] = note
            return self._send(201, _resp_note(note))

        if path == "/inbox/sync":
            body = self._read_json()
            pulled = [_resp_note(n) for n in sorted(_NOTES.values(), key=lambda x: x["version"])]
            return self._send(
                200,
                {
                    "current_version": _GLOBAL_VERSION,
                    "pulled_notes": pulled,
                    "has_more": False,
                    "conflicts": [],
                    "push_results": [],
                    "device_states": {
                        did: {"version": d.get("version", 0), "last_seen": d.get("last_seen_at", ""), "pending": d.get("pending_changes", 0)}
                        for did, d in _DEVICES.items()
                    },
                },
            )

        if path == "/inbox/sync/devices/heartbeat":
            body = self._read_json()
            _update_device_heartbeat(
                body.get("device_id", "unknown"),
                body.get("name", ""),
                body.get("platform", ""),
                body.get("pending_changes", 0),
                body.get("local_version", 0),
            )
            return self._send(200, {"ok": True})

        if path.startswith("/inbox/notes/") and path.endswith("/comments"):
            note_id = self._note_id(path)
            if note_id is None or note_id not in _NOTES:
                return self._send(404, {"error": "note not found"})
            body = self._read_json()
            # 对齐真实 aw-inbox：评论 = 新建一条笔记 + Comment 关系（GET /inbox/notes 也会返回它）
            comment = _make_note(body.get("content", ""), [], device_id=self._device_id())
            _NOTES[comment["id"]] = comment
            _COMMENT_TARGETS[comment["id"]] = note_id
            return self._send(201, _resp_note(comment))

        return self._send(404, {"error": "not found"})

    def _handle_PUT(self):
        url = urlparse(self.path)
        path = url.path
        if path.startswith("/inbox/notes/"):
            note_id = self._note_id(path)
            if note_id is None or note_id not in _NOTES:
                return self._send(404, {"error": "not found"})
            body = self._read_json()
            note = _NOTES[note_id]
            note["content"] = body.get("content", note["content"])
            note["tags"] = list(body.get("tags") or note["tags"])
            note["updated_at"] = _now()
            note["version"] = _bump_version()
            return self._send(200, _resp_note(note))
        return self._send(404, {"error": "not found"})

    def _handle_DELETE(self):
        url = urlparse(self.path)
        path = url.path
        if path.startswith("/inbox/notes/"):
            note_id = self._note_id(path)
            if note_id is None or note_id not in _NOTES:
                return self._send(404, {"error": "not found"})
            del _NOTES[note_id]
            # 连带清理指向该笔记的评论关系
            global _COMMENT_TARGETS
            _COMMENT_TARGETS = {cid: t for cid, t in _COMMENT_TARGETS.items() if t != note_id}
            _bump_version()
            return self._send(204, None)
        return self._send(404, {"error": "not found"})

def start_mock_server(port: int = 5600, host: str = "127.0.0.1", seed: bool = True) -> ThreadingHTTPServer:
    """启动 mock 服务端（后台线程），返回 server 对象。"""
    if seed:
        # 造几条演示数据
        demo = [
            ("今晚整理 ActivityWatch 二次开发的进度 #aw #todo", ["aw", "todo"]),
            ("卧推 80kg 达成，下个目标 85 #健身", ["健身"]),
            ("研究 mdns 局域网同步原理 _activitywatch._tcp.local. #同步 #技术", ["同步", "技术"]),
        ]
        for content, tags in demo:
            note = _make_note(content, tags, device_id="mock-seed")
            _NOTES[note["id"]] = note
        _update_device_heartbeat("mock-seed", "seed-host", "mock", 0, _GLOBAL_VERSION)

    server = ThreadingHTTPServer((host, port), _Handler)
    t = threading.Thread(target=server.serve_forever, daemon=True, name="mock-inbox")
    t.start()
    return server


if __name__ == "__main__":
    import sys
    _port = int(sys.argv[1]) if len(sys.argv) > 1 else 5600
    _host = sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1"
    _srv = start_mock_server(port=_port, host=_host)
    print(f"mock inbox server listening on http://{_host}:{_port}  (Ctrl+C to stop)")
    try:
        threading.Event().wait()
    except KeyboardInterrupt:
        _srv.shutdown()

