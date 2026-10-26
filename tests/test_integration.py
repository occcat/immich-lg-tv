import importlib.util
import json
from pathlib import Path
import subprocess
import threading
import unittest
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("fixture", ROOT / "tools" / "mock_immich.py")
fixture = importlib.util.module_from_spec(spec); spec.loader.exec_module(fixture)

class ClientContractTests(unittest.TestCase):
    def run_client(self, version, mode="normal", prefix="", key=fixture.DEMO_KEY):
        server = fixture.FixtureServer(("127.0.0.1", 0), version, prefix)
        thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
        url = f"http://127.0.0.1:{server.server_port}{prefix}"
        try:
            result = subprocess.run([str(ROOT / "build/test-core"), url, key, mode], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            reads = server.requests
            self.assertTrue(reads)
            self.assertTrue(all(r["method"] == "GET" or (r["method"] == "POST" and r["path"] == "/api/search/metadata") for r in reads))
            if mode != "unauthorized": self.assertTrue(all(r["authenticated"] for r in reads))
            return reads
        finally:
            server.shutdown(); server.server_close(); thread.join()

    def test_immich_3_cursor_pagination_filters_and_empty_album(self):
        requests = self.run_client(3)
        searches = [r["body"] for r in requests if r["method"] == "POST"]
        self.assertEqual(searches[1]["cursor"], "40")
        self.assertIn("any", searches[-1]["filter"]["albumIds"])

    def test_immich_2_page_pagination_and_legacy_duration(self):
        requests = self.run_client(2)
        searches = [r["body"] for r in requests if r["method"] == "POST"]
        self.assertEqual(searches[1]["page"], 2)

    def test_reverse_proxy_subpath(self): self.run_client(3, prefix="/photos")
    def test_expired_or_wrong_key(self): self.run_client(3, mode="unauthorized", key="wrong")

    def test_fixture_refuses_mutations(self):
        server = fixture.FixtureServer(("127.0.0.1", 0)); thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
        try:
            request = urllib.request.Request(f"http://127.0.0.1:{server.server_port}/api/assets", data=b"{}", method="DELETE", headers={"x-api-key": fixture.DEMO_KEY})
            with self.assertRaises(urllib.error.HTTPError) as error: urllib.request.urlopen(request)
            self.assertEqual(error.exception.code, 405)
        finally: server.shutdown(); server.server_close(); thread.join()

    @unittest.skipUnless((fixture.MEDIA / "sample.mp4").exists(), "run make fixtures for demux tests")
    def test_demux_range_reads_annex_b_and_seek_after_eof(self):
        self.check_demux("asset-3", "silent")

    @unittest.skipUnless((fixture.MEDIA / "sample-audio.mp4").exists(), "run make fixtures for audio tests")
    def test_demux_aac_adts_and_audio_video_timestamps(self):
        self.check_demux("asset-17", "audio")

    def check_demux(self, asset, mode):
        server = fixture.FixtureServer(("127.0.0.1", 0)); thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
        try:
            result = subprocess.run([str(ROOT / "build/test-demux"), f"http://127.0.0.1:{server.server_port}", fixture.ident(asset), mode], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            self.assertTrue(all(r["method"] == "GET" and r["authenticated"] and r["range"] for r in server.requests))
        finally: server.shutdown(); server.server_close(); thread.join()

    @unittest.skipUnless((fixture.MEDIA / "sample.mp4").exists(), "run make fixtures for video tests")
    def test_authenticated_video_proxy_preserves_range_and_hides_key(self):
        server = fixture.FixtureServer(("127.0.0.1", 0)); thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
        process = subprocess.Popen([str(ROOT / "build/test-core"), f"http://127.0.0.1:{server.server_port}", fixture.DEMO_KEY, "proxy"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            url = process.stdout.readline().strip()
            self.assertTrue(url.startswith("http://127.0.0.1:")); self.assertNotIn(fixture.DEMO_KEY, url)
            response = urllib.request.urlopen(urllib.request.Request(url, headers={"Range": "bytes=128-511"}), timeout=5)
            self.assertEqual(response.status, 206); self.assertEqual(len(response.read()), 384)
            self.assertTrue(response.headers["Content-Range"].startswith("bytes 128-511/"))
            media = [r for r in server.requests if r["path"].endswith("video/playback")]
            self.assertTrue(media[-1]["authenticated"]); self.assertEqual(media[-1]["range"], "bytes=128-511")
            with self.assertRaises(urllib.error.HTTPError) as error: urllib.request.urlopen(url.replace("/stream/", "/unknown/"))
            self.assertEqual(error.exception.code, 404)
            self.assertEqual(process.wait(timeout=12), 0, process.stderr.read())
        finally:
            if process.poll() is None: process.terminate(); process.wait()
            process.stdout.close(); process.stderr.close(); server.shutdown(); server.server_close(); thread.join()

if __name__ == "__main__": unittest.main()
