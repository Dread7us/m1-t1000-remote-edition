#!/usr/bin/env python3
"""Merge T-1000 on a review branch, retaining this edition's README."""
from pathlib import Path
import argparse, datetime, json, subprocess, sys

ROOT = Path(__file__).resolve().parents[2]
UPSTREAM = "https://github.com/dagnazty/M1_T-1000.git"

def git(*args, check=True):
    result = subprocess.run(["git", *args], cwd=ROOT, text=True, encoding="utf-8", capture_output=True)
    if check and result.returncode:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip())
    return result

def sync(ref="main"):
    if ref != "main":
        raise RuntimeError("Only the upstream main branch is supported.")
    if git("status", "--porcelain").stdout.strip():
        raise RuntimeError("Commit or stash your changes before updating.")
    current = git("symbolic-ref", "--short", "HEAD").stdout.strip()
    if current != "main":
        raise RuntimeError("Start on your edition's main branch.")
    remote = git("remote", "get-url", "upstream", check=False)
    if remote.returncode:
        git("remote", "add", "upstream", UPSTREAM)
    elif remote.stdout.strip() not in (UPSTREAM, UPSTREAM.removesuffix(".git")):
        raise RuntimeError("The upstream remote is not the expected T-1000 repository.")
    git("fetch", "upstream", "main")
    incoming = git("rev-parse", "upstream/main").stdout.strip()
    if git("merge-base", "--is-ancestor", incoming, "HEAD", check=False).returncode == 0:
        print("Already includes upstream/main. No changes made.")
        return 0
    readme = (ROOT / "README.md").read_bytes()
    snapshot = git("show", incoming + ":README.md").stdout
    branch = "upstream-update/" + datetime.date.today().isoformat() + "-" + incoming[:8]
    git("switch", "-c", branch)
    result = git("merge", "--no-commit", "--no-ff", incoming, check=False)
    merge_head = git("rev-parse", "-q", "--verify", "MERGE_HEAD", check=False)
    if merge_head.returncode:
        raise RuntimeError(result.stderr.strip() or "Merge did not start; inspect git status.")
    # Only this intentionally replaced file is edition-owned. Never apply a
    # blanket 'ours' strategy to source code or silently resolve other conflicts.
    (ROOT / "README.md").write_bytes(readme)
    (ROOT / "README.upstream.md").write_text(snapshot, encoding="utf-8", newline="\n")
    lock = json.loads((ROOT / "remote-edition.json").read_text())
    lock["upstream_commit"] = incoming
    (ROOT / "remote-edition.json").write_text(json.dumps(lock, indent=2) + "\n", encoding="utf-8")
    git("add", "--", "README.md", "README.upstream.md", "remote-edition.json")
    unresolved = git("diff", "--name-only", "--diff-filter=U").stdout.strip()
    if unresolved:
        print("Merge paused on " + branch + ". Your main branch is unchanged.")
        print("Resolve these files, git add each file, then git commit:\n" + unresolved)
        print("To cancel this review merge: git merge --abort; git switch main")
        return 2
    git("commit", "-m", "Merge upstream T-1000 " + incoming[:8])
    print("Prepared " + branch + ". Your main branch is unchanged.")
    print("Run python tools/remote_edition/validate.py and ./build.sh, then review the diff.")
    print("After validation: git switch main; git merge --ff-only " + branch)
    print("Publish explicitly when ready: git push origin main")
    return 0

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ref", default="main", choices=["main"])
    args = parser.parse_args()
    try:
        sys.exit(sync(args.ref))
    except (RuntimeError, OSError) as exc:
        print("Stopped:", exc, file=sys.stderr)
        sys.exit(1)
