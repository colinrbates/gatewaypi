#!/usr/bin/env python3
"""GatewayPi web upload page.

Serves a phone-friendly page on port 8080 for pushing .nam models and IR
.wav files into the GatewayPi library over the local network.  Standard
library only — no dependencies to install.
"""

import email.parser
import email.policy
import html
import json
import os
from http.server import HTTPServer, BaseHTTPRequestHandler

DATA = os.environ.get("GATEWAYPI_DATA", "/var/lib/gatewaypi")
MODELS = os.path.join(DATA, "models")
IRS = os.path.join(DATA, "irs")
PORT = 8080
MAX_UPLOAD = 200 * 1024 * 1024  # generous — sticks of IR packs are big

PAGE = """<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>GatewayPi</title>
<style>
 body {{ font-family: system-ui, sans-serif; background:#17181a; color:#e9e6df;
        max-width: 480px; margin: 0 auto; padding: 1rem; }}
 h1 {{ color:#e29a3a; font-size:1.4rem; }}
 form {{ background:#1f2124; padding:1rem; border-radius:8px; margin:1rem 0; }}
 input[type=file] {{ width:100%; margin:.5rem 0; }}
 button {{ background:#e29a3a; color:#17181a; border:0; padding:.7rem 1.4rem;
          border-radius:6px; font-weight:700; font-size:1rem; }}
 ul {{ font-size:.85rem; color:#a3a19a; }}
 .msg {{ background:#2a3d2e; padding:.6rem 1rem; border-radius:6px; }}
</style></head><body>
<h1>GatewayPi library</h1>
{msg}
<form method="post" enctype="multipart/form-data" action="/upload">
  <strong>Add files</strong><br>
  <small>.nam &rarr; models &nbsp;&middot;&nbsp; .wav &rarr; IRs</small><br>
  <input type="file" name="files" multiple accept=".nam,.wav">
  <button type="submit">Upload</button>
</form>
<strong>Models ({nmodels})</strong><ul>{models}</ul>
<strong>IRs ({nirs})</strong><ul>{irs}</ul>
</body></html>"""


def listing(path):
    try:
        names = sorted(os.listdir(path))
    except OSError:
        names = []
    items = "".join(f"<li>{html.escape(n)}</li>" for n in names[:200])
    return len(names), items


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
        raw = (f"Content-Type: {ctype}\r\nMIME-Version: 1.0\r\n\r\n".encode()
               + self.rfile.read(length))
        message = email.parser.BytesParser(policy=email.policy.default).parsebytes(raw)

        saved, skipped = [], []
        for part in message.iter_parts():
            filename = part.get_filename()
            if not filename:
                continue
            # Never trust a client-supplied path.
            filename = os.path.basename(filename)
            payload = part.get_payload(decode=True) or b""
            lower = filename.lower()
            if lower.endswith(".nam"):
                dest = os.path.join(MODELS, filename)
            elif lower.endswith(".wav"):
                dest = os.path.join(IRS, filename)
            else:
                skipped.append(filename)
                continue
            with open(dest, "wb") as f:
                f.write(payload)
            saved.append(filename)

        parts = []
        if saved:
            parts.append("Uploaded: " + ", ".join(html.escape(n) for n in saved))
        if skipped:
            parts.append("Skipped (not .nam/.wav): "
                         + ", ".join(html.escape(n) for n in skipped))
        msg = f'<div class="msg">{" &middot; ".join(parts) or "No files received"}</div>'
        self._page(msg)

    def log_message(self, fmt, *args):
        pass  # journald noise reduction


def main():
    os.makedirs(MODELS, exist_ok=True)
    os.makedirs(IRS, exist_ok=True)
    HTTPServer(("0.0.0.0", PORT), Handler).serve_forever()


if __name__ == "__main__":
    main()
