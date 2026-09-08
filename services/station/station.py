#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Read-only satellite gallery and durable single-consumer queue. Python >= 3.5.

No shell commands or processing options are accepted from image sidecars or HTTP.
Only the administrator-owned station.json selects native processing commands.
"""
from __future__ import print_function
import argparse
import fnmatch
import hashlib
import json
import logging
import os
import re
import shutil
import signal
import socketserver
import sqlite3
import subprocess
import sys
import threading
import time
import warnings
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit

LOG = logging.getLogger("satdump-station")
APP = Path(__file__).resolve().parent
STOP = threading.Event()
IMAGE_EXT = (".png", ".jpg", ".jpeg")
SCHEMAS = ("satdump.presentation/1", "satdump.presentation/2")


class InputChanged(ValueError):
    pass


def read_json(path, limit=2 * 1024 * 1024):
    with open(str(path), "rb") as stream:
        raw = stream.read(limit + 1)
    if len(raw) > limit:
        raise ValueError("JSON exceeds size limit: " + str(path))
    return json.loads(raw.decode("utf-8"))


def atomic_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = str(path) + ".tmp-" + str(os.getpid())
    try:
        with open(tmp, "w", encoding="utf-8") as stream:
            json.dump(value, stream, ensure_ascii=False, sort_keys=True, allow_nan=False)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(tmp, 0o644)
        os.replace(tmp, str(path))
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


def contained(path, root):
    return os.path.commonpath([os.path.realpath(str(path)), os.path.realpath(str(root))]) == os.path.realpath(str(root))


def load_config(path):
    cfg = read_json(path)
    if cfg.get("schema") != "satdump.station/1":
        raise ValueError("Unsupported station configuration schema")
    data = Path(cfg["data_dir"])
    if not data.is_absolute() or str(data) in ("/", "/var", "/opt", "/home", "/usr"):
        raise ValueError("data_dir must be a dedicated absolute directory")
    for key, default, minimum in (("poll_seconds", 10, 1), ("settle_seconds", 30, 0),
                                  ("timeout_seconds", 1800, 1), ("max_attempts", 3, 1),
                                  ("max_items", 1000, 1), ("min_free_mb", 1024, 0),
                                  ("max_input_mb", 32768, 1), ("max_log_mb", 32, 1)):
        cfg[key] = int(cfg.get(key, default))
        if cfg[key] < minimum:
            raise ValueError("Invalid " + key)
    ids = set()
    for source in cfg["sources"]:
        if not re.match(r"^[a-zA-Z0-9_-]+$", source["id"]) or source["id"] in ids:
            raise ValueError("Source ids must be unique safe identifiers")
        ids.add(source["id"])
        if source["kind"] not in ("image", "product", "pipeline"):
            raise ValueError("Unknown source kind")
        root = Path(source["path"])
        if not root.is_absolute() or contained(data / "public", root) or contained(root, data / "public"):
            raise ValueError("Input and public trees must not overlap")
        for private in ("work", "state", "logs", "archive"):
            if contained(root, data / private) or contained(data / private, root):
                raise ValueError("Input and worker state trees must not overlap")
        if source["kind"] == "pipeline" and source.get("enabled", True):
            if not re.match(r"^[a-zA-Z0-9_-]+$", source["pipeline"]):
                raise ValueError("Invalid pipeline id")
            if not isinstance(source.get("options", []), list) or not all(isinstance(x, str) for x in source.get("options", [])):
                raise ValueError("options must be an argv array, not shell text")
            if not source.get("input_level"):
                raise ValueError("pipeline requires input_level")
    cfg["_config_dir"] = str(Path(path).resolve().parent)
    return cfg


def source_files(path, kind):
    """Return a deterministic input inventory; reject symlinks and unfinished trees."""
    path = Path(path)
    if path.is_symlink():
        raise ValueError("Symlink input rejected")
    if kind != "product":
        files = [path]
        sidecar = path.with_suffix(".json")
        if sidecar.is_file():
            files.append(sidecar)
    else:
        files = []
        for base, dirs, names in os.walk(str(path), followlinks=False):
            for name in dirs + names:
                if (Path(base) / name).is_symlink():
                    raise ValueError("Symlink in product tree")
            for name in names:
                if name.endswith((".part", ".tmp", ".partial")):
                    raise ValueError("Unfinished transfer in product tree")
                if name != ".ready":
                    files.append(Path(base) / name)
    files = sorted(files, key=str)
    if len(files) > 10000:
        raise ValueError("More than 10000 files in one input")
    if any(x.is_symlink() for x in files):
        raise ValueError("Symlink input or sidecar rejected")
    if not files or any(not x.is_file() for x in files):
        raise ValueError("Empty or incomplete input")
    return files


def inventory(path, kind, max_bytes):
    files = source_files(path, kind)
    base = path if kind == "product" else path.parent
    result, total = [], 0
    for file_path in files:
        stat = file_path.stat()
        total += stat.st_size
        result.append((str(file_path.relative_to(base)), stat.st_size, stat.st_mtime_ns))
    if total > max_bytes:
        raise ValueError("Input exceeds max_input_mb")
    signature = hashlib.sha256(json.dumps(result, ensure_ascii=True).encode("ascii")).hexdigest()
    return signature, files, total


def content_id(files, base, recipe, progress=None):
    digest = hashlib.sha256(json.dumps(recipe, sort_keys=True, ensure_ascii=True).encode("ascii"))
    last = time.monotonic()
    for path in files:
        digest.update(str(path.relative_to(base)).encode("utf-8"))
        digest.update(b"\x00")
        with open(str(path), "rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
                if progress is not None and time.monotonic() - last > 5:
                    progress()
                    last = time.monotonic()
                if STOP.is_set():
                    raise RuntimeError("Interrupted")
        digest.update(b"\x00")
    return digest.hexdigest()


def candidate_paths(source):
    root = Path(source["path"])
    if not root.is_dir():
        raise OSError("Input directory is not mounted or not accessible: " + str(root))
    if source["kind"] == "product":
        # One immediate child = one complete pass. The .ready marker belongs here.
        paths = sorted([p for p in root.iterdir() if p.is_dir() and not p.name.startswith(".")])
    else:
        patterns = source.get("patterns", ["*.png", "*.jpg", "*.jpeg"])
        paths = []
        for base, dirs, names in os.walk(str(root), followlinks=False):
            dirs[:] = sorted(d for d in dirs if not d.startswith(".") and not (Path(base) / d).is_symlink())
            paths.extend(Path(base) / n for n in sorted(names)
                         if not n.startswith(".") and any(fnmatch.fnmatch(n.lower(), pat.lower()) for pat in patterns)
                         and not n.endswith((".part", ".tmp", ".partial")))
    for path in paths:
        ready = path / ".ready" if source["kind"] == "product" else Path(str(path) + ".ready")
        if source.get("require_ready", source["kind"] == "product") and not ready.is_file():
            continue
        if source["kind"] == "product" and not any(path.rglob("product.cbor")):
            continue
        yield path


class Worker:
    def __init__(self, cfg):
        self.cfg = cfg
        self.data = Path(cfg["data_dir"])
        for name in ("state", "work", "logs", "archive", "public/items"):
            (self.data / name).mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(str(self.data / "state/queue.sqlite3"), timeout=30)
        self.db.row_factory = sqlite3.Row
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA synchronous=FULL")
        self.db.execute("""CREATE TABLE IF NOT EXISTS jobs (
            id TEXT PRIMARY KEY, source TEXT NOT NULL, path TEXT NOT NULL,
            signature TEXT NOT NULL, state TEXT NOT NULL, attempts INTEGER NOT NULL DEFAULT 0,
            next_at REAL NOT NULL DEFAULT 0, error TEXT NOT NULL DEFAULT '', created REAL NOT NULL,
            finished REAL)""")
        self.db.execute("CREATE INDEX IF NOT EXISTS jobs_input ON jobs(source,path,signature)")
        self.db.execute("CREATE INDEX IF NOT EXISTS jobs_ready ON jobs(state,next_at,created)")
        self.db.commit()
        self.seen = {}
        self.source_errors = {}
        self.sources = {s["id"]: s for s in cfg["sources"] if s.get("enabled", True)}
        patch_path = Path(cfg["_config_dir"]) / cfg.get("processing_config", "processing.json")
        self.patch = read_json(patch_path)
        self.patch.setdefault("satdump_general", {})["tle_update_interval"] = {"value": "Never"}
        self.patch["satdump_general"]["log_to_file"] = {"value": False}
        self.engine = str(Path(cfg.get("engine", "/opt/satdump-station/current/engine/satdump")).resolve())

    def fingerprint(self, source, signature):
        recipe = {"source": source, "processing": self.patch,
                  "revision": self.cfg.get("processing_revision", "1"), "input": signature}
        return hashlib.sha256(json.dumps(recipe, sort_keys=True, ensure_ascii=True).encode("ascii")).hexdigest()

    def close(self):
        self.db.close()

    def recover(self):
        # Called only after the process lock has been acquired by main().
        self.db.execute("UPDATE jobs SET state='pending', next_at=0 WHERE state='running'")
        self.db.commit()
        self.catalog()

    def scan(self):
        for source_id, source in sorted(self.sources.items()):
            try:
                candidates = list(candidate_paths(source))
                self.source_errors.pop(source_id, None)
            except OSError as error:
                self.source_errors[source_id] = "unavailable"
                LOG.warning("%s: %s", source_id, error)
                continue
            for path in candidates:
                if STOP.is_set():
                    return
                key = (source_id, str(path))
                try:
                    sig, files, total = inventory(path, source["kind"], self.cfg["max_input_mb"] * 1024 ** 2)
                    sig = self.fingerprint(source, sig)
                    previous = self.seen.get(key)
                    now = time.monotonic()
                    if previous is None or previous[0] != sig:
                        self.seen[key] = (sig, now)
                        continue
                    if now - previous[1] < self.cfg["settle_seconds"]:
                        continue
                    known = self.db.execute("SELECT 1 FROM jobs WHERE source=? AND path=? AND signature=?",
                                            (source_id, str(path), sig)).fetchone()
                    if known:
                        continue
                    if shutil.disk_usage(str(self.data)).free < total * 2 + self.cfg["min_free_mb"] * 1024 ** 2:
                        self.source_errors[source_id] = "low_disk"
                        continue
                    base = path if source["kind"] == "product" else path.parent
                    recipe = {"source": source, "processing": self.patch,
                              "revision": self.cfg.get("processing_revision", "1")}
                    job_id = content_id(files, base, recipe, lambda: self.heartbeat("hashing"))
                    if self.fingerprint(source, inventory(path, source["kind"], self.cfg["max_input_mb"] * 1024 ** 2)[0]) != sig:
                        self.seen.pop(key, None)
                        continue
                    self.db.execute("INSERT OR IGNORE INTO jobs(id,source,path,signature,state,created) VALUES(?,?,?,?,?,?)",
                                    (job_id, source_id, str(path), sig, "pending", time.time()))
                    # Same content touched/renamed: remember this fingerprint without reprocessing.
                    self.db.execute("UPDATE jobs SET path=?, signature=? WHERE id=? AND state='done'",
                                    (str(path), sig, job_id))
                    self.db.commit()
                except (OSError, ValueError) as error:
                    LOG.warning("Skipped %s: %s", path, error)
        # Bound observations for long-running receivers with rotating input folders.
        if len(self.seen) > 20000:
            self.seen = {k: v for k, v in self.seen.items() if Path(k[1]).exists()}

    def native(self, argv, work, log_path):
        env = os.environ.copy()
        env.update({"HOME": str(work / "home"), "XDG_CONFIG_HOME": str(work / "home/.config"),
                    "OMP_NUM_THREADS": str(self.cfg.get("native_threads", 2))})
        # Do not leak the embedded Python loader into the independent SatDump runtime.
        for name in ("PYTHONHOME", "PYTHONPATH", "LD_LIBRARY_PATH"):
            env.pop(name, None)
        start = time.monotonic()
        with open(str(log_path), "ab", buffering=0) as log:
            log.write((json.dumps(argv, ensure_ascii=True) + "\n").encode("ascii"))
            proc = subprocess.Popen(argv, cwd=str(work), env=env, stdout=log, stderr=subprocess.STDOUT,
                                    stdin=subprocess.DEVNULL, start_new_session=True)
            try:
                while proc.poll() is None:
                    self.heartbeat("processing")
                    if STOP.is_set() or time.monotonic() - start > self.cfg["timeout_seconds"]:
                        raise RuntimeError("Processing interrupted or timed out")
                    if log_path.stat().st_size > self.cfg["max_log_mb"] * 1024 ** 2:
                        raise RuntimeError("Processing log exceeded limit")
                    STOP.wait(1)
                if proc.returncode:
                    raise RuntimeError("SatDump exit code " + str(proc.returncode))
            finally:
                if proc.poll() is None:
                    os.killpg(proc.pid, signal.SIGTERM)
                    try:
                        proc.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        os.killpg(proc.pid, signal.SIGKILL)
                        proc.wait()

    def process(self, job):
        job_id = job["id"]
        final = self.data / "public/items" / job_id
        if (final / "item.json").is_file():
            if read_json(final / "item.json")["job_id"] == job_id:
                return  # Recovery after atomic publication but before DB commit.
        source = self.sources[job["source"]]
        path = Path(job["path"])
        ready = path / ".ready" if source["kind"] == "product" else Path(str(path) + ".ready")
        if source.get("require_ready", source["kind"] == "product") and not ready.is_file():
            raise RuntimeError("Input readiness marker was removed")
        sig, files, total = inventory(path, source["kind"], self.cfg["max_input_mb"] * 1024 ** 2)
        sig = self.fingerprint(source, sig)
        if sig != job["signature"]:
            raise InputChanged("Input changed; wait for a new stable snapshot")
        if shutil.disk_usage(str(self.data)).free < total * 2 + self.cfg["min_free_mb"] * 1024 ** 2:
            raise RuntimeError("Insufficient free space")
        work = self.data / "work" / job_id
        if work.exists():
            shutil.rmtree(str(work))
        incoming = work / "input"
        incoming.mkdir(parents=True)
        file_time = path.stat().st_mtime
        base = path if source["kind"] == "product" else path.parent
        last = time.monotonic()
        for original in files:
            target = incoming / original.relative_to(base)
            target.parent.mkdir(parents=True, exist_ok=True)
            with open(str(original), "rb") as src, open(str(target), "wb") as dst:
                for block in iter(lambda: src.read(4 * 1024 * 1024), b""):
                    dst.write(block)
                    if STOP.is_set():
                        raise RuntimeError("Interrupted while copying")
                    if time.monotonic() - last > 5:
                        self.heartbeat("copying")
                        last = time.monotonic()
        if self.fingerprint(source, inventory(path, source["kind"], self.cfg["max_input_mb"] * 1024 ** 2)[0]) != sig:
            raise InputChanged("Input changed while copying")
        copied = [incoming / original.relative_to(base) for original in files]
        recipe = {"source": source, "processing": self.patch,
                  "revision": self.cfg.get("processing_revision", "1")}
        if content_id(copied, incoming, recipe, lambda: self.heartbeat("verifying")) != job_id:
            raise InputChanged("Copied bytes differ from the queued input")
        settings = work / "home/.config/satdump/settings.json"
        atomic_json(settings, self.patch)
        patch_path = work / "processing.json"
        atomic_json(patch_path, self.patch)
        result = incoming
        log_path = self.data / "logs" / (job_id + ".log")
        if log_path.exists():
            os.replace(str(log_path), str(log_path) + ".previous")
        if source["kind"] == "product":
            products = sorted(incoming.rglob("product.cbor"))
            for product in products:
                # Never allow stale annotated copies to masquerade as fresh successful output.
                for old in product.parent.glob("*_annotated*"):
                    if old.is_file():
                        old.unlink()
                self.native([self.engine, "reprocess", str(product.parent), str(patch_path)], work, log_path)
        elif source["kind"] == "pipeline":
            result = work / "decoded"
            argv = [self.engine, source["pipeline"], source["input_level"], str(incoming / path.name), str(result)]
            argv += source.get("options", [])
            argv += ["--offline", "--processing_config", str(patch_path)]
            self.native(argv, work, log_path)
        images = sorted(p for p in result.rglob("*") if p.suffix.lower() in IMAGE_EXT and p.is_file())
        if source["kind"] != "image":
            images = [p for p in images if p.name.endswith("_annotated_presentation.png")]
            if not images:
                raise RuntimeError("No native presentation PNGs; check channels, autogen presets and SatDump log")
        else:
            images = [incoming / path.name]
        if len(images) > 500:
            raise ValueError("Too many products in one pass")
        publication = work / "publication"
        publication.mkdir()
        entries = []
        for index, image in enumerate(images):
            entries.append(self.prepare_image(image, publication, job_id, index, source,
                                              file_time, source["kind"] != "image"))
        if not entries:
            raise RuntimeError("No publishable products")
        atomic_json(publication / "item.json", {"job_id": job_id, "entries": entries})
        # Scientific/decoded files remain private and are retained independently of the website.
        archive = self.data / "archive" / job_id
        if self.cfg.get("archive_science", True) and not archive.exists():
            os.rename(str(result), str(archive))
        # All files were written before the only directory rename visible to the web server.
        os.rename(str(publication), str(final))
        if not self.cfg.get("keep_work", False):
            shutil.rmtree(str(work))

    def prepare_image(self, source_path, dest, job_id, index, source, file_time, native):
        from PIL import Image
        Image.MAX_IMAGE_PIXELS = int(self.cfg.get("max_image_pixels", 90000000))
        metadata = {}
        sidecar = source_path.with_suffix(".json")
        if sidecar.exists():
            metadata = read_json(sidecar)
            if not isinstance(metadata, dict):
                raise ValueError("Sidecar must be an object")
        if native and metadata.get("schema") not in SCHEMAS:
            raise ValueError("Native presentation passport is missing or invalid")
        pass_info = metadata.get("pass", {}) if isinstance(metadata.get("pass", {}), dict) else {}
        stem = "{0:03d}".format(index)
        with warnings.catch_warnings():
            warnings.simplefilter("error", Image.DecompressionBombWarning)
            with Image.open(str(source_path)) as image:
                if image.format not in ("PNG", "JPEG"):
                    raise ValueError("Only real PNG/JPEG images may be published")
                image.load()
                width, height = image.size
                if not width or not height:
                    raise ValueError("Empty image")
                original_name = stem + (".png" if image.format == "PNG" else ".jpg")
                shutil.copyfile(str(source_path), str(dest / original_name))
                # Display products only. Do not change the original scientific raster or its sidecar.
                if image.mode in ("RGBA", "LA") or "transparency" in image.info:
                    rgba = image.convert("RGBA")
                    view = Image.new("RGB", image.size, (14, 22, 36))
                    view.paste(rgba, mask=rgba.split()[-1])
                else:
                    view = image.convert("RGB")
                resample = getattr(getattr(Image, "Resampling", Image), "LANCZOS")
                view.thumbnail((1920, 1080), resample)
                view.save(str(dest / (stem + "-preview.jpg")), "JPEG", quality=90)
                view.thumbnail((480, 270), resample)
                view.save(str(dest / (stem + "-thumb.jpg")), "JPEG", quality=82)
        atomic_json(dest / (stem + ".json"), metadata)
        prefix = "items/" + job_id + "/"
        return {"id": job_id + "-" + stem, "source": source["id"],
                "satellite": str(pass_info.get("satellite", source.get("satellite", "Источник не указан")))[:240],
                "instrument": str(pass_info.get("instrument", source.get("instrument", "")))[:240],
                "title": str(pass_info.get("product", source_path.stem))[:400],
                "acquisition_time": str(pass_info.get("acquisition_time", ""))[:240],
                "file_mtime": file_time, "published_at": time.time(),
                "width": width, "height": height, "native_presentation": metadata.get("schema") in SCHEMAS,
                "preview": prefix + stem + "-preview.jpg", "thumbnail": prefix + stem + "-thumb.jpg",
                "original": prefix + original_name, "metadata": prefix + stem + ".json"}

    def catalog(self):
        entries = []
        for manifest in sorted((self.data / "public/items").glob("*/item.json")):
            try:
                entries.extend(read_json(manifest)["entries"])
            except (OSError, ValueError, KeyError):
                LOG.exception("Invalid publication %s", manifest)
        entries.sort(key=lambda x: (x["published_at"], x["id"]), reverse=True)
        atomic_json(self.data / "public/catalog.json",
                    {"schema": "satdump.gallery/1", "title": self.cfg.get("title", "Спутниковые наблюдения"),
                     "updated_at": time.time(), "total": len(entries), "items": entries[:self.cfg["max_items"]]})

    def heartbeat(self, phase="idle"):
        counts = dict(self.db.execute("SELECT state, count(*) FROM jobs GROUP BY state").fetchall())
        atomic_json(self.data / "public/worker.json", {"updated_at": time.time(), "phase": phase,
                    "queue": counts, "sources": self.source_errors,
                    "stale_after": max(60, self.cfg["poll_seconds"] * 4)})

    def tick(self):
        self.heartbeat("scanning")
        self.scan()
        job = self.db.execute("SELECT * FROM jobs WHERE state='pending' AND next_at<=? ORDER BY created LIMIT 1",
                              (time.time(),)).fetchone()
        if job:
            self.db.execute("UPDATE jobs SET state='running',attempts=attempts+1 WHERE id=?", (job["id"],))
            self.db.commit()
            try:
                self.process(job)
                self.db.execute("UPDATE jobs SET state='done',error='',finished=? WHERE id=?", (time.time(), job["id"]))
                LOG.info("Published %s", job["id"])
            except Exception as error:
                LOG.exception("Processing failed: %s", job["id"])
                attempts = job["attempts"] + 1
                state = "superseded" if isinstance(error, InputChanged) else ("failed" if attempts >= self.cfg["max_attempts"] else "pending")
                self.db.execute("UPDATE jobs SET state=?,error=?,next_at=? WHERE id=?",
                                (state, str(error)[:2000], time.time() + min(3600, 30 * 2 ** attempts), job["id"]))
            self.db.commit()
            self.catalog()
        self.heartbeat()
        return bool(job)


class GalleryHandler(BaseHTTPRequestHandler):
    server_version = "SatDumpGallery"
    sys_version = ""

    def setup(self):
        BaseHTTPRequestHandler.setup(self)
        self.connection.settimeout(15)

    def log_message(self, fmt, *args):
        LOG.info("HTTP %s %s", self.address_string(), (fmt % args).replace("\n", " "))

    def do_HEAD(self):
        self.respond(False)

    def do_GET(self):
        self.respond(True)

    def do_POST(self):
        self.send_error(405, "Read-only service")

    def respond(self, body):
        self.connection.settimeout(15)
        path = unquote(urlsplit(self.path).path)
        if "\x00" in path or "\\" in path or ".." in path.split("/"):
            self.send_error(404)
            return
        if path == "/health.json":
            try:
                health = read_json(self.server.public / "worker.json")
                health["worker_alive"] = time.time() - health["updated_at"] < health["stale_after"]
            except (OSError, ValueError, KeyError):
                health = {"worker_alive": False}
            health["web_alive"] = True
            self.send_bytes(json.dumps(health).encode("utf-8"), "application/json", body)
            return
        static = {"/": "index.html", "/index.html": "index.html", "/app.js": "app.js", "/style.css": "style.css"}
        if path in static:
            root, relative = APP / "web", static[path]
        elif path in ("/catalog.json", "/worker.json") or re.match(r"^/items/[0-9a-f]{64}/[0-9]{3}(?:-preview|-thumb)?\.(?:png|jpg|json)$", path):
            root, relative = self.server.public, path.lstrip("/")
        else:
            self.send_error(404)
            return
        target = root / relative
        if not contained(target, root) or not target.is_file():
            self.send_error(404)
            return
        content_types = {".html": "text/html; charset=utf-8", ".js": "application/javascript; charset=utf-8",
                         ".css": "text/css; charset=utf-8", ".json": "application/json; charset=utf-8",
                         ".jpg": "image/jpeg", ".png": "image/png"}
        try:
            with open(str(target), "rb") as stream:
                size = os.fstat(stream.fileno()).st_size
                self.headers_ok(size, content_types[target.suffix], path.startswith("/items/"))
                if body:
                    shutil.copyfileobj(stream, self.wfile, 128 * 1024)
        except (BrokenPipeError, ConnectionResetError, TimeoutError):
            pass

    def headers_ok(self, size, mime, immutable=False):
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(size))
        self.send_header("Cache-Control", "public, max-age=31536000, immutable" if immutable else "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'self'")
        self.end_headers()

    def send_bytes(self, data, mime, body):
        self.headers_ok(len(data), mime)
        if body:
            self.wfile.write(data)


class GalleryServer(socketserver.ThreadingMixIn, HTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address, public):
        self.public = Path(public).resolve()
        self.slots = threading.BoundedSemaphore(24)
        HTTPServer.__init__(self, address, GalleryHandler)

    def process_request(self, request, address):
        if not self.slots.acquire(False):
            request.close()
            return
        try:
            socketserver.ThreadingMixIn.process_request(self, request, address)
        except Exception:
            self.slots.release()
            raise

    def process_request_thread(self, request, address):
        try:
            socketserver.ThreadingMixIn.process_request_thread(self, request, address)
        finally:
            self.slots.release()


def main():
    parser = argparse.ArgumentParser(description="SatDump: очередь, обработка и локальная галерея")
    parser.add_argument("command", choices=("worker", "once", "serve", "check", "status", "retry"))
    parser.add_argument("--config", default="/etc/satdump-station/station.json")
    parser.add_argument("--public", default="/var/lib/satdump-station/public")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8090)
    parser.add_argument("--job", default="")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    if args.command == "serve":
        if not 1 <= args.port <= 65535:
            parser.error("port must be 1..65535")
        server = GalleryServer((args.host, args.port), args.public)
        try:
            server.serve_forever(poll_interval=0.5)
        except KeyboardInterrupt:
            pass
        finally:
            server.server_close()
        return 0
    cfg = load_config(args.config)
    if args.command == "check":
        from PIL import Image
        print("Configuration OK; Python " + sys.version.split()[0] + "; Pillow " + str(getattr(Image, "__version__", "available")))
        engine = cfg.get("engine", "/opt/satdump-station/current/engine/satdump")
        if not os.access(engine, os.X_OK):
            raise ValueError("SatDump launcher is not executable: " + engine)
        subprocess.check_call([engine, "version"])
        return 0
    if args.command in ("status", "retry"):
        db = sqlite3.connect(str(Path(cfg["data_dir"]) / "state/queue.sqlite3"), timeout=30)
        try:
            if args.command == "retry":
                if not re.match(r"^[0-9a-f]{64}$", args.job):
                    parser.error("retry requires --job <64-character id>")
                cursor = db.execute("UPDATE jobs SET state='pending',attempts=0,next_at=0,error='' WHERE id=? AND state='failed'", (args.job,))
                db.commit()
                if not cursor.rowcount:
                    raise ValueError("Failed job not found")
            for row in db.execute("SELECT id,state,attempts,error FROM jobs ORDER BY created DESC LIMIT 30"):
                print(json.dumps(list(row), ensure_ascii=False))
        finally:
            db.close()
        return 0
    import fcntl
    state = Path(cfg["data_dir"]) / "state"
    state.mkdir(parents=True, exist_ok=True)
    with open(str(state / "worker.lock"), "a") as lock:
        try:
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            raise RuntimeError("Another worker is already running")
        STOP.clear()
        for sig in (signal.SIGINT, signal.SIGTERM):
            signal.signal(sig, lambda *_: STOP.set())
        worker = Worker(cfg)
        try:
            worker.recover()
            if args.command == "once":
                worker.scan()
                STOP.wait(cfg["settle_seconds"])
            while not STOP.is_set():
                busy = worker.tick()
                if args.command == "once":
                    break
                if not busy:
                    STOP.wait(cfg["poll_seconds"])
        finally:
            worker.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        LOG.error("%s", exc)
        sys.exit(1)
