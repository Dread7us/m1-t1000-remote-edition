#!/usr/bin/env python3
"""Exercise upstream merging in disposable local Git repositories."""
from pathlib import Path
import importlib.util, json, subprocess, tempfile, unittest

spec = importlib.util.spec_from_file_location("sync_upstream", Path(__file__).with_name("sync_upstream.py"))
sync = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sync)

def git(path, *args):
    return subprocess.check_output(["git", "-C", str(path), *args], text=True, stderr=subprocess.DEVNULL).strip()

class SyncTests(unittest.TestCase):
    def fixture(self, root):
        upstream = root / "upstream"; upstream.mkdir()
        git(upstream, "init", "-b", "main")
        git(upstream, "config", "user.name", "Fixture")
        git(upstream, "config", "user.email", "fixture@example.invalid")
        (upstream / "README.md").write_text("Original upstream README\n")
        (upstream / "source.c").write_text("base\n")
        git(upstream, "add", "README.md", "source.c"); git(upstream, "commit", "-m", "Base")
        fork = root / "fork"
        cloned = subprocess.run(["git", "clone", "--no-hardlinks", str(upstream), str(fork)], capture_output=True, text=True)
        if cloned.returncode: raise RuntimeError(cloned.stderr)
        git(fork, "config", "user.name", "Fixture"); git(fork, "config", "user.email", "fixture@example.invalid")
        (fork / "README.md").write_text("Our edition README\n")
        (fork / "remote-edition.json").write_text(json.dumps({"upstream_commit": git(upstream, "rev-parse", "HEAD")}))
        (fork / "our-feature.c").write_text("keep our feature\n")
        git(fork, "add", "README.md", "remote-edition.json", "our-feature.c"); git(fork, "commit", "-m", "Edition")
        sync.ROOT = fork; sync.UPSTREAM = str(upstream)
        return upstream, fork

    def test_merge_preserves_edition_and_main(self):
        with tempfile.TemporaryDirectory(prefix="m1-sync-test-") as directory:
            upstream, fork = self.fixture(Path(directory))
            before = git(fork, "rev-parse", "main")
            (upstream / "README.md").write_text("Updated upstream README\n")
            (upstream / "source.c").write_text("updated upstream source\n")
            git(upstream, "add", "README.md", "source.c"); git(upstream, "commit", "-m", "Upstream update")
            self.assertEqual(sync.sync(), 0)
            self.assertEqual(git(fork, "rev-parse", "main"), before)
            self.assertEqual((fork / "README.md").read_text(), "Our edition README\n")
            self.assertEqual((fork / "README.upstream.md").read_text(), "Updated upstream README\n")
            self.assertEqual((fork / "our-feature.c").read_text(), "keep our feature\n")
            self.assertEqual((fork / "source.c").read_text(), "updated upstream source\n")
            self.assertEqual(json.loads((fork / "remote-edition.json").read_text())["upstream_commit"], git(upstream, "rev-parse", "HEAD"))
            self.assertEqual(len(git(fork, "show", "-s", "--format=%P", "HEAD").split()), 2)

    def test_source_conflict_stops_without_overwriting_main(self):
        with tempfile.TemporaryDirectory(prefix="m1-sync-test-") as directory:
            upstream, fork = self.fixture(Path(directory))
            (fork / "source.c").write_text("our conflicting source\n")
            git(fork, "add", "source.c"); git(fork, "commit", "-m", "Our source change")
            before = git(fork, "rev-parse", "main")
            (upstream / "source.c").write_text("their conflicting source\n")
            git(upstream, "add", "source.c"); git(upstream, "commit", "-m", "Their source change")
            self.assertEqual(sync.sync(), 2)
            self.assertEqual(git(fork, "rev-parse", "main"), before)
            self.assertIn("source.c", git(fork, "diff", "--name-only", "--diff-filter=U"))
            self.assertIn("<<<<<<<", (fork / "source.c").read_text())
            git(fork, "merge", "--abort")

    def test_dirty_tree_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="m1-sync-test-") as directory:
            _, fork = self.fixture(Path(directory))
            (fork / "uncommitted.txt").write_text("do not lose this")
            with self.assertRaisesRegex(RuntimeError, "Commit or stash"):
                sync.sync()
            self.assertEqual(git(fork, "branch", "--show-current"), "main")

if __name__ == "__main__":
    unittest.main()
