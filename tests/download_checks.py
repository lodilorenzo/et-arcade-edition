"""Deterministic HTTP/download checks; optional --https-url checks trusted TLS."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import ssl
import subprocess
import tempfile
import threading

PAYLOAD = bytes(range(256)) * 1024


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def handle(self):
        try:
            super().handle()
        except (ConnectionError, ssl.SSLError):
            pass  # Expected during cancellation/certificate rejection.

    def do_GET(self):
        if self.path in ("/redirect", "/bad-redirect"):
            self.send_response(302)
            self.send_header("Location", "/ok" if self.path == "/redirect" else "file:///etc/passwd")
            self.end_headers()
        else:
            self.send_response(200 if self.path in ("/ok", "/truncated") else 404)
            self.send_header("Content-Length", str(len(PAYLOAD) + (100 if self.path == "/truncated" else 0)))
            self.end_headers()
            try:
                self.wfile.write(PAYLOAD)
            except (ConnectionError, ssl.SSLError):
                pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--https-url", help="Optional external HTTPS URL expected to succeed")
    args = parser.parse_args()
    binary = args.binary.resolve()
    with tempfile.TemporaryDirectory() as directory:
        folder = Path(directory)
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            url = f"http://127.0.0.1:{server.server_port}"
            for path, expected in (("/ok", "ok"), ("/redirect", "ok"), ("/404", "fail"),
                                   ("/bad-redirect", "fail"), ("/truncated", "fail"), ("/ok", "cancel")):
                target = folder / "download.pk3.tmp"
                subprocess.run([str(binary), url + path, str(target), expected], check=True, timeout=50)
                if expected == "ok":
                    assert target.read_bytes() == PAYLOAD
        finally:
            server.shutdown()
            server.server_close()
        # An untrusted certificate must never be accepted; optional if openssl is absent.
        import shutil
        if shutil.which("openssl"):
            subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                            "-subj", "/CN=localhost", "-keyout", str(folder / "key.pem"),
                            "-out", str(folder / "cert.pem")], check=True, capture_output=True)
            secure = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(folder / "cert.pem", folder / "key.pem")
            secure.socket = context.wrap_socket(secure.socket, server_side=True)
            threading.Thread(target=secure.serve_forever, daemon=True).start()
            try:
                subprocess.run([str(binary), f"https://localhost:{secure.server_port}/ok",
                                str(folder / "untrusted.tmp"), "fail"], check=True, timeout=50)
            finally:
                secure.shutdown()
                secure.server_close()
        else:
            print("SKIP self-signed TLS fixture: openssl not found")
        if args.https_url:
            subprocess.run([str(binary), args.https_url, str(folder / "https.tmp"), "ok"], check=True, timeout=50)
            assert (folder / "https.tmp").stat().st_size > 0
    print("PASS download payload, redirect, HTTP failure/truncation, forbidden schemes and cancellation")


if __name__ == "__main__":
    main()
