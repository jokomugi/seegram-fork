#!/usr/bin/env python3
"""Build and stage the isolated SeeGram Personal macOS app."""
import argparse
import os
from pathlib import Path
import platform
import re
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
    parser.add_argument("--configuration", choices=("Debug", "Release"), default="Debug")
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
    source_tests = ROOT / "out/personal-source-tests"
    subprocess.run([
        "cmake", "-S", str(ROOT / "fork/personal/tests/sources"), "-B", str(source_tests),
        "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug",
        f"-DCMAKE_PREFIX_PATH={ROOT.parent / 'Libraries/local/Qt-6.11.2'}",
        f"-DOPENSSL_ROOT_DIR={ROOT.parent / 'Libraries/openssl3'}",
    ], check=True)
    subprocess.run(["cmake", "--build", str(source_tests)], check=True)
    subprocess.run(["ctest", "--test-dir", str(source_tests), "--output-on-failure"], check=True)
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
        f"-DCMAKE_BUILD_TYPE={args.configuration}",
    ], env=env, check=True)
    subprocess.run([
        "cmake", "--build", str(build), "--config", args.configuration, "--target", "Telegram", "-j", "4",
    ], check=True)
    source = build / args.configuration / f"{NAME}.app"
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
        if destination.exists():
            custom_icon = destination / "Icon\r"
            if custom_icon.is_file():
                subprocess.run(["ditto", str(custom_icon), str(app / "Icon\r")], check=True)
                finder_info = subprocess.run(
                    ["xattr", "-px", "com.apple.FinderInfo", str(destination)],
                    capture_output=True, text=True,
                )
                if finder_info.returncode == 0:
                    subprocess.run([
                        "xattr", "-wx", "com.apple.FinderInfo",
                        finder_info.stdout.strip(), str(app),
                    ], check=True)
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
        if destination.exists():
            running = subprocess.run(
                ["pgrep", "-f", "^" + re.escape(str(destination / "Contents/MacOS" / NAME)) + "( |$)"],
                capture_output=True, text=True,
            )
            if running.returncode not in (0, 1):
                raise SystemExit("Could not verify whether the personal app is running; preserving the current app.")
            if running.returncode == 0:
                raise SystemExit("Close the running personal app before replacing it; the current app and profile were preserved.")
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
    if not args.build_only:
        registrar = Path("/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister")
        if registrar.is_file():
            subprocess.run([str(registrar), "-f", str(destination)], check=True)
    print(f"{'Test' if args.build_only else args.configuration} app: {destination}")
    print(f"Profile: {profile}")
    print("Automatic updates are disabled; updates use the personal release workflow.")


if __name__ == "__main__":
    main()
