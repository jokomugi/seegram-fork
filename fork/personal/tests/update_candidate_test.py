import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "update_candidate.py"
spec = importlib.util.spec_from_file_location("update_candidate", SCRIPT)
updates = importlib.util.module_from_spec(spec)
spec.loader.exec_module(updates)


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], text=True,
                                   stderr=subprocess.PIPE).strip()


class UpdateCandidateTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="personal-update-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.upstream = self.root / "upstream"
        self.upstream.mkdir()
        git(self.upstream, "init", "--initial-branch=main")
        git(self.upstream, "config", "user.name", "Fixture")
        git(self.upstream, "config", "user.email", "fixture@example.invalid")
        (self.upstream / "app.txt").write_text("upstream one\n")
        git(self.upstream, "add", ".")
        git(self.upstream, "commit", "-m", "Base release")
        self.base = git(self.upstream, "rev-parse", "HEAD")
        git(self.upstream, "tag", "v1.0-1")
        self.fork = self.root / "fork"
        subprocess.run(["git", "clone", "--quiet", str(self.upstream), str(self.fork)], check=True)
        git(self.fork, "config", "user.name", "Fixture")
        git(self.fork, "config", "user.email", "fixture@example.invalid")
        git(self.fork, "switch", "-c", "personal")
        (self.fork / "plugins.txt").write_text("our module stays\n")
        metadata = self.fork / "fork/personal/upstream.json"
        metadata.parent.mkdir(parents=True)
        metadata.write_text(json.dumps({"schema": 1, "repository": str(self.upstream),
                                        "base_commit": self.base, "release": "v1.0-1"}))
        git(self.fork, "add", ".")
        git(self.fork, "commit", "-m", "Personal module")
        self.original = git(self.fork, "rev-parse", "HEAD")
        (self.upstream / "app.txt").write_text("upstream two\n")
        git(self.upstream, "commit", "-am", "Next release")
        self.next_base = git(self.upstream, "rev-parse", "HEAD")
        git(self.upstream, "tag", "v1.0-2")

    def test_new_release_preserves_our_module_and_current_checkout(self):
        candidate = self.root / "candidate"
        result = updates.prepare_candidate(self.fork, "v1.0-2", candidate)
        self.assertEqual(result["status"], "candidate")
        self.assertEqual((candidate / "plugins.txt").read_text(), "our module stays\n")
        self.assertEqual((candidate / "app.txt").read_text(), "upstream two\n")
        metadata = json.loads((candidate / "fork/personal/upstream.json").read_text())
        self.assertEqual(metadata["base_commit"], self.next_base)
        self.assertEqual(git(self.fork, "rev-parse", "HEAD"), self.original)
        self.assertEqual(git(self.fork, "branch", "--show-current"), "personal")
        self.assertEqual(git(self.fork, "status", "--porcelain"), "")

    def test_rebase_conflict_preserves_current_checkout_and_exposes_the_conflict(self):
        (self.fork / "app.txt").write_text("personal behavior\n")
        git(self.fork, "commit", "-am", "Personal integration")
        original = git(self.fork, "rev-parse", "HEAD")
        candidate = self.root / "conflicting-candidate"
        result = updates.prepare_candidate(self.fork, "v1.0-2", candidate)
        self.assertEqual(result["status"], "conflict")
        self.assertEqual(result["conflicts"], ["app.txt"])
        self.assertEqual(git(self.fork, "rev-parse", "HEAD"), original)
        self.assertEqual((self.fork / "app.txt").read_text(), "personal behavior\n")
        self.assertEqual(git(self.fork, "status", "--porcelain"), "")


if __name__ == "__main__":
    unittest.main()
