"""src/installer/tirevive-driver.ps1 (issue #31): a later -match replaces $Matches, so the Access package's name was empty
and pnputil ran without one: the Access driver stayed and Windows put the Virus back on it."""
import os
import re
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "..", "..", "src", "installer", "tirevive-driver.ps1")


class DriverScript(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(SCRIPT, encoding="utf-8") as f:
            cls.text = f.read()

    def test_a_package_to_delete_never_comes_straight_from_matches(self):
        for line in self.text.splitlines():
            if "/delete-driver" in line and not line.lstrip().startswith("#"):
                self.assertNotIn("$Matches", line, line)

    def test_no_condition_reads_matches_after_two_matches(self):
        # the shape of the bug: two -match in one condition, $Matches used in its body
        lines = self.text.splitlines()
        for i, line in enumerate(lines):
            if len(re.findall(r"-match", line)) >= 2 and line.lstrip().startswith("if "):
                body = "\n".join(lines[i + 1 : i + 4])
                self.assertNotIn("$Matches", body, line)

    def test_the_log_says_what_the_virus_ended_up_on(self):
        self.assertIn('"now: $($d.InstanceId) -> $($d.Service)"', self.text)
        self.assertIn("still on the Access USB driver", self.text)

    def test_the_log_waits_for_the_re_detection_and_asks_to_unplug(self):
        # #31: the Virus took longer than 3 s to come back, and unloading Access' driver with it connected crashed
        self.assertIn("Wait-Until { Test-VirusDetected (Get-VirusDevices) } 15 1", self.text)
        self.assertIn("Show-UserMessage", self.text)
        self.assertLess(self.text.index("Get-AccessBoundVirus"), self.text.index("pnputil /delete-driver $pkg"))

    def test_a_failed_driver_add_fails_the_setup(self):
        # audit #15: the MSI action checks the exit code (Return="check"); exit 0 after a failure looked like success.
        # Not on the signature status alone: M6 logged "UnknownError" with a working driver
        add = self.text.index("$out = pnputil /add-driver")
        self.assertRegex(self.text[add:], r"if \(-not \$pkg\) \{ Fail-Install ")
        fail = self.text[self.text.index("function Fail-Install"):]
        fail = fail[: fail.index("\n}\n")]
        self.assertIn("Trust-Store", fail)  # no uninstall follows a failed setup: take the trust back here
        self.assertIn("exit 1", fail)



class SystemLogs(unittest.TestCase):
    def test_system_actions_log_into_the_install_folder(self):
        # audit #28: a standard user can create %ProgramData%\TI Revive first (a junction, a planted driver.log hard
        # link) and SYSTEM then appends wherever it points; the install folder under Program Files is not theirs
        for name in ("tirevive-driver.ps1", "tirevive-bundles.ps1"):
            with open(os.path.join(HERE, "..", "..", "src", "installer", name), encoding="utf-8") as f:
                text = f.read()
            self.assertNotIn("$logDir = Join-Path $env:ProgramData", text, name)
            self.assertIn("$logDir = $PSScriptRoot", text, name)



class AccessScript(unittest.TestCase):
    def test_only_the_64_bit_access_msi_is_picked(self):
        # audit #30: the newest "*Virus TI*.msi" could be the 32-bit one, whose Virus Control.dll is refused (E345)
        with open(os.path.join(HERE, "..", "..", "src", "installer", "tirevive-access.ps1"), encoding="utf-8") as f:
            text = f.read()
        self.assertIn("-Filter '*Virus TI*64-bit*.msi'", text)
        self.assertNotIn("-Filter '*Virus TI*.msi'", text)

if __name__ == "__main__":
    unittest.main()
