#!/usr/bin/env python3

import argparse
import base64
import hashlib
import json
import math
import mimetypes
import os
import re
import select
import signal
import socket
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


ROOT = Path(os.environ.get("PEK_SUPERVISOR_ROOT", "/work"))
STATIC_ROOT = Path(os.environ.get("PEK_SUPERVISOR_STATIC", ROOT / "development/web/content"))
PIPELINES_ROOT = Path(os.environ.get("PEK_SUPERVISOR_PIPELINES", ROOT / "config/pipelines"))
MODELS_ROOT = Path(os.environ.get("PEK_SUPERVISOR_MODELS", ROOT / "config/models"))
OPCHAINS_ROOT = Path(os.environ.get("PEK_SUPERVISOR_OPCHAINS", ROOT / "config/opchains"))
PEK_MENU = Path(os.environ.get("PEK_SUPERVISOR_PEK_MENU", ROOT / "tools/pek-menu"))

HOST = os.environ.get("PEK_SUPERVISOR_HOST", "0.0.0.0")
HTTP_PORT = int(os.environ.get("PEK_SUPERVISOR_HTTP_PORT", "9999"))
CHILD_HTTP_PORT = int(os.environ.get("PEK_CHILD_HTTP_PORT", "10099"))
WS_PORT = int(os.environ.get("PEK_CHILD_WS_PORT", "8000"))
CTRL_PORT = int(os.environ.get("PEK_CHILD_CTRL_PORT", "8001"))
CAMERA_NAME = os.environ.get("PEK_CAMERA_NAME", "")
ONNXRUNTIME_ROOT = os.environ.get("PEK_ONNXRUNTIME_ROOT", str(ROOT / "deps/onnxruntime"))
SOURCE_MODE = os.environ.get("PEK_SUPERVISOR_SOURCE", "raspicam")
ROI_MIN_FRACTION = 0.02
ROI_MIN_PIXELS = 32
RESOLUTION_SCALE_MIN = 0.1
RESOLUTION_SCALE_MAX = 1.0


PROCESSING_MARKERS = (
    "pekinfer",
    "pektracker",
    "pekperformance",
    "pekosd",
    "peksink",
    "fakesink",
    "filesink",
)


def pipeline_label_from_id(pipeline_id):
    return pipeline_id.split("/")[-1].replace("-", " ").replace("_", " ").title()


def json_response(handler, payload, status=200):
    data = json.dumps(payload).encode("utf-8")
    handler.send_response(status)
    handler.send_header("Content-Type", "application/json")
    handler.send_header("Content-Length", str(len(data)))
    handler.send_header("Cache-Control", "no-store")
    handler.end_headers()
    handler.wfile.write(data)


def text_response(handler, text, status=200, content_type="text/plain"):
    data = text.encode("utf-8")
    handler.send_response(status)
    handler.send_header("Content-Type", content_type)
    handler.send_header("Content-Length", str(len(data)))
    handler.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
    handler.send_header("Pragma", "no-cache")
    handler.send_header("Expires", "0")
    handler.end_headers()
    handler.wfile.write(data)


def chrome_reset_response(handler):
    html = """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Reset WebUI</title>
  <style>
    html, body { margin: 0; min-height: 100%; background: #000; color: #e5e7eb; font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; }
    body { display: grid; place-items: center; }
    main { max-width: 440px; padding: 24px; line-height: 1.45; }
    h1 { margin: 0 0 12px; font-size: 1.15rem; }
    p { color: #9ca3af; margin: 0 0 16px; }
    button { border-radius: 999px; border: 1px solid rgba(226, 232, 240, 0.24); background: #050505; color: #f8fafc; padding: 9px 14px; font: inherit; cursor: pointer; }
  </style>
</head>
<body>
  <main>
    <h1>Resetting WebUI</h1>
    <p id="status">Clearing Chrome's stored WebUI state...</p>
    <button id="continueBtn" type="button" hidden>Open WebUI</button>
  </main>
  <script>
    async function resetStorage() {
      const tasks = [];
      if ("serviceWorker" in navigator) {
        tasks.push(navigator.serviceWorker.getRegistrations()
          .then((registrations) => Promise.all(registrations.map((registration) => registration.unregister()))));
      }
      if ("caches" in window) {
        tasks.push(caches.keys().then((keys) => Promise.all(keys.map((key) => caches.delete(key)))));
      }
      if ("indexedDB" in window && indexedDB.databases) {
        tasks.push(indexedDB.databases().then((dbs) => Promise.all(
          dbs.map((db) => db.name).filter(Boolean).map((name) => new Promise((resolve) => {
            const req = indexedDB.deleteDatabase(name);
            req.onsuccess = req.onerror = req.onblocked = resolve;
          }))
        )));
      }

      try { localStorage.clear(); } catch (e) {}
      try { sessionStorage.clear(); } catch (e) {}
      await Promise.allSettled(tasks);
    }

    async function run() {
      const status = document.getElementById("status");
      const button = document.getElementById("continueBtn");
      await resetStorage();
      status.textContent = "Opening the current WebUI...";
      const next = "/?chrome-reset=" + Date.now();
      button.hidden = false;
      button.onclick = () => location.replace(next);
      setTimeout(() => location.replace(next), 500);
    }

    run().catch(() => {
      document.getElementById("status").textContent = "Reset finished. Open the WebUI again.";
      const button = document.getElementById("continueBtn");
      button.hidden = false;
      button.onclick = () => location.replace("/?chrome-reset=" + Date.now());
    });
  </script>
</body>
</html>
"""
    data = html.encode("utf-8")
    handler.send_response(200)
    handler.send_header("Content-Type", "text/html; charset=utf-8")
    handler.send_header("Content-Length", str(len(data)))
    handler.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
    handler.send_header("Pragma", "no-cache")
    handler.send_header("Expires", "0")
    handler.send_header("Clear-Site-Data", '"cache", "storage"')
    handler.end_headers()
    handler.wfile.write(data)


def read_exact(sock, size):
    chunks = bytearray()
    while len(chunks) < size:
        chunk = sock.recv(size - len(chunks))
        if not chunk:
            raise ConnectionError("socket closed")
        chunks.extend(chunk)
    return bytes(chunks)


def read_websocket_text_frame(sock):
    first, second = read_exact(sock, 2)
    opcode = first & 0x0F
    masked = bool(second & 0x80)
    length = second & 0x7F

    if length == 126:
        length = int.from_bytes(read_exact(sock, 2), "big")
    elif length == 127:
        length = int.from_bytes(read_exact(sock, 8), "big")

    mask = read_exact(sock, 4) if masked else None
    payload = bytearray(read_exact(sock, length))

    if mask:
        for index in range(len(payload)):
            payload[index] ^= mask[index % 4]

    if opcode != 1:
        return ""

    return payload.decode("utf-8", errors="replace")


def write_websocket_text_frame(sock, text):
    payload = text.encode("utf-8")
    mask = os.urandom(4)
    length = len(payload)

    if length < 126:
        header = bytes([0x81, 0x80 | length])
    elif length < 65536:
        header = bytes([0x81, 0x80 | 126]) + length.to_bytes(2, "big")
    else:
        header = bytes([0x81, 0x80 | 127]) + length.to_bytes(8, "big")

    masked_payload = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
    sock.sendall(header + mask + masked_payload)


def connect_ctrl_websocket(timeout=3):
    key = base64.b64encode(os.urandom(16)).decode("ascii")
    expected_accept = base64.b64encode(
        hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")).digest()
    ).decode("ascii")

    sock = socket.create_connection(("127.0.0.1", CTRL_PORT), timeout=timeout)
    sock.settimeout(timeout)
    request = (
        "GET /ws HTTP/1.1\r\n"
        f"Host: 127.0.0.1:{CTRL_PORT}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Origin: http://127.0.0.1\r\n"
        "\r\n"
    )
    sock.sendall(request.encode("ascii"))

    try:
        response = bytearray()
        while b"\r\n\r\n" not in response:
            response.extend(sock.recv(4096))

        header_text = response.decode("iso-8859-1", errors="replace")
        if " 101 " not in header_text or expected_accept not in header_text:
            raise ConnectionError("control websocket handshake failed")
        return sock
    except Exception:
        sock.close()
        raise


def fetch_ctrl_snapshot(timeout=3):
    with connect_ctrl_websocket(timeout) as sock:
        return json.loads(read_websocket_text_frame(sock))


def active_model_states_from_snapshot(snapshot):
    states = {}
    for model in snapshot.get("models", []):
        element_name = model.get("element_name")
        if element_name:
            states[element_name] = bool(model.get("active"))
    return states


def fetch_active_model_states(timeout=1):
    try:
        return active_model_states_from_snapshot(fetch_ctrl_snapshot(timeout))
    except Exception as exc:
        print(f"[supervisor] could not capture model state: {exc}", flush=True)
        return {}


def restore_model_states(states, attempts=8, timeout=1):
    if not states:
        return

    last_error = None
    for attempt in range(attempts):
        try:
            with connect_ctrl_websocket(timeout) as sock:
                # Drain the initial status frame before sending commands.
                read_websocket_text_frame(sock)
                for element_name, active in states.items():
                    write_websocket_text_frame(
                        sock,
                        json.dumps({
                            "type": "model_toggle",
                            "name": element_name,
                            "active": active,
                        }),
                    )
                    time.sleep(0.05)
            return
        except Exception as exc:
            last_error = exc
            time.sleep(0.35 + (attempt * 0.1))

    print(f"[supervisor] could not restore model state: {last_error}", flush=True)


def resolve_pipeline(pipeline_id):
    if not pipeline_id:
        return None

    candidate = Path(pipeline_id)
    if candidate.is_absolute() and candidate.exists():
        return candidate

    relative = pipeline_id[:-5] if pipeline_id.endswith(".json") else pipeline_id
    candidate = PIPELINES_ROOT / f"{relative}.json"
    if candidate.exists():
        return candidate

    return None


def list_pipelines(current_path):
    pipelines = []
    if not PIPELINES_ROOT.exists():
        return pipelines

    for path in sorted(PIPELINES_ROOT.rglob("*.json")):
        if path.name.startswith("DISABLED_"):
            continue

        relative = path.relative_to(PIPELINES_ROOT).with_suffix("").as_posix()
        pipeline = {
            "id": relative,
            "label": pipeline_label_from_id(relative),
            "path": str(path),
            "description": "",
        }

        try:
            with path.open("r", encoding="utf-8") as handle:
                data = json.load(handle)
            pipeline["description"] = data.get("description", "")
        except Exception:
            pass

        pipelines.append(pipeline)

    current = ""
    if current_path:
        current_base = Path(current_path).stem
        for pipeline in pipelines:
            if current_path == pipeline["path"] or current_base == pipeline["id"] or current_base.startswith(f"{pipeline['id']}-"):
                current = pipeline["id"]
                break

    return pipelines, current


def rewrite_pipeline_piece(piece):
    if not isinstance(piece, str):
        return piece

    if CAMERA_NAME and "libcamerasrc" in piece:
        piece = re.sub(r'camera-name=(?:"[^"]+"|[^ !]+)', f'camera-name="{CAMERA_NAME}"', piece)

    if "pekosd" in piece:
        trailing_bang = ""
        stripped = piece.rstrip()
        if stripped.endswith("!"):
            stripped = stripped[:-1].rstrip()
            trailing_bang = " !"

        stripped = re.sub(r"\s+enable-perfdata=\S+", "", stripped)
        stripped = re.sub(r"\s+enabled=\S+", "", stripped)
        piece = f"{stripped} enabled=false enable-perfdata=false{trailing_bang}"

    if "peksink" not in piece:
        return piece

    trailing_bang = ""
    stripped = piece.rstrip()
    if stripped.endswith("!"):
        stripped = stripped[:-1].rstrip()
        trailing_bang = " !"

    stripped = re.sub(r"\s+(http-port|ws-port|ctrl-port)=\S+", "", stripped)
    stripped = f"{stripped} http-port={CHILD_HTTP_PORT} ws-port={WS_PORT} ctrl-port={CTRL_PORT}"
    return f"{stripped}{trailing_bang}"


def first_processing_index(pipeline):
    for index, piece in enumerate(pipeline):
        if not isinstance(piece, str):
            continue
        if any(marker in piece for marker in PROCESSING_MARKERS):
            return index
    return 0


def parse_cap_dimension(piece, name):
    pattern = rf"(?:^|[, !]){re.escape(name)}=(?:\(int\))?([0-9]+)"
    matches = re.findall(pattern, piece)
    if not matches:
        return None

    return int(matches[-1])


def source_dimensions_from_pipeline(pipeline, stop_index):
    width = None
    height = None

    for piece in pipeline[:stop_index]:
        if not isinstance(piece, str):
            continue

        parsed_width = parse_cap_dimension(piece, "width")
        parsed_height = parse_cap_dimension(piece, "height")
        if parsed_width:
            width = parsed_width
        if parsed_height:
            height = parsed_height

    if width and height:
        return width, height

    return None


def roi_insert_index(pipeline, processing_index):
    for index, piece in enumerate(pipeline[:processing_index]):
        if isinstance(piece, str) and "videoconvert" in piece:
            return index

    return processing_index


def normalise_roi(roi):
    if not isinstance(roi, dict):
        raise ValueError("Crop region is missing.")

    values = {}
    for key in ("x", "y", "width", "height"):
        try:
            value = float(roi[key])
        except (KeyError, TypeError, ValueError):
            raise ValueError("Crop region must include x, y, width and height.") from None

        if not math.isfinite(value):
            raise ValueError("Crop region values must be finite numbers.")
        values[key] = value

    x = min(max(values["x"], 0.0), 1.0)
    y = min(max(values["y"], 0.0), 1.0)
    width = min(max(values["width"], 0.0), 1.0 - x)
    height = min(max(values["height"], 0.0), 1.0 - y)

    if width < ROI_MIN_FRACTION or height < ROI_MIN_FRACTION:
        raise ValueError("Crop region is too small.")

    return {
        "x": round(x, 6),
        "y": round(y, 6),
        "width": round(width, 6),
        "height": round(height, 6),
    }


def normalise_resolution_scale(scale, fallback=1.0):
    if scale is None:
        return fallback

    try:
        value = float(scale)
    except (TypeError, ValueError):
        raise ValueError("Resolution scale must be a number.") from None

    if not math.isfinite(value):
        raise ValueError("Resolution scale must be a finite number.")

    if value > 1.0:
        value /= 100.0

    return round(min(max(value, RESOLUTION_SCALE_MIN), RESOLUTION_SCALE_MAX), 4)


def roi_pixel_crop(roi, source_width, source_height):
    pixel_width = max(ROI_MIN_PIXELS, min(source_width, round(roi["width"] * source_width)))
    pixel_height = max(ROI_MIN_PIXELS, min(source_height, round(roi["height"] * source_height)))
    pixel_x = min(max(0, round(roi["x"] * source_width)), max(0, source_width - pixel_width))
    pixel_y = min(max(0, round(roi["y"] * source_height)), max(0, source_height - pixel_height))

    return {
        "left": pixel_x,
        "right": max(0, source_width - pixel_x - pixel_width),
        "top": pixel_y,
        "bottom": max(0, source_height - pixel_y - pixel_height),
        "width": pixel_width,
        "height": pixel_height,
        "sourceWidth": source_width,
        "sourceHeight": source_height,
    }


def transform_dimensions(source_width, source_height, roi, resolution_scale):
    crop = None
    base_width = source_width
    base_height = source_height

    if roi:
        crop = roi_pixel_crop(roi, source_width, source_height)
        base_width = crop["width"]
        base_height = crop["height"]

    output_width = max(ROI_MIN_PIXELS, min(base_width, round(base_width * resolution_scale)))
    output_height = max(ROI_MIN_PIXELS, min(base_height, round(base_height * resolution_scale)))

    return {
        "source": {"width": source_width, "height": source_height},
        "crop": crop,
        "base": {"width": base_width, "height": base_height},
        "output": {"width": output_width, "height": output_height},
        "resolutionScale": resolution_scale,
    }


def apply_roi_transform(data, roi, resolution_scale):
    pipeline = data.get("pipeline")
    if not isinstance(pipeline, list):
        if roi or resolution_scale < 0.9999:
            raise ValueError("ROI transform requires a list-based pipeline.")
        return None

    if not roi and resolution_scale >= 0.9999:
        return None

    processing_index = first_processing_index(pipeline)
    dimensions = source_dimensions_from_pipeline(pipeline, processing_index)
    if not dimensions:
        raise ValueError("ROI transform needs source width and height in the pipeline caps.")

    source_width, source_height = dimensions
    transform = transform_dimensions(source_width, source_height, roi, resolution_scale)
    insert_index = roi_insert_index(pipeline, processing_index)

    transform_stage = []
    if transform["crop"]:
        crop = transform["crop"]
        transform_stage.append(
            "videocrop name=pekroicrop "
            f"left={crop['left']} right={crop['right']} top={crop['top']} bottom={crop['bottom']} !"
        )
        transform_stage.append(f"video/x-raw,width={transform['base']['width']},height={transform['base']['height']} !")

    if resolution_scale < 0.9999:
        transform_stage.extend([
            "videoscale !",
            f"video/x-raw,width={transform['output']['width']},height={transform['output']['height']} !",
        ])

    data["pipeline"] = [
        *pipeline[:insert_index],
        *transform_stage,
        *pipeline[insert_index:],
    ]
    data["supervisor-roi"] = {
        "normalised": roi,
        "transform": transform,
    }
    return transform


def resolution_info_for(source_path, roi=None, resolution_scale=1.0):
    with source_path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)

    apply_source_mode(data)
    pipeline = data.get("pipeline")
    if not isinstance(pipeline, list):
        return None

    dimensions = source_dimensions_from_pipeline(pipeline, first_processing_index(pipeline))
    if not dimensions:
        return None

    return transform_dimensions(dimensions[0], dimensions[1], roi, resolution_scale)


def apply_source_mode(data):
    if SOURCE_MODE != "raspicam":
        return

    pipeline = data.get("pipeline")
    source = data.get("alternative-source-raspicam")
    if not isinstance(pipeline, list) or not isinstance(source, list) or not source:
        return

    data["pipeline"] = [*source, *pipeline[first_processing_index(pipeline):]]


def prepared_pipeline_file(source_path, roi=None, resolution_scale=1.0):
    with source_path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)

    apply_source_mode(data)
    apply_roi_transform(data, roi, resolution_scale)

    pipeline = data.get("pipeline")
    if isinstance(pipeline, list):
        data["pipeline"] = [rewrite_pipeline_piece(piece) for piece in pipeline]
    elif isinstance(pipeline, str):
        data["pipeline"] = rewrite_pipeline_piece(pipeline)

    temp = tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        prefix=f"pek-supervisor-{source_path.stem}-",
        suffix=".json",
        delete=False,
    )
    with temp:
        json.dump(data, temp)
    return Path(temp.name)


def model_info(name):
    for base in (MODELS_ROOT, OPCHAINS_ROOT):
        direct = base / name
        for filename in ("model.json", "opchain.json"):
            path = direct / filename
            if path.exists():
                with path.open("r", encoding="utf-8") as handle:
                    return json.load(handle)

        if not base.exists():
            continue

        for path in base.glob("*/model.json"):
            try:
                with path.open("r", encoding="utf-8") as handle:
                    data = json.load(handle)
                if data.get("name") == name:
                    return data
            except Exception:
                pass

        for path in base.glob("*/opchain.json"):
            try:
                with path.open("r", encoding="utf-8") as handle:
                    data = json.load(handle)
                if data.get("name") == name:
                    return data
            except Exception:
                pass

    return {"error": "model.json or opchain.json not found"}


class PipelineSupervisor:
    def __init__(self, initial_pipeline):
        self.lock = threading.Lock()
        self.process = None
        self.current_source = None
        self.current_prepared = None
        self.roi = None
        self.resolution_scale = 1.0
        self.last_message = ""
        self.switching = False
        self.initial_pipeline = initial_pipeline

    def stop_existing_gst_launches(self):
        subprocess.run(["pkill", "-x", "gst-launch-1.0"], check=False)
        time.sleep(1.0)

    def status(self):
        with self.lock:
            running = self.process is not None and self.process.poll() is None
            current_source = self.current_source
            active_roi = dict(self.roi) if self.roi else None
            active_scale = self.resolution_scale
            resolution = resolution_info_for(current_source, active_roi, active_scale) if current_source else None
            return {
                "running": running,
                "switching": self.switching,
                "current": str(current_source) if current_source else "",
                "message": self.last_message,
                "sourceMode": SOURCE_MODE,
                "wsPort": WS_PORT,
                "ctrlPort": CTRL_PORT,
                "roi": active_roi,
                "resolutionScale": active_scale,
                "resolution": resolution,
            }

    def start_initial(self):
        self.stop_existing_gst_launches()
        source = resolve_pipeline(self.initial_pipeline)
        if not source:
            raise RuntimeError(f"Initial pipeline not found: {self.initial_pipeline}")
        self.switch_to(source)

    def terminate_child_locked(self):
        if not self.process or self.process.poll() is not None:
            return

        os.killpg(self.process.pid, signal.SIGTERM)
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(self.process.pid, signal.SIGKILL)
            self.process.wait(timeout=5)

    def switch_to(self, source_path):
        with self.lock:
            existing_process = self.process
            had_running_pipeline = existing_process is not None and existing_process.poll() is None

        preserved_model_states = fetch_active_model_states() if had_running_pipeline else {}

        with self.lock:
            self.switching = True
            active_roi = dict(self.roi) if self.roi else None
            active_scale = self.resolution_scale
            self.last_message = f"Switching to {source_path.stem}..."

        try:
            prepared = prepared_pipeline_file(source_path, active_roi, active_scale)
            env = os.environ.copy()
            env["PEK_CURRENT_PIPELINE"] = str(source_path)
            env["PEK_ONNXRUNTIME_ROOT"] = ONNXRUNTIME_ROOT

            with self.lock:
                self.terminate_child_locked()
                self.process = subprocess.Popen(
                    [str(PEK_MENU), str(prepared)],
                    cwd=str(ROOT),
                    env=env,
                    stdout=open("/tmp/pek-supervisor-pipeline.log", "ab"),
                    stderr=subprocess.STDOUT,
                    start_new_session=True,
                )
                self.current_source = source_path
                self.current_prepared = prepared
                self.last_message = f"Started {source_path.stem}."

            time.sleep(2.0)
            restore_model_states(preserved_model_states)

            with self.lock:
                if self.process and self.process.poll() is not None:
                    self.last_message = f"{source_path.stem} exited while starting."
                    return False, self.last_message
                return True, self.last_message
        except Exception as exc:
            with self.lock:
                self.last_message = f"Could not start {source_path.stem}: {exc}"
            return False, self.last_message
        finally:
            with self.lock:
                self.switching = False

    def restart(self):
        with self.lock:
            source = self.current_source

        if not source:
            return False, "No pipeline is currently selected."

        return self.switch_to(source)

    def set_pipeline_settings(self, roi=None, resolution_scale=None):
        normalised = normalise_roi(roi) if roi else None
        with self.lock:
            source = self.current_source
            previous_roi = self.roi
            previous_scale = self.resolution_scale
            self.roi = normalised
            self.resolution_scale = normalise_resolution_scale(resolution_scale, self.resolution_scale)

        if not source:
            with self.lock:
                self.roi = previous_roi
                self.resolution_scale = previous_scale
            return False, "No pipeline is currently selected."

        ok, message = self.switch_to(source)
        if not ok:
            with self.lock:
                self.roi = previous_roi
                self.resolution_scale = previous_scale
            self.switch_to(source)
        return ok, message

    def clear_roi(self, resolution_scale=None):
        with self.lock:
            source = self.current_source
            previous_roi = self.roi
            previous_scale = self.resolution_scale
            had_roi = previous_roi is not None
            self.roi = None
            self.resolution_scale = normalise_resolution_scale(resolution_scale, self.resolution_scale)
            scale_changed = self.resolution_scale != previous_scale

        if not source:
            with self.lock:
                self.roi = previous_roi
                self.resolution_scale = previous_scale
            return False, "No pipeline is currently selected."
        if not had_roi and not scale_changed:
            return True, "No crop is active."

        ok, message = self.switch_to(source)
        if not ok:
            with self.lock:
                self.roi = previous_roi
                self.resolution_scale = previous_scale
            self.switch_to(source)
        return ok, message


SUPERVISOR = None


class ReusableThreadingHTTPServer(ThreadingHTTPServer):
    allow_reuse_address = True


class Handler(BaseHTTPRequestHandler):
    server_version = "PekWebUISupervisor/0.1"

    def log_message(self, fmt, *args):
        print(f"[supervisor] {self.address_string()} - {fmt % args}", flush=True)

    def read_json_payload(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode("utf-8") if length else "{}"
        return json.loads(body)

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)

        if parsed.path == "/ctrl-ws":
            self.proxy_websocket("127.0.0.1", CTRL_PORT, "/ws")
            return

        if parsed.path in ("/chrome-reset", "/chrome-reset.html"):
            chrome_reset_response(self)
            return

        if parsed.path == "/pek-config.js":
            text_response(
                self,
                (
                    f"window.PEK_CONFIG = {{ wsPort: {WS_PORT}, ctrlPort: {CTRL_PORT}, "
                    "ctrlProxyPath: '/ctrl-ws', supervised: true, roiPipeline: true };"
                ),
                content_type="application/javascript",
            )
            return

        if parsed.path == "/api/pipelines":
            pipelines, current = list_pipelines(SUPERVISOR.status()["current"])
            json_response(self, {"pipelines": pipelines, "current": current})
            return

        if parsed.path == "/api/supervisor/status":
            json_response(self, SUPERVISOR.status())
            return

        if parsed.path == "/api/ctrl-snapshot":
            try:
                json_response(self, fetch_ctrl_snapshot())
            except Exception as exc:
                json_response(self, {"error": str(exc)}, status=502)
            return

        if parsed.path == "/api/model-info":
            query = urllib.parse.parse_qs(parsed.query)
            name = query.get("name", [""])[0]
            if not name:
                json_response(self, {"error": "missing 'name' query parameter"}, status=400)
                return
            json_response(self, model_info(name))
            return

        self.serve_static(parsed.path)

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/api/pipeline-restart":
            ok, message = SUPERVISOR.restart()
            json_response(
                self,
                {
                    "available": True,
                    "complete": ok,
                    "message": message,
                },
                status=200 if ok else 500,
            )
            return

        if parsed.path == "/api/pipeline-roi":
            try:
                payload = self.read_json_payload()
                roi = payload.get("roi")
                resolution_scale = payload.get("resolutionScale", payload.get("scale"))
                if roi is None or payload.get("enabled") is False:
                    ok, message = SUPERVISOR.clear_roi(resolution_scale)
                else:
                    ok, message = SUPERVISOR.set_pipeline_settings(roi, resolution_scale)
            except json.JSONDecodeError:
                json_response(self, {"available": False, "message": "Invalid crop request."}, status=400)
                return
            except ValueError as exc:
                json_response(self, {"available": False, "message": str(exc)}, status=400)
                return

            status_payload = SUPERVISOR.status()
            json_response(
                self,
                {
                    "available": True,
                    "complete": ok,
                    "roi": status_payload.get("roi"),
                    "resolutionScale": status_payload.get("resolutionScale"),
                    "resolution": status_payload.get("resolution"),
                    "message": message,
                },
                status=200 if ok else 500,
            )
            return

        if parsed.path != "/api/pipeline-switch":
            json_response(self, {"error": "not found"}, status=404)
            return

        try:
            payload = self.read_json_payload()
        except json.JSONDecodeError:
            json_response(self, {"available": False, "message": "Invalid switch request."}, status=400)
            return

        source = resolve_pipeline(payload.get("pipeline", ""))
        if not source:
            json_response(self, {"available": False, "message": "Pipeline preset not found."}, status=404)
            return

        ok, message = SUPERVISOR.switch_to(source)
        json_response(
            self,
            {
                "available": True,
                "complete": ok,
                "pipeline": source.relative_to(PIPELINES_ROOT).with_suffix("").as_posix()
                if source.is_relative_to(PIPELINES_ROOT)
                else str(source),
                "message": message,
            },
            status=200 if ok else 500,
        )

    def serve_static(self, path):
        route = "/index.html" if path in ("", "/") else path
        candidate = (STATIC_ROOT / route.lstrip("/")).resolve()
        static_root = STATIC_ROOT.resolve()

        if static_root not in candidate.parents and candidate != static_root:
            json_response(self, {"error": "not found"}, status=404)
            return

        if not candidate.exists() or not candidate.is_file():
            json_response(self, {"error": "not found"}, status=404)
            return

        content_type, _ = mimetypes.guess_type(candidate)
        data = candidate.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", content_type or "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        self.end_headers()
        self.wfile.write(data)

    def proxy_websocket(self, target_host, target_port, target_path):
        if self.headers.get("Upgrade", "").lower() != "websocket":
            json_response(self, {"error": "websocket upgrade required"}, status=400)
            return

        try:
            upstream = socket.create_connection((target_host, target_port), timeout=5)
        except OSError:
            json_response(self, {"error": "control websocket unavailable"}, status=502)
            return

        self.close_connection = True
        try:
            request_lines = [
                f"GET {target_path} HTTP/1.1",
                f"Host: {target_host}:{target_port}",
            ]

            hop_headers = {"host", "connection", "upgrade", "proxy-connection"}
            for key, value in self.headers.items():
                if key.lower() in hop_headers:
                    continue
                request_lines.append(f"{key}: {value}")

            request_lines.extend([
                "Upgrade: websocket",
                "Connection: Upgrade",
                "",
                "",
            ])
            upstream.sendall("\r\n".join(request_lines).encode("utf-8"))

            response = bytearray()
            while b"\r\n\r\n" not in response:
                chunk = upstream.recv(4096)
                if not chunk:
                    raise ConnectionError("upstream websocket closed during handshake")
                response.extend(chunk)

            self.connection.sendall(response)
            self._tunnel_sockets(self.connection, upstream)
        except Exception as exc:
            print(f"[supervisor] websocket proxy closed: {exc}", flush=True)
        finally:
            upstream.close()

    def _tunnel_sockets(self, client, upstream):
        client.setblocking(False)
        upstream.setblocking(False)
        sockets = [client, upstream]

        while True:
            readable, _, errored = select.select(sockets, [], sockets, 60)
            if errored:
                return
            if not readable:
                continue

            for source in readable:
                target = upstream if source is client else client
                try:
                    data = source.recv(65536)
                except OSError:
                    return
                if not data:
                    return
                try:
                    target.sendall(data)
                except OSError:
                    return


def main():
    parser = argparse.ArgumentParser(description="Keep PEK WebUI alive while switching child pipelines.")
    parser.add_argument("pipeline", help="Initial pipeline ID or JSON path.")
    args = parser.parse_args()

    global SUPERVISOR
    SUPERVISOR = PipelineSupervisor(args.pipeline)
    server = ReusableThreadingHTTPServer((HOST, HTTP_PORT), Handler)
    SUPERVISOR.start_initial()
    print(f"[supervisor] serving WebUI on http://{HOST}:{HTTP_PORT}", flush=True)
    try:
        server.serve_forever()
    finally:
        with SUPERVISOR.lock:
            SUPERVISOR.terminate_child_locked()


if __name__ == "__main__":
    main()
