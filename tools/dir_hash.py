#!/usr/bin/env python

import os
import hashlib
import zlib
from sys import argv


default_hash_type = 'blake2s'


def iter_file_chunks(dir, chunk_size=8192):
    for root, _, files in os.walk(dir):
        for file in files:
            file_path = os.path.join(root, file)
            with open(file_path, 'rb') as f:
                while chunk := f.read(chunk_size):
                    yield chunk


def calc_crc32(dir):
    crc = 0
    for chunk in iter_file_chunks(dir):
        crc = zlib.crc32(chunk, crc)
    return f"{crc:08x}"


def calc_hash(dir, hash_type):
    hash_func = getattr(hashlib, hash_type, None)
    if not hash_func:
        raise Exception(f"Unsupported hash type: {hash_type}")

    hasher = hash_func()
    for chunk in iter_file_chunks(dir):
        hasher.update(chunk)
    return hasher.hexdigest()


def main(dir, hash_type=default_hash_type):
    hash_type = argv[2] if len(argv) > 2 else default_hash_type

    if hash_type == "crc32":
        print(calc_crc32(dir))
        return

    print(calc_hash(dir, hash_type))


if __name__ == "__main__":
    if len(argv) < 2:
        print("Usage: dir_hash.py <directory> <hash_type | crc32>")
        exit(1)

    main(argv[1], argv[2] if len(argv) > 2 else default_hash_type)
    exit(0)