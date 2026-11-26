"""Build a signed OurOS repository from a JSON manifest and a P-256 key."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess


NAME = r"[a-z0-9][a-z0-9_-]{0,15}"
VERSION = r"[a-zA-Z0-9][a-zA-Z0-9_.+-]{0,14}"
FILE = r"[a-z0-9][a-z0-9_.-]{0,22}"
P256_SPKI = bytes.fromhex("3059301306072a8648ce3d020106082a8648ce3d03010703420004")


def field(value, pattern, width):
    if not isinstance(value, str) or not re.fullmatch(pattern, value):
        raise ValueError(f"Invalid repository label {value!r}")
    return value.encode("ascii").ljust(width, b"\0")


def identity(entry):
    return field(entry["name"], NAME, 17) + field(entry["version"], VERSION, 16)


def openssl(exe, args, data=None):
    return subprocess.run([exe, *args], input=data, stdout=subprocess.PIPE,
                          check=True).stdout


def compact_signature(der):
    if len(der) < 8 or der[0] != 0x30 or der[1] != len(der) - 2:
        raise ValueError("Unexpected ECDSA signature encoding")
    offset = 2
    parts = []
    for _ in range(2):
        if offset + 2 > len(der) or der[offset] != 2:
            raise ValueError("Missing ECDSA signature scalar")
        size = der[offset + 1]
        offset += 2
        if not 1 <= size <= 33 or offset + size > len(der):
            raise ValueError("Invalid ECDSA signature scalar length")
        scalar = der[offset:offset + size]
        offset += size
        if scalar[0] & 0x80 or (size == 33 and scalar[0] != 0):
            raise ValueError("Invalid ECDSA signature scalar")
        parts.append(scalar.lstrip(b"\0").rjust(32, b"\0"))
    if offset != len(der):
        raise ValueError("Trailing ECDSA signature data")
    return b"".join(parts)


def build(manifest_path, key, executable):
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    entries = manifest["packages"]
    if not isinstance(entries, list) or not 1 <= len(entries) <= 8:
        raise ValueError("A repository contains one to eight releases")
    public_der = openssl(executable, ["pkey", "-in", str(key), "-pubout", "-outform", "DER"])
    if len(public_der) != len(P256_SPKI) + 64 or not public_der.startswith(P256_SPKI):
        raise ValueError("Use an uncompressed P-256 signing key")
    public_key = public_der[len(P256_SPKI):]
    body = bytearray(b"OUROSRP1" + bytes([len(entries)]) + bytes(7) + public_key)
    seen = set()
    for entry in entries:
        ident = identity(entry)
        if ident in seen:
            raise ValueError("Duplicate package name and version")
        seen.add(ident)
        files = entry["files"]
        deps = entry.get("requires", [])
        if not isinstance(files, dict) or not 1 <= len(files) <= 4:
            raise ValueError("A release contains one to four files")
        if not isinstance(deps, list) or len(deps) > 3:
            raise ValueError("A release declares up to three dependencies")
        dep_names = {dep["name"] for dep in deps}
        if len(dep_names) != len(deps) or entry["name"] in dep_names:
            raise ValueError("Duplicate or self dependency")
        body.extend(ident + bytes([len(files), len(deps)]))
        for dep in deps:
            body.extend(identity(dep))
        for name, source in sorted(files.items()):
            member = manifest_path.parent / source
            if member.stat().st_size > 32768:
                raise ValueError(f"Member exceeds 32 KiB: {member}")
            data = member.read_bytes()
            if len(data) > 32768:
                raise ValueError(f"Member exceeds 32 KiB: {member}")
            body.extend(field(name, FILE, 24) + struct.pack("<I", len(data)))
            body.extend(hashlib.sha256(data).digest() + data)
    if len(body) + 64 > 65536:
        raise ValueError("Signed repository exceeds 64 KiB")
    signature = openssl(executable, ["dgst", "-sha256", "-sign", str(key)], body)
    return bytes(body) + compact_signature(signature), hashlib.sha256(public_key).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--key", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--openssl", default="C:/msys64/usr/bin/openssl.exe")
    args = parser.parse_args()
    try:
        data, pin = build(args.manifest, args.key, args.openssl)
        with args.output.open("xb") as output:
            output.write(data)
    except (ValueError, KeyError, TypeError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Repository build failed: {error}\n")
    print(f"pkgtrust {pin}")
    print(f"Signed repository {args.output} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
