"""Regenerate the project's original, tiny PMX 2.0 regression fixtures.

Run with Python 3 from any working directory; only the standard library is used.
The cube geometry, materials and cutout pixels are authored here. The main
texture is a copy of the project's own assets/models/pad.png. No third-party
model or Assimp test asset is used.
"""

from pathlib import Path
import shutil
import struct
import zlib


HERE = Path(__file__).resolve().parent
TEXTURE = "pmx-textures\\彩色 pad%20.png"


def pmx(encoding, texture=TEXTURE, opacity=(1.0, 1.0), additional_uv=0, metadata=None):
    if not 0 <= additional_uv <= 4:
        raise ValueError("PMX permits zero to four additional UV channels")
    data = bytearray(b"PMX ")
    offsets = {} if metadata is None else metadata

    def write(fmt, *values):
        offset = len(data)
        data.extend(struct.pack("<" + fmt, *values))
        return offset

    def text(value):
        encoded = value.encode("utf-8" if encoding else "utf-16le")
        write("i", len(encoded))
        data.extend(encoded)

    write("f", 2.0)
    offsets["header_size"] = write("B", 8)
    offsets["header_encoding"] = write("B", encoding)
    offsets["header_uv"] = write("B", additional_uv)
    for kind in ("vertex", "texture", "material", "bone", "morph", "rigidbody"):
        offsets["header_" + kind + "_width"] = write("B", 4)

    for value in ("原创方块", "AEG original cube", "Original static import fixture", ""):
        text(value)

    # Four distinct vertices per face retain the sharp normal and UV seams.
    # Coordinates are PMX left-handed: x [-1,1], y [0,2], z [1,3].
    faces = (
        ((0, 0, 1), ((-1, 2, 3), (1, 2, 3), (1, 0, 3), (-1, 0, 3))),
        ((0, 0, -1), ((1, 2, 1), (-1, 2, 1), (-1, 0, 1), (1, 0, 1))),
        ((1, 0, 0), ((1, 2, 3), (1, 2, 1), (1, 0, 1), (1, 0, 3))),
        ((-1, 0, 0), ((-1, 2, 1), (-1, 2, 3), (-1, 0, 3), (-1, 0, 1))),
        ((0, 1, 0), ((-1, 2, 1), (1, 2, 1), (1, 2, 3), (-1, 2, 3))),
        ((0, -1, 0), ((-1, 0, 3), (1, 0, 3), (1, 0, 1), (-1, 0, 1))),
    )
    uvs = ((0, 0), (1, 0), (1, 1), (0, 1))
    write("i", 24)
    for normal, positions in faces:
        for position, uv in zip(positions, uvs):
            write("8f", *position, *normal, *uv)
            for channel in range(additional_uv):
                write("4f", (channel + 1) * 0.1, 0.2, 0.3, 0.4)
            offset = write("B", 0)  # BDEF1
            offsets.setdefault("first_vertex_skinning", offset)
            write("if", 0, 1.0)  # root bone 0, edge scale 1
    write("i", 36)
    offsets["first_vertex_index"] = len(data)
    for face in range(6):
        # Winding agrees with each supplied outward normal. Assimp's combined
        # handedness and winding conversion preserves that relationship.
        write("6i", *(face * 4 + index for index in (0, 2, 1, 0, 3, 2)))

    write("i", 1)
    text(texture)
    write("i", 2)
    for index, rgb in enumerate(((0.25, 0.5, 0.75), (0.8, 0.2, 0.1))):
        text(("贴图", "纯色")[index])
        text(("Textured", "Solid")[index])
        write("4f", *rgb, opacity[index])
        write("3ff3fB4ff", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0)
        offsets["material_texture_" + str(index)] = write("i", 0 if index == 0 else -1)
        write("iBBB", -1, 0, 1, 0)
        text("")
        offsets["material_span_" + str(index)] = write("i", 18)

    write("i", 1)
    text("根")
    text("Root")
    write("3f", 0, 0, 0)
    offsets["bone_parent_0"] = write("i", -1)
    write("iH3f", 0, 0, 0, 1, 0)
    write("4i", 0, 0, 0, 0)  # no morphs, display frames, rigid bodies or joints
    return data


def cutout_png():
    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload)))

    # Top row: transparent then opaque; bottom row: opaque then transparent.
    rows = bytes((0, 240, 30, 20, 0, 20, 220, 30, 255,
                  0, 30, 40, 230, 255, 240, 210, 30, 0))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">2I5B", 2, 2, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


if __name__ == "__main__":
    texture_dir = HERE / "pmx-textures"
    texture_dir.mkdir(exist_ok=True)
    shutil.copyfile(HERE.parents[1] / "assets/models/pad.png", texture_dir / "彩色 pad%20.png")
    (texture_dir / "cutout.png").write_bytes(cutout_png())
    (HERE / "pmx-utf8.pmx").write_bytes(pmx(1))
    (HERE / "pmx-utf16.pmx").write_bytes(pmx(0))
    (HERE / "pmx-missing-texture.pmx").write_bytes(pmx(1, "pmx-textures\\absent.png"))
    (HERE / "pmx-alpha.pmx").write_bytes(pmx(1, "pmx-textures\\cutout.png", (0.4, 0.0)))
    (HERE / "pmx-truncated.pmx").write_bytes(b"PMX " + struct.pack("<f", 2.0))
    (HERE / "pmx-extra-uv.pmx").write_bytes(pmx(1, additional_uv=4))

    # Mutate a single recorded field in an otherwise valid model. Header UV=5
    # intentionally has no extra payload: the engine must reject it before the
    # unsafe upstream vertex parser. Bad skinning must become a std::exception.
    variants = (
        ("pmx-bad-uv-header.pmx", "header_uv", "B", 5),
        ("pmx-bad-encoding.pmx", "header_encoding", "B", 2),
        ("pmx-bad-index-width.pmx", "header_vertex_width", "B", 3),
        ("pmx-bad-vertex-index.pmx", "first_vertex_index", "i", 24),
        ("pmx-bad-material-span.pmx", "material_span_0", "i", 21),
        ("pmx-bad-texture.pmx", "material_texture_0", "i", 1),
        ("pmx-bad-bone-parent.pmx", "bone_parent_0", "i", 0),
        ("pmx-bad-skinning.pmx", "first_vertex_skinning", "B", 5),
    )
    for filename, field, fmt, value in variants:
        metadata = {}
        data = pmx(1, metadata=metadata)
        struct.pack_into("<" + fmt, data, metadata[field], value)
        (HERE / filename).write_bytes(data)
