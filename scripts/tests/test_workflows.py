"""The CI workflows (.github/workflows) install their tools at exact versions (audit #16): a release build must not
pick up whatever a package index serves that day."""
import glob
import os
import re
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
WORKFLOWS = sorted(glob.glob(os.path.join(HERE, "..", "..", ".github", "workflows", "*.yml")))


def lines():
    for path in WORKFLOWS:
        with open(path, encoding="utf-8") as f:
            for n, line in enumerate(f, 1):
                yield f"{os.path.basename(path)}:{n}", line.split(" #")[0]


class Workflows(unittest.TestCase):
    def test_pip_installs_are_pinned(self):
        for where, line in lines():
            if re.search(r"\bpip3? install\b", line):
                for spec in re.findall(r'"([A-Za-z0-9_.-]+[^"]*)"|\s([A-Za-z0-9_.-]+[<>=!~][^\s]*)', line):
                    spec = spec[0] or spec[1]
                    self.assertRegex(spec, r"^[A-Za-z0-9_.-]+==[0-9][0-9.]*$", where)

    def test_powershell_modules_are_pinned(self):
        for where, line in lines():
            if "Install-Module" in line:
                self.assertIn("-RequiredVersion", line, where)
                self.assertNotIn("-MinimumVersion", line, where)

    def test_every_workflow_limits_its_token(self):  # audit #34
        for path in WORKFLOWS:
            with open(path, encoding="utf-8") as f:
                self.assertRegex(f.read(), r"(?m)^permissions:", os.path.basename(path))

    def test_no_version_written_into_a_workflow(self):  # audit #34: dev-mac built "0.8.18-dev" for months
        for where, line in lines():
            self.assertNotRegex(line, r"\b\d+\.\d+\.\d+-dev\b", where)


if __name__ == "__main__":
    unittest.main()
