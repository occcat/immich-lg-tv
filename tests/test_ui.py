import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import unittest
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("ui_fixture", ROOT / "tools/mock_immich.py")
fixture = importlib.util.module_from_spec(spec); spec.loader.exec_module(fixture)

class NativeUIRegression(unittest.TestCase):
    def test_asset_album_transition_back_stack_and_cursor_pages(self):
        # This sequence previously crashed in draw_gallery: the loading route
        # interpreted an old Asset* catalog as Album* before the worker returned.
        server = fixture.FixtureServer(("127.0.0.1", 0))
        thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
        try:
            with tempfile.TemporaryDirectory(prefix="immich-tv-ui-") as temp:
                folder = Path(temp)
                def shot(at,name): return {"at":at,"capture":str(folder/f"{name}.png")}
                sequence = [{"at":800,"key":"down"},{"at":850,"key":"ok"},shot(1700,"albums"),{"at":1800,"key":"ok"},shot(2500,"album"),{"at":2600,"key":"back"},{"at":3100,"key":"back"},{"at":3200,"key":"up"},{"at":3250,"key":"ok"},{"at":4000,"key":"nextpage"},shot(4700,"page2"),{"at":4800,"key":"prevpage"},shot(5500,"timeline")]
                sequence[3:3] = [{"at":1720,"key":"down"},shot(1740,"album-row2"),{"at":1760,"key":"up"}]
                sequence += [{"at":5520,"pointer":{"x":448,"y":841}},shot(5540,"pointer-row3"),
                             {"at":5560,"pointer":{"x":604,"y":350}},shot(5580,"pointer-gap"),
                             {"at":5600,"key":"down"},shot(5660,"scrolled"),
                             {"at":5700,"key":"back"}] + [{"at":5800,"key":"down"}]*4
                sequence += [{"at":5900,"key":"ok"},shot(6200,"settings"),{"at":6300,"key":"ok"},shot(6600,"keyboard")]
                params = {"server":f"http://127.0.0.1:{server.server_port}","apiKey":fixture.DEMO_KEY,"allowHttp":True,"qa":sequence,"exitAfterMs":6900}
                env = dict(os.environ, SDL_VIDEODRIVER="dummy")
                result = subprocess.run([str(ROOT/"build/immich-tv-qa"),"--hidden","--config",str(folder/"config.json"),json.dumps(params)],cwd=ROOT,env=env,capture_output=True,text=True,timeout=20)
                self.assertEqual(result.returncode,0,result.stderr)
                def state(name): return json.loads((folder/f"{name}.png.json").read_text())
                self.assertEqual((state("albums")["view"],state("albums")["count"]),(1,6))
                self.assertGreater(state("album")["count"],0)
                self.assertEqual((state("page2")["page"],state("page2")["count"]),(2,7))
                self.assertEqual((state("timeline")["view"],state("timeline")["page"]),(0,1))
                self.assertTrue(all(not state(name)["loading"] for name in ["albums","album","page2","timeline"]))
                self.assertEqual((state("album-row2")["selected"],state("album-row2")["columns"]),(3,3))
                self.assertEqual((state("timeline")["columns"],state("timeline")["rows"]),(5,3))
                self.assertEqual(state("pointer-row3")["selected"],10)
                self.assertEqual(state("pointer-gap")["selected"],10)
                self.assertEqual((state("scrolled")["selected"],state("scrolled")["scrollRow"]),(15,1))
                self.assertTrue(state("settings")["settings"])
                self.assertTrue(state("keyboard")["keyboard"])
                # Regress the reported visual defect using rendered pixels,
                # including the CJK font's uneven ascent/descent padding.
                def centered_ink(name, box, color):
                    image = Image.open(folder/f"{name}.png").convert("RGB")
                    sx, sy = image.width/1920, image.height/1080
                    x,y,w,h = box
                    crop = image.crop((round(x*sx),round(y*sy),round((x+w)*sx),round((y+h)*sy)))
                    pad = round(18*min(sx,sy))
                    points = [(px,py) for py in range(pad,crop.height-pad) for px in range(pad,crop.width-pad)
                              if max(abs(a-b) for a,b in zip(crop.getpixel((px,py)),color)) < 9]
                    self.assertTrue(points, f"Missing glyphs in {name}")
                    for axis, extent in [(0,crop.width),(1,crop.height)]:
                        center = (min(p[axis] for p in points)+max(p[axis] for p in points)+1)/2
                        self.assertLessEqual(abs(center-extent/2),1.5,f"Off-center text in {name}")
                    return image
                image = centered_ink("settings",(336,760,936,76),(12,24,45))
                centered_ink("keyboard",(430,444,96,66),(10,15,24))
                sx,sy = image.width/1920,image.height/1080
                corner = image.crop((round(336*sx),round(760*sy),round(351*sx),round(775*sy)))
                # Binary integer scanlines have only fill/background colors.
                colors = set(corner.get_flattened_data())-{(15,23,37),(105,156,237)}
                self.assertGreaterEqual(len(colors),3,"Rounded edge lost antialiasing")
        finally:
            server.shutdown(); server.server_close(); thread.join()

if __name__ == "__main__": unittest.main()
