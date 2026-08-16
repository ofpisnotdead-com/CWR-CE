import struct
from pathlib import Path


def fixed_string(value: str, size: int) -> bytes:
    encoded = value.encode("ascii")
    if len(encoded) >= size:
        raise ValueError(f"{value!r} does not fit in {size} bytes")
    return encoded + bytes(size - len(encoded))


output = bytearray(b"4WVR")
output += struct.pack("<ii", 4, 4)
output += struct.pack("<16h", *range(16))
output += struct.pack("<16h", *([0] * 16))
output += fixed_string(r"landtext\mo.pac", 32)
output += bytes(511 * 32)

for object_id, x, z in ((17, 100.0, 200.0), (2, 150.0, 250.0), (9, 200.0, 300.0)):
    output += struct.pack("<12f", 1, 0, 0, 0, 1, 0, 0, 0, 1, x, 0, z)
    output += struct.pack("<i", object_id)
    output += fixed_string(rf"data3d\dummy{object_id}.p3d", 76)

Path(__file__).with_name("test_world.wrp").write_bytes(output)
