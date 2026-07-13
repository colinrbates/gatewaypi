#!/usr/bin/env python3
"""GatewayPi web upload page.

Serves a phone-friendly page on port 8080 for pushing .nam captures and IR
.wav files into the GatewayPi library over the local network.  Supports
single files and whole folders (with subfolders).  Standard library only.
"""

import email.parser
import email.policy
import html
import json
import os
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler

DATA = os.environ.get("GATEWAYPI_DATA", "/var/lib/gatewaypi")


def _resolve_dirs():
    """Captures/IR folders: config.json first, then env, then home defaults."""
    home = os.path.expanduser("~")
    captures = os.path.join(home, "Captures")
    irs = os.path.join(home, "IRs")
    cfg_path = os.path.join(DATA, "config.json")
    try:
        cfg = json.load(open(cfg_path))
        captures = cfg.get("capturesDir", captures)
        irs = cfg.get("irsDir", irs)
    except Exception:
        pass
    return (os.environ.get("GATEWAYPI_CAPTURES", captures),
            os.environ.get("GATEWAYPI_IRS", irs))


MODELS, IRS = _resolve_dirs()
PORT = 8080
MAX_UPLOAD = 500 * 1024 * 1024  # IR packs and capture folders can be large

PAGE = """<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>GatewayPi</title>
<style>
 body {{ font-family: system-ui, sans-serif; background:#17181a; color:#e9e6df;
        max-width: 520px; margin: 0 auto; padding: 1rem; }}
 h1 {{ color:#e29a3a; font-size:1.4rem; }}
 form {{ background:#1f2124; padding:1rem; border-radius:8px; margin:1rem 0; }}
 label {{ display:block; font-weight:700; margin:.3rem 0; }}
 input[type=file] {{ width:100%; margin:.5rem 0; color:#e9e6df; }}
 button {{ background:#e29a3a; color:#17181a; border:0; padding:.7rem 1.4rem;
          border-radius:6px; font-weight:700; font-size:1rem; }}
 ul {{ font-size:.85rem; color:#a3a19a; max-height:9rem; overflow:auto;
       background:#141517; border-radius:6px; padding:.5rem 1.2rem; }}
 .msg {{ background:#2a3d2e; padding:.6rem 1rem; border-radius:6px; }}
 small {{ color:#8a877f; }}
</style></head><body>
<h1>GatewayPi library</h1>
{msg}
<form method="post" enctype="multipart/form-data" action="/upload">
  <label>Add files &nbsp;<small>.nam &rarr; Captures &middot; .wav &rarr; IRs</small></label>
  <input type="file" name="files" multiple accept=".nam,.wav">
  <label>...or a whole folder <small>(subfolders kept)</small></label>
  <input type="file" name="files" multiple webkitdirectory directory>
  <button type="submit">Upload</button>
</form>
<strong>Captures ({nmodels})</strong><ul>{models}</ul>
<strong>IRs ({nirs})</strong><ul>{irs}</ul>
</body></html>"""


def listing(path):
    found = []
    for root, _dirs, files in os.walk(path):
        for n in files:
            if n.lower().endswith((".nam", ".wav")):
                rel = os.path.relpath(os.path.join(root, n), path)
                found.append(rel)
    found.sort()
    items = "".join(f"<li>{html.escape(n)}</li>" for n in found[:300])
    return len(found), items


def safe_relpath(name):
    """Keep subfolders from webkitRelativePath, but never escape the target."""
    name = name.replace("\\", "/")
    parts = [p for p in name.split("/") if p not in ("", ".", "..")]
    return os.path.join(*parts) if parts else ""


class Handler(BaseHTTPRequestHandler):
    server_version = "GatewayPi/1.0"

    def _page(self, msg=""):
        nmodels, models = listing(MODELS)
        nirs, irs = listing(IRS)
        body = PAGE.format(msg=msg, nmodels=nmodels, models=models,
                           nirs=nirs, irs=irs).encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self._page()
        else:
            self.send_error(404)

    def do_POST(self):
        if self.path != "/upload":
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length", 0))
        if length <= 0 or length > MAX_UPLOAD:
            self.send_error(413, "Upload too large")
            return

        ctype = self.headers.get("Content-Type", "")
        # Read the whole body, but tolerate a client that stalls: the socket
        # timeout turns a half-finished upload into an error instead of a hang.
        try:
            body = self.rfile.read(length)
        except (TimeoutError, OSError):
            self.close_connection = True
            return
        raw = f"Content-Type: {ctype}\r\nMIME-Version: 1.0\r\n\r\n".encode() + body
        message = email.parser.BytesParser(policy=email.policy.default).parsebytes(raw)

        saved, skipped = [], []
        for part in message.iter_parts():
            filename = part.get_filename()
            if not filename:
                continue
            rel = safe_relpath(filename)
            if not rel:
                continue
            payload = part.get_payload(decode=True) or b""
            lower = rel.lower()
            if lower.endswith(".nam"):
                dest = os.path.join(MODELS, rel)
            elif lower.endswith(".wav"):
                dest = os.path.join(IRS, rel)
            else:
                skipped.append(os.path.basename(rel))
                continue
            os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
            with open(dest, "wb") as f:
                f.write(payload)
            saved.append(rel)

        parts = []
        if saved:
            parts.append(f"Uploaded {len(saved)}: "
                         + ", ".join(html.escape(n) for n in saved[:20])
                         + (" ..." if len(saved) > 20 else ""))
        if skipped:
            parts.append("Skipped (not .nam/.wav): "
                         + ", ".join(html.escape(n) for n in skipped[:10]))
        msg = f'<div class="msg">{" &middot; ".join(parts) or "No files received"}</div>'
        self._page(msg)

    # Per-connection socket timeout so a stalled client can't wedge its
    # worker thread forever.
    timeout = 30

    def log_message(self, fmt, *args):
        pass  # journald noise reduction


def main():
    os.makedirs(MODELS, exist_ok=True)
    os.makedirs(IRS, exist_ok=True)
    # Threaded: one slow/stalled request can never block the whole server
    # (the single-threaded version could get wedged by a half-open upload).
    server = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    server.daemon_threads = True
    server.serve_forever()


if __name__ == "__main__":
    main()
