#!/usr/bin/env python3
"""Build and stage the isolated SeeGram Personal development app."""
import argparse
import os
from pathlib import Path
import platform
import plistlib
import shutil
import subprocess
import tempfile

from check_macho import verify_mapping

ROOT = Path(__file__).resolve().parents[2]
NAME = "SeeGram Personal"
BUNDLE_ID = "app.jokomugi.seegram.personal"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-only", action="store_true")
    args = parser.parse_args()
    if platform.system() != "Darwin":
        raise SystemExit("macOS is required")
    tests = ROOT / "out/personal-tests"
    subprocess.run([
        "cmake", "-S", str(ROOT / "fork/personal/tests"), "-B", str(tests),
        "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug",
    ], check=True)
    subprocess.run(["cmake", "--build", str(tests)], check=True)
    subprocess.run([
        "ctest", "--test-dir", str(tests), "--output-on-failure",
    ], check=True)
    build = ROOT / "out/personal-build"
    destination = ROOT / ("out/personal-test" if args.build_only else "out/personal") / f"{NAME}.app"
    env = os.environ.copy()
    if (build / "CMakeCache.txt").exists():
        env.pop("QT", None)
    else:
        env["QT"] = "6.11.2"
    test_api = env.get("TDESKTOP_API_TEST") == "1"
    if test_api:
        print("Developer build: limited upstream test API credentials; configure your own before normal use.", flush=True)
    subprocess.run([
        "cmake", "-S", str(ROOT), "-B", str(build), "-G", "Xcode",
        "-DDESKTOP_APP_DISABLE_AUTOUPDATE=ON",
        "-DDESKTOP_APP_SPECIAL_TARGET=mac",
        f"-DDESKTOP_APP_MAC_ARCH={platform.machine()}",
        f"-DCMAKE_OSX_ARCHITECTURES={platform.machine()}",
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0",
        f"-DTDESKTOP_API_TEST={'ON' if test_api else 'OFF'}",
        "-DCMAKE_BUILD_TYPE=Debug",
    ], env=env, check=True)
    subprocess.run([
        "cmake", "--build", str(build), "--config", "Debug", "--target", "Telegram", "-j", "6",
    ], check=True)
    source = build / "Debug" / f"{NAME}.app"
    if not (source / "Contents/MacOS" / NAME).is_file():
        raise SystemExit("The personal executable was not produced")
    if not (source / "Contents/Helpers/SeeGramModuleRunner").is_file():
        raise SystemExit("The module runner was not bundled")
    destination.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix="personal-stage-", dir=destination.parent))
    try:
        app = stage / destination.name
        subprocess.run(["ditto", str(source), str(app)], check=True)
        info = app / "Contents/Info.plist"
        with info.open("rb") as file:
            metadata = plistlib.load(file)
        identifier = BUNDLE_ID + (".test" if args.build_only else "")
        metadata.update(CFBundleIdentifier=identifier, CFBundleName=NAME, CFBundleDisplayName=NAME)
        metadata.pop("CFBundleURLTypes", None)
        with info.open("wb") as file:
            plistlib.dump(metadata, file)
        executable = app / "Contents/MacOS" / NAME
        subprocess.run(["strip", "-S", "-x", str(executable)], check=True)
        print(f"Mach-O mapped image: {verify_mapping(executable) // (1024 * 1024)} MiB", flush=True)
        subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(app)], check=True)
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True)
        profile = (destination.parent / "profile" if args.build_only
                   else Path.home() / "Library/Application Support/SeeGram Personal")
        portable = destination.parent / "TelegramForcePortable"
        if portable.is_symlink():
            if portable.resolve() != profile.resolve():
                raise SystemExit("The portable link points to a different profile")
        elif portable.exists():
            raise SystemExit("Preserving an existing portable folder; resolve its location first")
        profile.mkdir(parents=True, exist_ok=True)
        if not portable.is_symlink():
            portable.symlink_to(profile, target_is_directory=True)
        backup = destination.with_suffix(".app.previous")
        if destination.exists():
            if backup.exists():
                shutil.rmtree(backup)
            destination.rename(backup)
        try:
            app.rename(destination)
        except OSError:
            if backup.exists() and not destination.exists():
                backup.rename(destination)
            raise
    finally:
        shutil.rmtree(stage)
    print(f"{'Test' if args.build_only else 'Developer'} app: {destination}")
    print(f"Profile: {profile}")
    print("Automatic updates are disabled in this development build.")


if __name__ == "__main__":
    main()
