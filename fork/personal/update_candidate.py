#!/usr/bin/env python3
"""Replay personal commits onto a SeeGram release in an isolated checkout."""
import argparse
import json
from pathlib import Path
import re
import subprocess

METADATA = Path("fork/personal/upstream.json")


def git(root, *args, check=True):
    result = subprocess.run(["git", "-C", str(root), *args], text=True,
                            capture_output=True)
    if check and result.returncode:
        raise RuntimeError(result.stderr.strip() or "Git operation failed")
    return result


def prepare_candidate(root, release, destination):
    root, destination = Path(root).resolve(), Path(destination).resolve()
    if not re.fullmatch(r"v[0-9]+(?:\.[0-9]+){0,3}(?:-[0-9]+)?", release):
        raise ValueError("Expected a stable SeeGram release tag")
    if git(root, "status", "--porcelain").stdout.strip():
        raise ValueError("Commit the personal changes before preparing an update")
    config = json.loads((root / METADATA).read_text())
    if config["schema"] != 1 or not re.fullmatch(r"[0-9a-f]{40}", config["base_commit"]):
        raise ValueError("Unsupported upstream metadata")
    base = config["base_commit"]
    git(root, "merge-base", "--is-ancestor", base, "HEAD")
    git(root, "fetch", "--no-tags", config["repository"], f"refs/tags/{release}")
    target = git(root, "rev-parse", "FETCH_HEAD^{commit}").stdout.strip()
    if target == base:
        return {"status": "up_to_date", "release": release, "base_commit": base}
    branch = "codex/seegram-" + release
    if destination.exists():
        raise ValueError("Candidate path already exists; preserving it for inspection")
    if git(root, "show-ref", "--verify", f"refs/heads/{branch}", check=False).returncode == 0:
        raise ValueError("Candidate branch already exists; preserving it for inspection")
    destination.parent.mkdir(parents=True, exist_ok=True)
    git(root, "worktree", "add", "--detach", str(destination), "HEAD")
    rebase = git(destination, "rebase", "--onto", target, base, check=False)
    report = {"status": "candidate", "release": release, "base_commit": target,
              "worktree": str(destination), "branch": branch}
    if rebase.returncode:
        report["status"] = "conflict"
        report["conflicts"] = git(destination, "diff", "--name-only", "--diff-filter=U").stdout.splitlines()
        report["summary"] = rebase.stderr.strip()
    else:
        git(destination, "switch", "-c", branch)
        config.update(release=release, base_commit=target)
        (destination / METADATA).write_text(json.dumps(config, indent=2) + "\n")
        git(destination, "add", str(METADATA))
        git(destination, "commit", "-m", f"Update SeeGram base to {release}")
        report["commit"] = git(destination, "rev-parse", "HEAD").stdout.strip()
    report_path = destination.parent / (destination.name + ".report.json")
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--release", required=True)
    parser.add_argument("--worktree", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = prepare_candidate(args.root, args.release, args.worktree)
    except (ValueError, RuntimeError, OSError) as error:
        parser.exit(1, str(error) + "\n")
    print(json.dumps(result))
    if result["status"] == "conflict":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
