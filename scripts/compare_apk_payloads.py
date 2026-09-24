#!/usr/bin/env python3
import hashlib
import pathlib
import sys
import zipfile


def sha256(content):
    return hashlib.sha256(content).hexdigest()


def read_payload(path):
    with zipfile.ZipFile(path) as archive:
        result = []
        for info in archive.infolist():
            metadata = (
                info.filename,
                info.date_time,
                info.compress_type,
                info.external_attr,
                info.flag_bits,
            )
            result.append((metadata, sha256(archive.read(info.filename))))
        return result


def compare_payloads(first_path, second_path):
    first = read_payload(first_path)
    second = read_payload(second_path)
    if len(first) != len(second):
        raise ValueError("APK entry count differs")

    manifest = []
    for first_entry, second_entry in zip(first, second):
        first_metadata, first_hash = first_entry
        second_metadata, second_hash = second_entry
        name = first_metadata[0]
        if first_metadata != second_metadata or first_hash != second_hash:
            raise ValueError(f"APK payload differs at {name}")
        manifest.append((name, first_hash))
    return manifest


def main(argv):
    if len(argv) != 4:
        raise SystemExit("usage: compare_apk_payloads.py FIRST.apk SECOND.apk MANIFEST")
    manifest = compare_payloads(argv[1], argv[2])
    output = "".join(f"{digest}  {name}\n" for name, digest in manifest)
    pathlib.Path(argv[3]).write_text(output, encoding="utf-8")
    print("APK payload is reproducible; RSA-PSS signing bytes are intentionally excluded")


if __name__ == "__main__":
    main(sys.argv)
