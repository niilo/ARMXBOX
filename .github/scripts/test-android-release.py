#!/usr/bin/env python3
"""Exercise release preparation and atomic publication against local Git remotes."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("android-release.py").resolve()
GRADLE_FILE = "android/app/build.gradle.kts"
FIXTURE = '''android {
  defaultConfig {
    versionCode = 30 // keep this comment
    versionName = "1.2.9"
  }
}
'''


def git(root, *args, check=True):
    return subprocess.run(["git", *args], cwd=root, text=True,
                          capture_output=True, check=check)


def identity(root):
    git(root, "config", "user.name", "Release Test")
    git(root, "config", "user.email", "release-test@example.invalid")
    git(root, "config", "commit.gpgsign", "false")
    git(root, "config", "tag.gpgsign", "false")


class AndroidReleaseTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="android-release-test-")
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        self.work = root / "work"
        self.remote = root / "origin.git"
        self.work.mkdir()
        self.remote.mkdir()
        git(self.work, "init", "--initial-branch=master")
        identity(self.work)
        self.gradle = self.work / GRADLE_FILE
        self.gradle.parent.mkdir(parents=True)
        self.gradle.write_text(FIXTURE)
        (self.work / ".gitignore").write_text("android/key.properties\n")
        git(self.work, "add", ".")
        git(self.work, "commit", "-m", "Initial Android version")
        git(self.remote, "init", "--bare", "--initial-branch=master")
        git(self.work, "remote", "add", "origin", str(self.remote))
        git(self.work, "push", "origin", "master")
        self.original = git(self.work, "rev-parse", "HEAD").stdout.strip()

    def cli(self, *args, success=True):
        result = subprocess.run([sys.executable, str(SCRIPT), "--root",
                                 str(self.work), *args], capture_output=True, text=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
        return result

    def other_clone(self):
        other = Path(self.temporary.name) / "other"
        git(self.work, "clone", str(self.remote), str(other))
        identity(other)
        return other

    def publish(self, success=True):
        return self.cli("publish", "--branch", "refs/heads/master",
                        "--tag", "v1.2.10", success=success)

    def test_default_release_commits_both_versions_and_tags_that_commit(self):
        self.cli("prepare")
        self.assertIn("versionCode = 31 // keep this comment", self.gradle.read_text())
        self.assertIn('versionName = "1.2.10"', self.gradle.read_text())
        self.publish()
        head = git(self.remote, "rev-parse", "refs/heads/master").stdout.strip()
        tag = git(self.remote, "rev-parse", "refs/tags/v1.2.10^{}").stdout.strip()
        self.assertEqual(head, tag)
        self.assertNotEqual(head, self.original)
        released = git(self.remote, "show", "v1.2.10:" + GRADLE_FILE).stdout
        self.assertIn("versionCode = 31", released)
        self.assertIn('versionName = "1.2.10"', released)
        message = git(self.remote, "tag", "-l", "-n", "v1.2.10").stdout
        self.assertIn("versionCode 31", message)

    def test_preview_reports_versions_without_mutating_anything(self):
        output = Path(self.temporary.name) / "github-output"
        result = self.cli("prepare", "--dry-run", "--github-output", str(output))
        self.assertIn("tag=v1.2.10", result.stdout)
        self.assertIn("version_code=31", output.read_text())
        self.assertEqual(self.gradle.read_text(), FIXTURE)
        self.assertEqual(git(self.work, "rev-parse", "HEAD").stdout.strip(), self.original)
        self.assertEqual(git(self.work, "tag", "-l").stdout, "")
        self.assertEqual(git(self.work, "status", "--porcelain").stdout, "")

    def test_explicit_versions_allow_a_minor_release_and_larger_code(self):
        result = self.cli("prepare", "--version-name", "1.3.0", "--version-code", "40")
        self.assertIn("tag=v1.3.0", result.stdout)
        self.assertIn("versionCode = 40", self.gradle.read_text())
        self.assertIn('versionName = "1.3.0"', self.gradle.read_text())

    def test_invalid_or_non_increasing_versions_do_not_edit_files(self):
        cases = (("--version-name", "1.2.9"), ("--version-name", "1.2.8"),
                 ("--version-name", "v1.2.10"), ("--version-name", "1.2.10\ntag=bad"),
                 ("--version-code", "30"), ("--version-code", "0"),
                 ("--version-code", "-1"), ("--version-code", "2100000001"))
        for args in cases:
            with self.subTest(args=args):
                self.cli("prepare", *args, success=False)
                self.assertEqual(self.gradle.read_text(), FIXTURE)

    def test_duplicate_tag_does_not_edit_versions(self):
        git(self.work, "tag", "v1.2.10")
        self.cli("prepare", success=False)
        self.assertEqual(self.gradle.read_text(), FIXTURE)

    def test_dirty_checkout_is_not_modified(self):
        (self.work / "unrelated").write_text("Uncommitted work")
        self.cli("prepare", success=False)
        self.assertEqual(self.gradle.read_text(), FIXTURE)

    def test_ignored_local_signing_properties_are_not_read_or_modified(self):
        properties = self.work / "android/key.properties"
        properties.write_text("Unrelated local configuration")
        self.cli("prepare")
        self.assertEqual(properties.read_text(), "Unrelated local configuration")

    def test_manual_tag_must_match_android_version_name(self):
        self.cli("check-tag", "v1.2.9")
        self.cli("check-tag", "v1.2.10", success=False)

    def test_publication_rejects_unrelated_staged_changes(self):
        self.cli("prepare")
        (self.work / "unrelated").write_text("Other staged work")
        git(self.work, "add", "unrelated")
        self.publish(success=False)
        self.assertEqual(git(self.work, "rev-parse", "HEAD").stdout.strip(), self.original)

    def test_branch_race_does_not_publish_the_tag(self):
        self.cli("prepare")
        other = self.other_clone()
        (other / "new-work").write_text("A newer commit")
        git(other, "add", ".")
        git(other, "commit", "-m", "Advance remote branch")
        git(other, "push", "origin", "master")
        advanced = git(self.remote, "rev-parse", "refs/heads/master").stdout
        self.publish(success=False)
        self.assertEqual(git(self.remote, "rev-parse", "refs/heads/master").stdout, advanced)
        self.assertNotEqual(git(self.remote, "show-ref", "--verify", "refs/tags/v1.2.10",
                                check=False).returncode, 0)

    def test_tag_race_does_not_publish_the_version_commit(self):
        self.cli("prepare")
        other = self.other_clone()
        git(other, "tag", "-a", "v1.2.10", "-m", "Another release claimed the tag")
        git(other, "push", "origin", "refs/tags/v1.2.10")
        self.publish(success=False)
        self.assertEqual(git(self.remote, "rev-parse", "refs/heads/master").stdout.strip(), self.original)
        self.assertEqual(git(self.remote, "rev-parse", "refs/tags/v1.2.10^{}").stdout.strip(), self.original)


if __name__ == "__main__":
    unittest.main()
