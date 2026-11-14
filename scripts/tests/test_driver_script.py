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


if __name__ == "__main__":
    unittest.main()
