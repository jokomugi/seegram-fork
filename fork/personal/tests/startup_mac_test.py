#!/usr/bin/env python3
"""Check native startup with persisted enabled modules in a disposable profile."""
import argparse
from pathlib import Path
import platform
import shlex
import subprocess
import tempfile
import time


def running_modules(path):
    if not path.is_file():
        return set()
    states = {}
    for line in path.read_text().splitlines()[1:]:
        fields = shlex.split(line)
        if len(fields) == 6:
            states[fields[4]] = (fields[1], fields[2])
    return {name for name, state in states.items() if state == ("2", "0")}


def check(app):
    executable = app / "Contents/MacOS/SeeGram Personal"
    if not executable.is_file():
        raise RuntimeError("Personal app executable is missing")
    with tempfile.TemporaryDirectory(prefix="seegram-enabled-startup-") as temporary:
        profile = Path(temporary)
        data = profile / "tdata"
        data.mkdir()
        preferences = data / "personal_modules.preferences"
        preferences.write_text("SeeGramPersonalModules 1\ndiagnostics\nslightly-redesigned\n")
        preferences.chmod(0o600)
        journal = preferences.with_suffix(preferences.suffix + ".events")
        with (profile / "startup-output.log").open("w") as output:
            (profile / "startup-output.log").chmod(0o600)
            process = subprocess.Popen(
                [str(executable), "-workdir", str(profile), "-startintray"],
                stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT,
            )
            try:
                deadline = time.monotonic() + 15
                expected = {"diagnostics", "slightly-redesigned"}
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError(f"Native startup exited before module restoration: {process.returncode}")
                    if expected <= running_modules(journal):
                        break
                    time.sleep(0.1)
                else:
                    raise RuntimeError("Enabled modules did not reach Running during native startup")
                time.sleep(2)
                if process.poll() is not None:
                    raise RuntimeError("Native app exited after restoring enabled modules")
                subprocess.run(
                    [str(executable), "-workdir", str(profile), "-quit"],
                    stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT,
                    timeout=5, check=True,
                )
                if process.wait(timeout=5) != 0:
                    raise RuntimeError("Native app did not exit normally")
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    args = parser.parse_args()
    if platform.system() != "Darwin":
        raise SystemExit("macOS is required")
    try:
        check(args.app.resolve())
    except (RuntimeError, subprocess.SubprocessError) as error:
        raise SystemExit(f"FAIL: {error}")
    print("PASS: native startup restores both enabled modules and quits normally")


if __name__ == "__main__":
    main()
