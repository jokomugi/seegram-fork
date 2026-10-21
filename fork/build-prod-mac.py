#!/usr/bin/env python3
"""Build the optimized personal macOS app for everyday use."""
import argparse
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-only", action="store_true")
    args = parser.parse_args()
    subprocess.run([sys.executable, str(Path(__file__).resolve().parent / "personal/prepare_release_mac.py")], check=True)
    command = [sys.executable, str(Path(__file__).with_name("build-dev-mac.py")),
               "--configuration", "Release"]
    if args.build_only:
        command.append("--build-only")
    subprocess.run(command, check=True)


if __name__ == "__main__":
    main()
