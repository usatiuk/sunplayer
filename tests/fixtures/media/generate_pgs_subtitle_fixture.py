#!/usr/bin/env python3
"""Generate SunPlayer's deterministic PGS decoder fixture.

The output is intentionally assembled from documented PGS segments instead of
using FFmpeg (which has a PGS decoder but no encoder). It contains:

* 1.0 s: epoch start with two independently positioned bitmap objects;
* 3.0 s: normal composition replacing both with one object;
* 5.0 s: empty composition clearing the display.

Every object uses simple non-zero palette-index runs followed by the PGS 0,0
end-of-line marker. There is no compressed-source or decoder code shared with
the production FFmpeg path.
"""

from __future__ import annotations

import hashlib
import pathlib
import struct


CANVAS_WIDTH = 320
CANVAS_HEIGHT = 180
CLOCK = 90_000


def u24(value: int) -> bytes:
    return value.to_bytes(3, "big")


def segment(pts_seconds: int, kind: int, payload: bytes) -> bytes:
    timestamp = pts_seconds * CLOCK
    return (
        b"PG"
        + struct.pack(">II", timestamp, timestamp)
        + struct.pack(">BH", kind, len(payload))
        + payload
    )


def pcs(
    pts_seconds: int,
    composition_number: int,
    state: int,
    objects: list[tuple[int, int, int]],
) -> bytes:
    payload = struct.pack(
        ">HHBHBBB",
        CANVAS_WIDTH,
        CANVAS_HEIGHT,
        0x10,
        composition_number,
        state,
        0,
        0,
    ) + bytes([len(objects)])
    for object_id, x, y in objects:
        payload += struct.pack(">HBBHH", object_id, 0, 0, x, y)
    return segment(pts_seconds, 0x16, payload)


def window(pts_seconds: int) -> bytes:
    payload = bytes([1, 0]) + struct.pack(
        ">HHHH", 0, 0, CANVAS_WIDTH, CANVAS_HEIGHT
    )
    return segment(pts_seconds, 0x17, payload)


def palette(pts_seconds: int) -> bytes:
    # Palette words are Y, Cr, Cb, alpha. Entry 0 is transparent; entry 1 is
    # reference white; entry 2 is opaque yellow.
    payload = bytes(
        [
            0,
            0,
            0,
            16,
            128,
            128,
            0,
            1,
            235,
            128,
            128,
            255,
            2,
            210,
            146,
            16,
            255,
        ]
    )
    return segment(pts_seconds, 0x14, payload)


def object_data(
    pts_seconds: int,
    object_id: int,
    width: int,
    height: int,
    palette_index: int,
) -> bytes:
    rle = b"".join(
        bytes([palette_index]) * width + b"\x00\x00"
        for _ in range(height)
    )
    payload = (
        struct.pack(">HBB", object_id, 0, 0xC0)
        + u24(4 + len(rle))
        + struct.pack(">HH", width, height)
        + rle
    )
    return segment(pts_seconds, 0x15, payload)


def end(pts_seconds: int) -> bytes:
    return segment(pts_seconds, 0x80, b"")


def generate() -> bytes:
    return b"".join(
        [
            pcs(1, 1, 0x80, [(1, 30, 120), (2, 230, 30)]),
            window(1),
            palette(1),
            object_data(1, 1, 60, 20, 1),
            object_data(1, 2, 40, 24, 2),
            end(1),
            pcs(3, 2, 0x00, [(3, 110, 125)]),
            window(3),
            palette(3),
            object_data(3, 3, 100, 28, 1),
            end(3),
            pcs(5, 3, 0x00, []),
            end(5),
        ]
    )


def main() -> None:
    destination = pathlib.Path(__file__).with_name("subtitles-pgs.sup")
    payload = generate()
    destination.write_bytes(payload)
    digest = hashlib.sha256(payload).hexdigest()
    print(f"{destination.name} {len(payload)} bytes sha256={digest}")


if __name__ == "__main__":
    main()
