"""
replay2video GUI launcher.
Serves ui.html on a local HTTP port, opens it in a PyWebView window.
File/folder dialogs are exposed as a pywebview JS API so they run on the
main thread (required by WebView2/WinForms). All other API calls go via
the HTTP server running in a background thread.
"""

import json
import os
import queue
import subprocess
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

# ── Paths ─────────────────────────────────────────────────────────────────────
if getattr(sys, 'frozen', False):
    BASE_DIR = Path(sys.executable).parent
    UI_HTML  = Path(sys._MEIPASS) / 'ui.html'
else:
    BASE_DIR = Path(__file__).parent
    UI_HTML  = BASE_DIR / 'ui.html'

EXE     = BASE_DIR / 'replay2video.exe'
PRESETS = BASE_DIR / 'presets'
CONFIG  = BASE_DIR / 'config.ini'

VERSION = 'v1.0.2'

# ── Shared encode state ───────────────────────────────────────────────────────
_proc: subprocess.Popen | None = None
_proc_lock = threading.Lock()
_sse_queue: queue.Queue = queue.Queue()

# Guard: only one file dialog open at a time
_dialog_lock = threading.Lock()


# ── Config / preset helpers ───────────────────────────────────────────────────
def load_presets() -> dict:
    result = {}
    if not PRESETS.is_dir():
        return result
    for p in sorted(PRESETS.glob('*.ini.example')):
        name = p.stem  # e.g. "gpu_hevc_nvenc_max.ini"  → stem = "gpu_hevc_nvenc_max.ini"
        # Strip the trailing ".ini" that glob().stem leaves from "*.ini.example"
        if name.endswith('.ini'):
            name = name[:-4]
        cfg = {}
        with p.open(encoding='utf-8', errors='replace') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                if '=' in line:
                    k, _, v = line.partition('=')
                    k = k.strip(); v = v.strip()
                    if k in ('fps', 'sim_fps', 'crf', 'bitrate', 'film_grain', 'player'):
                        try: v = int(v)
                        except ValueError: pass
                    elif k in ('speed', 'margin', 'cam_offset_y'):
                        try: v = float(v)
                        except ValueError: pass
                    cfg[k] = v
        result[name] = cfg
    return result


def read_config() -> dict:
    cfg = {}
    if not CONFIG.exists():
        return cfg
    with CONFIG.open(encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            if '=' in line:
                k, _, v = line.partition('=')
                cfg[k.strip()] = v.strip()
    return cfg


def write_config(params: dict):
    lines = ['# replay2video config — auto-saved by launcher\n']
    keys = [
        ('workdir', ''), ('last_replay_dir', ''), ('last_output_dir', ''),
        ('resolution', '1920x1080'), ('fps', 60),
        ('sim_fps', 0), ('codec', 'libx264'), ('preset', 'veryfast'),
        ('crf', 23), ('bitrate', 0), ('tune', ''), ('scale_filter', 'bilinear'),
        ('speed', 1.0), ('margin', 0.10), ('film_grain', 0), ('topdown_view', False),
        ('hwaccel', ''), ('hwaccel_device', ''),
    ]
    for k, default in keys:
        v = params.get(k, default)
        if v or v == 0:
            lines.append(f'{k}={v}\n')
    try:
        CONFIG.write_text(''.join(lines), encoding='utf-8')
    except OSError:
        pass


def save_dir(key: str, path: str):
    """Persist a single directory key into config.ini without clobbering other keys."""
    cfg = read_config()
    cfg[key] = path
    write_config(cfg)


# ── CLI args builder ──────────────────────────────────────────────────────────
def build_args(params: dict) -> list:
    args = [str(EXE)]

    def add(flag, key):
        v = params.get(key)
        if v:
            args.extend([flag, str(v)])

    args += ['--render-replay', params['input']]
    args += ['--output',        params['output']]
    args += ['--workdir',       params['workdir']]
    args += ['--resolution',    params['resolution']]
    args += ['--fps',           str(params.get('fps', 60))]

    sim_fps = int(params.get('sim_fps') or 0)
    if sim_fps > 0:
        args += ['--sim-fps', str(sim_fps)]

    add('--codec',       'codec')
    add('--preset',      'preset')
    args += ['--crf', str(params.get('crf', 23))]

    bitrate = int(params.get('bitrate') or 0)
    if bitrate > 0:
        args += ['--bitrate', str(bitrate)]

    add('--tune',         'tune')
    add('--scale-filter', 'scale_filter')
    args += ['--speed',  str(float(params.get('speed')  or 1.0))]
    args += ['--margin', str(float(params.get('margin') or 0.10))]

    fg = int(params.get('film_grain') or 0)
    if fg > 0:
        args += ['--film-grain', str(fg)]

    args += ['--player', str(int(params.get('pov_player') or 0))]
    add('--vf',             'vf')
    add('--hwaccel',        'hwaccel')
    add('--hwaccel-device', 'hwaccel_device')

    if params.get('dry_run'):
        args.append('--dry-run')

    return args


# ── SSE / stdout pipe ─────────────────────────────────────────────────────────
def sse_event(event: str, data: str) -> bytes:
    return f'event: {event}\ndata: {data}\n\n'.encode()


def stdout_reader(proc: subprocess.Popen):
    try:
        for raw in proc.stdout:
            line = raw.rstrip('\n\r')
            if line.startswith('[r2v:frame]'):
                parts = line.split()
                n = parts[1] if len(parts) > 1 else '0'
                _sse_queue.put(sse_event('frame', n))
            elif line:
                _sse_queue.put(sse_event('log', line))
    except Exception:
        pass
    rc = proc.wait()
    _sse_queue.put(sse_event('done', json.dumps({'ok': rc == 0, 'rc': rc})))


# ── PyWebView JS API (runs on main thread — required for file dialogs) ────────
class Api:
    """Methods exposed to JavaScript as window.pywebview.api.*

    pywebview dispatches JS API calls to the main GUI thread automatically on
    WinForms/WebView2. Do NOT wrap create_file_dialog in a sub-thread — that
    breaks the dispatcher and causes the window to freeze.
    """

    def _valid_dir(self, path: str) -> str:
        """Return path if it's an existing directory, else ''."""
        try:
            p = Path(path)
            return str(p) if p.is_dir() else ''
        except Exception:
            return ''

    def browse_replay(self, initial_dir: str = ''):
        if not _dialog_lock.acquire(blocking=False):
            return ''
        try:
            import webview
            d = self._valid_dir(initial_dir)
            r = webview.windows[0].create_file_dialog(
                webview.OPEN_DIALOG,
                directory=d,
                file_types=['EDOPro Replay (*.yrpX;*.yrp)', 'All files (*.*)'],
            )
            if r:
                chosen = r[0]
                save_dir('last_replay_dir', str(Path(chosen).parent))
                return chosen
            return ''
        except Exception:
            return ''
        finally:
            _dialog_lock.release()

    def browse_output(self, initial_dir: str = ''):
        if not _dialog_lock.acquire(blocking=False):
            return ''
        try:
            import webview
            d = self._valid_dir(initial_dir)
            r = webview.windows[0].create_file_dialog(
                webview.SAVE_DIALOG,
                directory=d,
                file_types=['MP4 Video (*.mp4)'],
                save_filename='output.mp4',
            )
            if r is None:
                return ''
            chosen = r if isinstance(r, str) else (r[0] if r else '')
            if chosen:
                save_dir('last_output_dir', str(Path(chosen).parent))
            return chosen
        except Exception:
            return ''
        finally:
            _dialog_lock.release()

    def browse_dir(self, initial_dir: str = ''):
        if not _dialog_lock.acquire(blocking=False):
            return ''
        try:
            import webview
            d = self._valid_dir(initial_dir)
            r = webview.windows[0].create_file_dialog(
                webview.FOLDER_DIALOG,
                directory=d,
            )
            if r:
                chosen = r[0]
                save_dir('workdir', chosen)
                return chosen
            return ''
        except Exception:
            return ''
        finally:
            _dialog_lock.release()

    def open_folder(self, path: str):
        folder = str(Path(path).parent)
        try:
            if sys.platform == 'win32':
                subprocess.Popen(['explorer', folder])
        except Exception:
            pass


# ── HTTP handler ──────────────────────────────────────────────────────────────
class Handler(BaseHTTPRequestHandler):

    def log_message(self, *args):
        pass

    def do_GET(self):
        p = urlparse(self.path)
        path = p.path

        routes = {
            '/':             self._serve_html,
            '/api/presets':  lambda: self._json({'presets': load_presets()}),
            '/api/config':   lambda: self._json(read_config()),
            '/api/version':  lambda: self._json({'version': VERSION}),
            '/api/progress': self._sse_stream,
        }
        handler = routes.get(path)
        if handler:
            handler()
        else:
            self._404()

    def do_POST(self):
        path = urlparse(self.path).path
        body = self._read_body()

        if path == '/api/encode':
            self._start_encode(body)
        elif path == '/api/cancel':
            self._cancel_encode()
        elif path == '/api/drop':
            self._handle_drop(body)
        else:
            self._404()

    def _serve_html(self):
        data = UI_HTML.read_bytes()
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Content-Length', len(data))
        self.end_headers()
        self.wfile.write(data)

    def _start_encode(self, params):
        global _proc
        with _proc_lock:
            if _proc and _proc.poll() is None:
                self._json({'ok': False, 'error': 'Encoding already in progress.'}); return
            if not EXE.exists():
                self._json({'ok': False, 'error': f'replay2video.exe not found at {EXE}'}); return
            while not _sse_queue.empty():
                try: _sse_queue.get_nowait()
                except queue.Empty: break
            write_config(params)
            try:
                _proc = subprocess.Popen(
                    build_args(params),
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                    creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0,
                )
            except Exception as e:
                self._json({'ok': False, 'error': str(e)}); return
            threading.Thread(target=stdout_reader, args=(_proc,), daemon=True).start()
        self._json({'ok': True})

    def _cancel_encode(self):
        global _proc
        with _proc_lock:
            if _proc and _proc.poll() is None:
                _proc.terminate()
        self._json({'ok': True})

    def _handle_drop(self, body):
        name = (body.get('name') or '').strip()
        if not name:
            self._json({'path': ''}); return
        cfg = read_config()
        candidates = [
            Path(name),
            BASE_DIR / name,
            Path(cfg.get('last_replay_dir', '')) / name if cfg.get('last_replay_dir') else None,
            Path(cfg.get('workdir', '')) / 'replay' / name if cfg.get('workdir') else None,
        ]
        for p in candidates:
            if p and p.exists():
                self._json({'path': str(p)}); return
        self._json({'path': name})

    def _sse_stream(self):
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream')
        self.send_header('Cache-Control', 'no-cache')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        try:
            while True:
                try:
                    msg = _sse_queue.get(timeout=25)
                    self.wfile.write(msg)
                    self.wfile.flush()
                    if b'event: done' in msg:
                        break
                except queue.Empty:
                    self.wfile.write(b': keepalive\n\n')
                    self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _read_body(self):
        n = int(self.headers.get('Content-Length', 0))
        if n:
            try: return json.loads(self.rfile.read(n))
            except Exception: return {}
        return {}

    def _json(self, data):
        body = json.dumps(data).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', len(body))
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(body)

    def _404(self):
        self.send_response(404); self.end_headers()


# ── Entry point ───────────────────────────────────────────────────────────────
def find_free_port():
    import socket
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def main():
    port = find_free_port()
    server = HTTPServer(('127.0.0.1', port), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()

    url = f'http://127.0.0.1:{port}/'
    api = Api()

    try:
        import webview
        window = webview.create_window(
            title='replay2video',
            url=url,
            js_api=api,
            width=740,
            height=720,
            min_size=(580, 540),
            resizable=True,
            background_color='#080810',
        )
        webview.start(debug=False)
    except ImportError:
        print(f'[launcher] pywebview not available — opening browser: {url}')
        webbrowser.open(url)
        try:
            while True: time.sleep(1)
        except KeyboardInterrupt:
            pass


if __name__ == '__main__':
    main()
