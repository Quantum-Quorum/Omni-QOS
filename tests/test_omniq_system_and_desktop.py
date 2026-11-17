import os
import subprocess
import sys
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

class TestOmniQSystemAndDesktop(unittest.TestCase):
    def test_01_kernel_module_elf_and_vermagic(self):
        ko = os.path.join(REPO_ROOT, "kernel", "drivers", "accel", "qpu", "omniq_qpu.ko")
        self.assertTrue(os.path.exists(ko))
        with open(ko, "rb") as f:
            self.assertEqual(f.read(4), b"\x7fELF")
        info = subprocess.check_output(["modinfo", ko], text=True)
        self.assertIn("7.0.0-43-generic", info)

    def test_02_python_sdk_and_adapters(self):
        sdk_test = os.path.join(REPO_ROOT, "tests", "test_python_sdk.py")
        rc = subprocess.run([sys.executable, sdk_test], stdout=subprocess.DEVNULL).returncode
        self.assertEqual(rc, 0)

    def test_03_shell_scripts_syntax_valid(self):
        for rel in [
            "scripts/build_iso.sh",
            "scripts/build_full_distro_iso.sh",
            "scripts/create_vmbox_omniq.sh",
            "scripts/run_local.sh",
        ]:
            p = os.path.join(REPO_ROOT, rel)
            self.assertTrue(os.path.exists(p), f"Missing {rel}")
            rc = subprocess.run(["bash", "-n", p]).returncode
            self.assertEqual(rc, 0, f"Syntax error in {rel}")

    def test_04_gtk_margin_and_css_guardrails_across_bin(self):
        bin_dir = os.path.join(REPO_ROOT, "bin")
        for fn in os.listdir(bin_dir):
            fp = os.path.join(bin_dir, fn)
            if os.path.islink(fp) or not os.path.isfile(fp):
                continue
            with open(fp, "rb") as f:
                head = f.read(4)
            if head == b"\x7fELF":
                continue
            with open(fp, "r", errors="ignore") as f:
                src = f.read()
            self.assertNotIn("set_margin_left(", src, f"Deprecated set_margin_left in bin/{fn}")
            self.assertNotIn("set_margin_right(", src, f"Deprecated set_margin_right in bin/{fn}")
            self.assertNotIn("text-align: left", src, f"Invalid GTK CSS text-align in bin/{fn}")

    def test_05_desktop_assets_and_configs_when_built(self):
        jwmrc = os.path.join(REPO_ROOT, "etc", "jwm", "system.jwmrc")
        if os.path.exists(jwmrc):
            with open(jwmrc, "r") as f:
                txt = f.read()
            self.assertIn("OmniqDesktop", txt)
            self.assertIn("omniq-hilbert-space", txt)
            self.assertIn("omniq-circuit-maker", txt)

        wp = os.path.join(REPO_ROOT, "etc", "omniq", "wallpaper.png")
        if os.path.exists(wp):
            with open(wp, "rb") as f:
                self.assertEqual(f.read(8), b"\x89PNG\r\n\x1a\n")

    def test_06_all_bin_tools_pass_headless_self_test(self):
        bin_dir = os.path.join(REPO_ROOT, "bin")
        skip = {"wmctrl", "xdotool", "chromium", "chromium-browser"}
        tested = 0
        for fn in sorted(os.listdir(bin_dir)):
            if fn in skip or fn.startswith(".") or fn == "__pycache__":
                continue
            fp = os.path.join(bin_dir, fn)
            if os.path.islink(fp) or not os.path.isfile(fp) or not os.access(fp, os.X_OK):
                continue
            res = subprocess.run([fp, "--test"], capture_output=True, text=True, timeout=15)
            self.assertEqual(res.returncode, 0, f"bin/{fn} --test failed:\n{res.stdout}\n{res.stderr}")
            tested += 1
        self.assertGreaterEqual(tested, 8)

    def test_07_iso9660_boot_images_if_present(self):
        for iso_name in ["omniq-os.iso", "omniq-os-full.iso"]:
            iso_path = os.path.join(REPO_ROOT, iso_name)
            if os.path.exists(iso_path):
                with open(iso_path, "rb") as f:
                    f.seek(0x8001)
                    self.assertEqual(f.read(5), b"CD001", f"Invalid ISO9660 magic in {iso_name}")

if __name__ == "__main__":
    unittest.main()
