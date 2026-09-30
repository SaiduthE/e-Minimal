"""A stand-in for the reader's web server, for testing the web pages on a PC.

Serves src/network/html the way CrossPointWebServer routes it (/, /files,
/convert, /dashboards, /pictures, /settings, /fonts, /css/app.css, /js/*.js)
on PORT, with the file API the pages call (/api/files, /download, /upload,
/mkdir, /rename, /move, /delete), /api/status, and an in-memory
/api/dashboards and /api/todo. The WebSocket upload runs on PORT+1 with the
same protocol as onWebSocketEvent():
START:<name>:<size>:<path> -> READY | ERROR:..., binary chunks, then DONE.
Files live in CARD_DIR, mirroring the SD card.

    python scripts/bindery/mock_reader.py 8766 CARD_DIR
"""
import asyncio, json, os, shutil, sys, threading
from email.parser import BytesParser
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from urllib.parse import parse_qs, urlsplit
import websockets

PORT = int(sys.argv[1]); CARD = os.path.abspath(sys.argv[2])
HTML = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "src", "network", "html")
os.makedirs(CARD, exist_ok=True)
ROUTES = {"/": "/HomePage.html", "/files": "/FilesPage.html", "/convert": "/ConvertPage.html",
          "/dashboards": "/DashboardsPage.html", "/pictures": "/PicturesPage.html",
          "/settings": "/SettingsPage.html", "/fonts": "/FontsPage.html"}

DASH = {
    "layout": {"active": 1}, "orientation": 0,
    "grid": {"long": 14, "short": 10, "minCells": 2, "maxTiles": 8, "maxName": 32, "maxLayouts": 12},
    "layouts": [
        {"id": 1, "name": "Wide top", "shape": "landscape", "tiles": [
            {"widget": "weather", "x": 0, "y": 0, "w": 14, "h": 5},
            {"widget": "todo", "x": 0, "y": 5, "w": 7, "h": 5},
            {"widget": "image", "x": 7, "y": 5, "w": 7, "h": 5}]},
        {"id": 2, "name": "Grid", "shape": "landscape", "tiles": [
            {"widget": "weather", "x": 0, "y": 0, "w": 7, "h": 5}, {"widget": "date", "x": 7, "y": 0, "w": 7, "h": 5},
            {"widget": "todo", "x": 0, "y": 5, "w": 7, "h": 5}, {"widget": "image", "x": 7, "y": 5, "w": 7, "h": 5}]}],
    "weather": {"hasLocation": True, "place": "Hyderabad", "lat": 17.385, "lon": 78.4867, "units": "c"},
    "carousel": {"folder": "/carousel", "intervalMinutes": 30, "minMinutes": 5, "maxMinutes": 1440},
}
TODO = {"items": [{"text": "Water the plants", "done": False}, {"text": "Return library books", "done": True}],
        "max": 20, "maxText": 120}

def card_path(web):
    return os.path.join(CARD, *[p for p in web.split("/") if p and p not in (".", "..")])

def multipart(headers, raw):
    msg = BytesParser().parsebytes(b"Content-Type: " + headers["Content-Type"].encode() + b"\r\n\r\n" + raw)
    return {part.get_param("name", header="content-disposition"):
            (part.get_filename(), part.get_payload(decode=True)) for part in msg.get_payload()}

class H(SimpleHTTPRequestHandler):
    def __init__(self, *a, **k): super().__init__(*a, directory=HTML, **k)
    def log_message(self, *a): pass
    def reply(self, code, body=b"", kind="text/plain"):
        if isinstance(body, str): body = body.encode()
        self.send_response(code); self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
    def json(self, value): self.reply(200, json.dumps(value), "application/json")
    def do_GET(self):
        url = urlsplit(self.path); q = parse_qs(url.query)
        if url.path == "/api/status":
            return self.json({"version": "mock", "ip": "127.0.0.1", "mode": "STA", "freeHeap": 200000, "device": "eMinimal"})
        if url.path == "/api/files":
            d = card_path(q.get("path", ["/"])[0])
            if not os.path.isdir(d): return self.json([])
            out = []
            for name in sorted(os.listdir(d)):
                if name.startswith("."): continue
                p = os.path.join(d, name); isdir = os.path.isdir(p)
                out.append({"name": name, "size": 0 if isdir else os.path.getsize(p), "isDirectory": isdir,
                            "isEpub": name.lower().endswith(".epub")})
            return self.json(out)
        if url.path == "/download":
            p = card_path(q.get("path", [""])[0])
            if not os.path.isfile(p): return self.reply(404, "Item not found")
            with open(p, "rb") as f: return self.reply(200, f.read(), self.guess_type(p))
        if url.path == "/api/dashboards": return self.json(DASH)
        if url.path == "/api/todo": return self.json(TODO)
        self.path = ROUTES.get(url.path, url.path)
        return super().do_GET()
    def do_POST(self):
        url = urlsplit(self.path); q = parse_qs(url.query)
        n = int(self.headers.get("Content-Length", 0)); raw = self.rfile.read(n)
        kind = self.headers.get("Content-Type", "")
        if url.path == "/mkdir":
            form = multipart(self.headers, raw)
            p = os.path.join(card_path(form["path"][1].decode()), form["name"][1].decode())
            if os.path.exists(p): return self.reply(400, "Folder already exists")
            os.makedirs(p); return self.reply(200, "Folder created")
        if url.path == "/upload":
            name, data = multipart(self.headers, raw)["file"]
            target = os.path.join(card_path(q.get("path", ["/"])[0]), name)
            if os.path.exists(target): return self.reply(400, "File already exists")
            with open(target, "wb") as f: f.write(data)
            return self.reply(200, "File uploaded successfully")
        if url.path == "/rename":
            form = multipart(self.headers, raw); src = card_path(form["path"][1].decode())
            dst = os.path.join(os.path.dirname(src), form["name"][1].decode())
            if not os.path.exists(src): return self.reply(404, "Item not found")
            if os.path.exists(dst): return self.reply(409, "Target already exists")
            os.rename(src, dst); return self.reply(200, "Renamed")
        if url.path == "/move":
            form = multipart(self.headers, raw); src = card_path(form["path"][1].decode())
            dest = card_path(form["dest"][1].decode())
            if not os.path.exists(src): return self.reply(404, "Item not found")
            if os.path.isdir(src): return self.reply(400, "Only files can be moved")
            if not os.path.isdir(dest): return self.reply(404, "Destination not found")
            target = os.path.join(dest, os.path.basename(src))
            if os.path.exists(target): return self.reply(409, "Target already exists")
            shutil.move(src, target); return self.reply(200, "Moved")
        if url.path == "/delete":
            form = parse_qs(raw.decode())
            paths = json.loads(form["paths"][0]) if "paths" in form else form.get("path", [])
            failed = ""
            for web in paths:
                p = card_path(web)
                if not os.path.exists(p): failed += web + " (not found); "
                elif os.path.isdir(p):
                    if os.listdir(p): failed += web + " (folder not empty); "
                    else: os.rmdir(p)
                else: os.remove(p)
            return self.reply(500, "Failed to delete some items: " + failed) if failed else self.reply(200, "Deleted")
        if url.path == "/api/dashboards" and "json" in kind:
            body = json.loads(raw or b"{}")
            for key in ("weather", "carousel"):
                if isinstance(body.get(key), dict):
                    if body[key].pop("clearLocation", False): DASH["weather"].update(hasLocation=False, place="")
                    DASH[key].update(body[key])
                    if key == "weather" and "lat" in body[key]: DASH["weather"]["hasLocation"] = True
            if isinstance(body.get("layout"), dict) and "active" in body["layout"]:
                DASH["layout"]["active"] = body["layout"]["active"]
            return self.json({"ok": True})
        if url.path == "/api/todo":
            body = json.loads(raw or b"{}"); op = body.get("op"); items = TODO["items"]; i = body.get("index", -1)
            if op == "add": items.append({"text": body.get("text", ""), "done": False})
            elif op == "edit" and 0 <= i < len(items): items[i]["text"] = body.get("text", "")
            elif op == "toggle" and 0 <= i < len(items): items[i]["done"] = not items[i]["done"]
            elif op == "delete" and 0 <= i < len(items): items.pop(i)
            elif op == "clearDone": TODO["items"] = [t for t in items if not t["done"]]
            return self.json(TODO)
        if url.path == "/api/settings": return self.reply(200, "Applied 1 setting(s)")
        self.reply(404, "Not found")

async def ws_handler(ws):
    f = None; size = got = 0
    async for msg in ws:
        if isinstance(msg, str):
            if msg.startswith("START:"):
                name, sz, path = msg[6:].split(":", 2)
                target = os.path.join(card_path(path), name)
                if os.path.exists(target): await ws.send("ERROR:File already exists: " + name); continue
                if not os.path.isdir(card_path(path)): await ws.send("ERROR:Failed to create file"); continue
                f = open(target, "wb"); size = int(sz); got = 0
                print("START", target, size, flush=True)
                await ws.send("READY")
        else:
            if f is None: await ws.send("ERROR:No upload in progress"); continue
            if got + len(msg) > size: await ws.send("ERROR:Upload overflow"); f.close(); f = None; continue
            f.write(msg); got += len(msg)
            if got == size:
                f.close(); f = None; print("DONE", got, flush=True); await ws.send("DONE")
    if f: f.close(); print("ABORTED at", got, "of", size, flush=True)

def http(): ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
threading.Thread(target=http, daemon=True).start()
async def main():
    async with websockets.serve(ws_handler, "127.0.0.1", PORT + 1, max_size=None):
        print(f"mock reader on http://127.0.0.1:{PORT}/, ws {PORT + 1}, card {CARD}", flush=True)
        await asyncio.Future()
asyncio.run(main())
