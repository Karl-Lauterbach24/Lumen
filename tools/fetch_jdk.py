#!/usr/bin/env python3
"""Fetches an Eclipse Temurin JDK for this machine and prints the folder it was unpacked to.

    python tools/fetch_jdk.py <feature version, e.g. 17> <target folder>

Used by tools/build_deps.sh on Linux, whose build containers have no JDK: tools/make_jre.py cuts the
Java runtime for BD-J disc menus out of it. The download comes from the Adoptium API (latest release
of that Java version) and is checked against the SHA-256 sum the API names.
"""
import hashlib
import json
import os
import platform
import shutil
import sys
import tarfile
import urllib.request


def main():
    feature, target = sys.argv[1], sys.argv[2]
    arch = {"x86_64": "x64", "amd64": "x64", "aarch64": "aarch64", "arm64": "aarch64"}[platform.machine().lower()]
    system = {"Linux": "linux", "Darwin": "mac"}[platform.system()]
    api = (f"https://api.adoptium.net/v3/assets/latest/{feature}/hotspot?architecture={arch}"
           f"&image_type=jdk&os={system}&vendor=eclipse")
    req = urllib.request.Request(api, headers={"User-Agent": "Lumen build (https://github.com/Karl-Lauterbach24/Lumen)"})
    with urllib.request.urlopen(req, timeout=60) as r:
        package = json.load(r)[0]["binary"]["package"]
    os.makedirs(target, exist_ok=True)
    archive = os.path.join(target, package["name"])
    print(f"fetch_jdk: {package['name']} ({package['size'] // 1048576} MB)", file=sys.stderr)
    sha = hashlib.sha256()
    req = urllib.request.Request(package["link"], headers={"User-Agent": "Lumen build"})
    with urllib.request.urlopen(req, timeout=120) as r, open(archive, "wb") as f:
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            sha.update(chunk)
            f.write(chunk)
    if sha.hexdigest() != package["checksum"]:
        os.remove(archive)
        sys.exit(f"fetch_jdk: checksum of {package['name']} does not match")
    unpacked = os.path.join(target, "jdk")
    shutil.rmtree(unpacked, ignore_errors=True)
    with tarfile.open(archive) as t:
        top = t.getnames()[0].split("/")[0]
        t.extractall(target)
    os.remove(archive)
    os.rename(os.path.join(target, top), unpacked)
    home = os.path.join(unpacked, "Contents", "Home") if system == "mac" else unpacked
    print(home)


if __name__ == "__main__":
    main()
