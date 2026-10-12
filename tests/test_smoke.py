"""Small regression check for the framebuffer assertion used by engine smoke tests."""
from pathlib import Path
import math
import struct
import tempfile
import unittest

from smoke_server import check_screenshot, PLAY_VIDEO_SETTINGS, has_play_video_defaults, play_config


class ScreenshotCheck(unittest.TestCase):
    def test_interactive_native_vsync_quality_uses_existing_render_paths(self):
        s=PLAY_VIDEO_SETTINGS
        self.assertEqual((s['r_customwidth'],s['r_customheight'],s['r_displayRefresh']),('3440','1440','120'))
        self.assertEqual((s['r_swapInterval'],s['com_maxfps'],s['r_finish']),('1','0','0'))
        self.assertAlmostEqual(float(s['r_customaspect']),3440/1440)
        self.assertAlmostEqual(math.tan(math.radians(float(s['cg_fov']))/2)/(3440/1440),1/(4/3))
        self.assertEqual((s['r_primitives'],s['r_picmip'],s['r_textureMode']),('2','0','GL_LINEAR_MIPMAP_LINEAR'))

    def test_play_video_defaults_seed_once_not_overwrite_menu_preferences(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)
            self.assertFalse(has_play_video_defaults(p))
            (p/'etconfig.cfg').write_text('seta et_rustVideoVersion "1"\nseta r_swapInterval "0"\n')
            self.assertTrue(has_play_video_defaults(p))

    def test_profile_video_marker_is_not_the_root_marker(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);profile=p/'profiles/Player';profile.mkdir(parents=True)
            (p/'etconfig.cfg').write_text('seta et_rustVideoVersion "1"\n')
            (p/'profiles/defaultprofile.dat').write_text('"Player"')
            (profile/'etconfig.cfg').write_text('seta r_mode "4"\n')
            self.assertEqual(play_config(p),profile/'etconfig.cfg')
            self.assertFalse(has_play_video_defaults(p))
            (profile/'etconfig.cfg').write_text('seta et_rustVideoVersion "1"\nseta r_mode "-1"\n')
            self.assertTrue(has_play_video_defaults(p))
            (p/'profiles/defaultprofile.dat').write_text('"../escape"')
            with self.assertRaises(ValueError):play_config(p)

    def test_accept_rendered_frame_reject_bad_or_blank_frames(self):
        header = bytearray(18)
        header[2], header[16] = 2, 24
        struct.pack_into("<HH", header, 12, 640, 480)
        pixels = bytes(range(16)) * (640 * 480 * 3 // 16)
        compressed = header.copy()
        compressed[2] = 10
        wrong_size = header.copy()
        struct.pack_into("<H", wrong_size, 12, 639)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "frame.tga"
            path.write_bytes(header + pixels)
            check_screenshot(path)
            for invalid in (b"", header, header + pixels[:-1], compressed + pixels,
                            wrong_size + pixels, header + bytes(len(pixels))):
                with self.subTest(size=len(invalid)):
                    path.write_bytes(invalid)
                    with self.assertRaises(ValueError):
                        check_screenshot(path)


if __name__ == "__main__":
    unittest.main()
