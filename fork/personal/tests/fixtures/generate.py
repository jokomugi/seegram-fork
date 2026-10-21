#!/usr/bin/env python3
"""Generate synthetic, unusable authorizations using an independent AES-ECB IGE fixture writer."""
import hashlib
from pathlib import Path
import struct
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parent


def integer(value):
    return struct.pack(">I", value)


def wide(value):
    return struct.pack(">Q", value)


def blob(value):
    return integer(len(value)) + value


def string(value):
    return blob(value.encode("utf-16-be"))


def digest(value):
    return hashlib.sha1(value).digest()


def xor(left, right):
    return bytes(a ^ b for a, b in zip(left, right))


def encrypt(value, key):
    plain = struct.pack("<I", len(value) + 4) + value
    plain += bytes((-len(plain)) % 16)
    message = digest(plain)[:16]
    a = digest(message + key[8:40])
    b = digest(key[40:56] + message + key[56:72])
    c = digest(key[72:104] + message)
    d = digest(message + key[104:136])
    aes = a[:8] + b[8:20] + c[4:16]
    iv = a[8:20] + b[:8] + c[16:20] + d[:8]
    previous_cipher, previous_plain = iv[:16], iv[16:]
    output = bytearray(message)
    for offset in range(0, len(plain), 16):
        block = plain[offset:offset + 16]
        encrypted = subprocess.run([
            "openssl", "enc", "-aes-256-ecb", "-K", aes.hex(), "-nopad",
        ], input=xor(block, previous_cipher), capture_output=True, check=True).stdout
        previous_cipher = xor(encrypted, previous_plain)
        previous_plain = block
        output.extend(previous_cipher)
    return bytes(output)


def derive(passcode, salt):
    value = hashlib.sha512(salt + passcode + salt).digest()
    return hashlib.pbkdf2_hmac("sha512", value, salt, 100000 if passcode else 1, 256)


def envelope(payload):
    version = struct.pack("<I", 7003000)
    checksum = hashlib.md5(payload + struct.pack("<I", len(payload)) + version + b"TDF$").digest()
    return b"TDF$" + version + payload + checksum


def data_name(index):
    name = b"data" + (b"#" + str(index + 1).encode() if index else b"")
    value = int.from_bytes(hashlib.md5(name).digest()[:8], "little")
    return "".join("0123456789ABCDEF"[(value >> (i * 4)) & 15] for i in range(16))


def authorization(user):
    return wide(0xffffffffffffffff) + wide(user) + integer(2) + integer(1) + integer(2) + bytes(256) + integer(0)


def main():
    local = bytes(range(256))
    salt = bytes(range(32))
    profile_id = bytes(range(16))
    profile_wrapped = encrypt(local + blob(profile_id), derive(b"profile", salt))
    info = integer(2) + integer(0) + integer(1) + integer(0)
    info += integer(0x53475031) + integer(1) + blob(profile_id) + string("Fixture")
    info += integer(1) + integer(1) + blob(salt) + blob(profile_wrapped)
    key_data = blob(salt) + blob(encrypt(local, derive(b"fixture", salt))) + blob(encrypt(info, local))
    key_data += blob(integer(1) + blob(salt) + blob(profile_wrapped))
    files = {"tdata/key_datas": envelope(key_data)}
    for index, user in enumerate((123, 456)):
        name = data_name(index)
        files[f"tdata/{name}s"] = envelope(blob(encrypt(integer(0x4b) + blob(authorization(user)), local)))
        peer = wide(user | (0x80 << 48)) + wide(0x77ffffffffffffff) + integer(3) + wide(0)
        peer += integer(0x80000000) + blob(b"") + integer(0)
        peer += string("Fixture") + string(str(index + 1)) + string("") + string("fixture")
        files[f"tdata/{name}/maps"] = envelope(blob(b"") + blob(b"") + blob(encrypt(integer(0x15) + blob(peer), local)))
    rotated = {name: value for name, value in files.items() if name != "tdata/key_datas"}
    for suffix, index in (("0", 1), ("1", 0)):
        registry = integer(1) + integer(index) + integer(index)
        data = blob(salt) + blob(encrypt(local, derive(b"fixture", salt))) + blob(encrypt(registry, local))
        rotated["tdata/key_data" + suffix] = envelope(data)
    for name, entries in (
        ("encrypted-tdata.zip", files),
        ("rotated-tdata.zip", rotated),
        ("traversal.zip", {"../outside": b"no", **files}),
        ("multiple-roots.zip", {**files, **{"copy/" + key: value for key, value in files.items()}}),
    ):
        with zipfile.ZipFile(ROOT / name, "w", zipfile.ZIP_DEFLATED) as archive:
            for path, value in entries.items():
                info = zipfile.ZipInfo(path, (2021 if path.endswith("key_data1") else 2022, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(info, value)


if __name__ == "__main__":
    main()
