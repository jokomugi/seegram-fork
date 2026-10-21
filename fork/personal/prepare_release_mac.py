#!/usr/bin/env python3
"""Prepare optimized native dependencies while preserving installed Debug libraries."""
import platform
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LIBRARIES = ROOT.parent / "Libraries"


def run(*args):
    subprocess.run([str(value) for value in args], check=True)


def native_library(name, outputs, options=(), targets=()):
    source = LIBRARIES / name
    output = source / "out/Release"
    required = [output / relative for relative in outputs]
    if all(path.is_file() and path.stat().st_size for path in required):
        return
    print(f"Preparing Release {name}.", flush=True)
    run("cmake", "-S", source, "-B", output, "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0",
        f"-DCMAKE_OSX_ARCHITECTURES={platform.machine()}", *options)
    run("cmake", "--build", output, "--parallel", "4", "--target", *targets)
    if not all(path.is_file() and path.stat().st_size for path in required):
        raise SystemExit(f"Release {name} preparation did not produce every required archive.")


def prepare():
    if platform.system() != "Darwin":
        raise SystemExit("macOS is required")
    qt = LIBRARIES / "qt_6.11.2"
    prefix = LIBRARIES / "local/Qt-6.11.2"
    required = [prefix / "lib" / f"libQt6{name}.a" for name in (
        "Core", "Gui", "Widgets", "Network", "Svg", "OpenGL", "OpenGLWidgets",
    )]
    required += [prefix / name for name in (
        "plugins/platforms/libqcocoa.a", "plugins/imageformats/libqwebp.a",
        "plugins/imageformats/libqgif.a", "plugins/imageformats/libqjpeg.a",
        "plugins/tls/libqsecuretransportbackend.a",
        "plugins/networkinformation/libqapplenetworkinformation.a",
    )]
    if not all(path.is_file() for path in required):
        if not (qt / "CMakeCache.txt").is_file():
            raise SystemExit("Prepare the repository's Qt dependencies first.")
        debug_exports = {
            path.relative_to(prefix): path.read_bytes()
            for path in prefix.rglob("*-debug.cmake")
        }
        print("Preparing Release Qt; installed Debug archives remain available.", flush=True)
        run("cmake", "-S", qt, "-B", qt,
            "-DCMAKE_BUILD_TYPE=Release", "-DFEATURE_debug=OFF",
            "-DFEATURE_debug_and_release=OFF", "-DFEATURE_force_debug_info=OFF",
            f"-DCMAKE_OSX_ARCHITECTURES={platform.machine()}")
        run("cmake", "--build", qt, "--parallel", "4")
        try:
            run("cmake", "--install", qt)
        finally:
            for relative, contents in debug_exports.items():
                target = prefix / relative
                if not target.exists():
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(contents)
    crashpad = LIBRARIES / "crashpad"
    output = crashpad / "out/Release"
    crashpad_required = [output / name for name in (
        "libcrashpad_client.a", "crashpad_handler",
    )]
    if not all(path.is_file() and path.stat().st_size for path in crashpad_required):
        run("cmake", "-S", crashpad, "-B", output, "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_OSX_ARCHITECTURES={platform.machine()}",
            "-DCRASHPAD_SPECIAL_TARGET=mac",
            f"-DCRASHPAD_ZLIB_INCLUDE_PATH={LIBRARIES / 'local/include'}",
            f"-DCRASHPAD_ZLIB_LIB_PATH={LIBRARIES / 'local/lib/libz.a'}")
        run("cmake", "--build", output, "--parallel", "4")
    if not all(path.is_file() for path in required):
        raise SystemExit("Release Qt preparation did not produce every required archive.")
    if not all(path.is_file() and path.stat().st_size for path in crashpad_required):
        raise SystemExit("Release Crashpad preparation did not produce the client and handler.")
    native_library("rnnoise", ("librnnoise.a",), targets=("rnnoise",))
    native_library("tde2e", ("tde2e/libtde2e.a", "tdutils/libtdutils.a"), (
        "-DOPENSSL_FOUND=1",
        f"-DOPENSSL_INCLUDE_DIR={LIBRARIES / 'openssl3/include'}",
        f"-DOPENSSL_SSL_LIBRARY={LIBRARIES / 'openssl3/libssl.a'}",
        f"-DOPENSSL_CRYPTO_LIBRARY={LIBRARIES / 'openssl3/libcrypto.a'}",
        "-DZLIB_FOUND=1",
        f"-DZLIB_LIBRARIES={LIBRARIES / 'local/lib/libz.a'}",
        "-DTD_E2E_ONLY=ON",
    ), ("tde2e", "tdutils"))
    native_library("tg_owt", ("libtg_owt.a",), (
        "-DTG_OWT_SPECIAL_TARGET=mac", "-DTG_OWT_BUILD_AUDIO_BACKENDS=OFF",
        f"-DTG_OWT_LIBJPEG_INCLUDE_PATH={LIBRARIES / 'local/include'}",
        f"-DTG_OWT_OPENSSL_INCLUDE_PATH={LIBRARIES / 'openssl3/include'}",
        f"-DTG_OWT_OPUS_INCLUDE_PATH={LIBRARIES / 'local/include/opus'}",
        f"-DTG_OWT_LIBVPX_INCLUDE_PATH={LIBRARIES / 'local/include'}",
        f"-DTG_OWT_OPENH264_INCLUDE_PATH={LIBRARIES / 'local/include'}",
        f"-DTG_OWT_FFMPEG_INCLUDE_PATH={LIBRARIES / 'local/include'}",
    ), ("tg_owt",))


if __name__ == "__main__":
    prepare()
