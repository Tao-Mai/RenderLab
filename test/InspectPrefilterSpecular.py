"""Print RGB peaks and their neighbors from the current prefiltered cubemap."""

import math
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FILE = ROOT / "assets/binary/texture/grasslands_sunset_4k_prefiltered_specular.bin"
FACES = ("+X", "-X", "+Y", "-Y", "+Z", "-Z")
HEADER = struct.Struct("<9I")
PIXEL = struct.Struct("<4f")


def neighbor_pixel(face: int, x: int, y: int, side: int) -> tuple[int, int, int]:
    if 0 <= x < side and 0 <= y < side:
        return face, x, y

    u = 2 * (x + 0.5) / side - 1
    v = 2 * (y + 0.5) / side - 1
    direction = (
        (1, -v, -u), (-1, -v, u), (u, 1, v),
        (u, -1, -v), (u, -v, 1), (-u, -v, -1),
    )[face]
    dx, dy, dz = direction
    ax, ay, az = map(abs, direction)

    if ax >= ay and ax >= az:
        next_face = 0 if dx >= 0 else 1
        next_u = (-dz if dx >= 0 else dz) / ax
        next_v = -dy / ax
    elif ay >= az:
        next_face = 2 if dy >= 0 else 3
        next_u = dx / ay
        next_v = (dz if dy >= 0 else -dz) / ay
    else:
        next_face = 4 if dz >= 0 else 5
        next_u = (dx if dz >= 0 else -dx) / az
        next_v = -dy / az

    next_x = min(side - 1, max(0, round((next_u + 1) * side / 2 - 0.5)))
    next_y = min(side - 1, max(0, round((next_v + 1) * side / 2 - 0.5)))
    return next_face, next_x, next_y


with FILE.open("rb") as source:
    magic, version, layout, format_, width, height, mip_levels, layers, payload_bytes = HEADER.unpack(
        source.read(HEADER.size)
    )
    assert (magic, version, layers) == (0x58455452, 2, 6)
    assert (width, height, mip_levels) == (1024, 1024, 11)
    assert FILE.stat().st_size == HEADER.size + payload_bytes

    mip_offset = HEADER.size
    for mip in range(mip_levels):
        side = max(1, width >> mip)
        level_bytes = side * side * layers * PIXEL.size

        if mip in (6, 7, 8):
            source.seek(mip_offset)
            pixels = list(PIXEL.iter_unpack(source.read(level_bytes)))
            assert len(pixels) == side * side * layers

            per_channel = [(-math.inf, -1) for _ in range(3)]
            for index, rgba in enumerate(pixels):
                for channel in range(3):
                    value = rgba[channel]
                    assert math.isfinite(value), (mip, index, channel, value)
                    if value > per_channel[channel][0]:
                        per_channel[channel] = (value, index)

            peak_channel = max(range(3), key=lambda channel: per_channel[channel][0])
            peak_value, peak_index = per_channel[peak_channel]
            face, face_index = divmod(peak_index, side * side)
            y, x = divmod(face_index, side)

            print(f"mip {mip}: {side}x{side}x6, roughness={mip / (mip_levels - 1):.1f}")
            for channel, (value, index) in zip("RGB", per_channel):
                channel_face, channel_index = divmod(index, side * side)
                channel_y, channel_x = divmod(channel_index, side)
                print(f"  {channel} max = {value:.9g} at {FACES[channel_face]} ({channel_x}, {channel_y})")
            print(f"  overall RGB max = {peak_value:.9g} ({'RGB'[peak_channel]})")
            print(f"  3x3 around {FACES[face]} ({x}, {y}); edges map to adjacent faces:")
            for neighbor_y in range(y - 1, y + 2):
                for neighbor_x in range(x - 1, x + 2):
                    neighbor_face, pixel_x, pixel_y = neighbor_pixel(
                        face, neighbor_x, neighbor_y, side
                    )
                    index = (neighbor_face * side + pixel_y) * side + pixel_x
                    rgba = pixels[index]
                    print(
                        f"    {FACES[neighbor_face]} ({pixel_x:2}, {pixel_y:2}) "
                        f"RGBA=({rgba[0]:.9g}, {rgba[1]:.9g}, {rgba[2]:.9g}, {rgba[3]:.9g})"
                    )
            print()

        mip_offset += level_bytes

    assert mip_offset == FILE.stat().st_size
