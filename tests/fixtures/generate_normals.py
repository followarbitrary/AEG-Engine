"""Regenerate AEG's original tiny normal-map import fixtures.

Run with Python 3 from any directory. Only the standard library is required.
All geometry, normal pixels and material documents are authored below; no
third-party assets are copied. glTF and GLB embed the same PNG and geometry.
"""

import base64
import copy
import json
import math
from pathlib import Path
import struct
import zlib


HERE = Path(__file__).resolve().parent
PIXELS = bytes((128, 128, 255, 255, 220, 128, 210, 255,
                128, 220, 210, 255, 32, 96, 220, 255))


def png():
    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload)))

    rows = b"\0" + PIXELS[:8] + b"\0" + PIXELS[8:]
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">2I5B", 2, 2, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


IMAGE = png()


def document(positions=None, uvs=None, indices=None, normals=None, tangents=None,
             no_uv=False, scale=0.375, node_scale=None, texture_info=None):
    positions = positions or ((0, 0, 0), (1, 0, 0), (0, 1, 0))
    uvs = uvs or ((0, 0), (1, 0), (0, 1))
    indices = indices or (0, 1, 2)
    normals = normals or ((0, 0, 1),) * len(positions)
    data = bytearray()
    views = []
    accessors = []

    def add(values, width, kind="f", target=34962, bounds=False):
        while len(data) % 4:
            data.append(0)
        offset = len(data)
        flat = list(values) if width == 1 else [v for row in values for v in row]
        data.extend(struct.pack("<" + kind * len(flat), *flat))
        views.append({"buffer": 0, "byteOffset": offset,
                      "byteLength": len(data) - offset, "target": target})
        accessor = {"bufferView": len(views) - 1,
                    "componentType": 5126 if kind == "f" else 5123,
                    "count": len(values), "type": "SCALAR" if width == 1 else "VEC" + str(width)}
        if bounds:
            accessor["min"] = [min(v[axis] for v in values) for axis in range(width)]
            accessor["max"] = [max(v[axis] for v in values) for axis in range(width)]
        accessors.append(accessor)
        return len(accessors) - 1

    attributes = {"POSITION": add(positions, 3, bounds=True), "NORMAL": add(normals, 3)}
    if not no_uv:
        attributes["TEXCOORD_0"] = add(uvs, 2)
        attributes["TEXCOORD_1"] = attributes["TEXCOORD_0"]
    if tangents:
        attributes["TANGENT"] = add(tangents, 4)
    index_accessor = add(indices, 1, kind="H", target=34963)
    info = {"index": 0, "scale": scale}
    if texture_info:
        info.update(texture_info)
    doc = {"asset": {"version": "2.0", "generator": "AEG original normal fixtures"},
           "buffers": [{"byteLength": len(data),
                        "uri": "data:application/octet-stream;base64," + base64.b64encode(data).decode()}],
           "bufferViews": views, "accessors": accessors,
           "images": [{"uri": "data:image/png;base64," + base64.b64encode(IMAGE).decode()}],
           "textures": [{"source": 0}],
           "materials": [{"name": "Original normal material", "normalTexture": info}],
           "meshes": [{"name": "Original normal mesh", "primitives": [
               {"attributes": attributes, "indices": index_accessor, "material": 0}]}],
           "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0}
    if node_scale:
        doc["nodes"][0]["scale"] = node_scale
    return doc, bytes(data)


def write(name, **kwargs):
    doc, data = document(**kwargs)
    (HERE / (name + ".gltf")).write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
    return doc, data


def glb(doc, data):
    doc = copy.deepcopy(doc)
    binary = bytearray(data)
    while len(binary) % 4:
        binary.append(0)
    image_offset = len(binary)
    binary.extend(IMAGE)
    doc["bufferViews"].append({"buffer": 0, "byteOffset": image_offset, "byteLength": len(IMAGE)})
    doc["images"] = [{"bufferView": len(doc["bufferViews"]) - 1, "mimeType": "image/png"}]
    doc["buffers"] = [{"byteLength": len(binary)}]
    while len(binary) % 4:
        binary.append(0)
    text = json.dumps(doc, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    return (struct.pack("<3I", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(binary))
            + struct.pack("<2I", len(text), 0x4E4F534A) + text
            + struct.pack("<2I", len(binary), 0x004E4942) + binary)


def obj(name, directive):
    (HERE / (name + ".obj")).write_text(
        "mtllib " + name + ".mtl\n"
        "o Original_normal_triangle\n"
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "vt 0 1\nvt 1 1\nvt 0 0\nvn 0 0 1\n"
        "usemtl Original_normal_material\nf 1/1/1 2/2/1 3/3/1\n", encoding="utf-8")
    (HERE / (name + ".mtl")).write_text(
        "newmtl Original_normal_material\nKd 1 1 1\n" + directive + " normal-map.png\n", encoding="utf-8")


def main():
    (HERE / "normal-map.png").write_bytes(IMAGE)
    doc, data = write("normal-map")
    (HERE / "normal-map.glb").write_bytes(glb(doc, data))
    obj("normal-map", "norm")
    obj("normal-map-kn", "map_Kn")
    obj("normal-height", "map_bump")
    obj("normal-obj-transform", "norm -s 2 2 1")
    write("normal-scale-zero", scale=0)
    write("normal-scale-negative", scale=-1.5)
    missing, _ = document()
    missing["images"][0]["uri"] = "normal-deliberately-missing.png"
    (HERE / "normal-missing.gltf").write_text(json.dumps(missing, indent=2) + "\n", encoding="utf-8")
    write("normal-uv1", texture_info={"texCoord": 1})
    transformed, _ = document(texture_info={"extensions": {"KHR_texture_transform": {"offset": [0.25, 0]}}})
    transformed["extensionsUsed"] = ["KHR_texture_transform"]
    (HERE / "normal-uv-transform.gltf").write_text(json.dumps(transformed, indent=2) + "\n", encoding="utf-8")
    quad = ((0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0))
    write("normal-mirrored-uv", positions=quad, uvs=((0, 0), (1, 0), (0, 1), (1, 0)),
          indices=(0, 1, 2, 0, 2, 3))
    write("normal-degenerate-uv", positions=quad, uvs=((0, 0),) * 4,
          indices=(0, 1, 2, 0, 2, 3))
    write("normal-mixed-uv", positions=quad, uvs=((0, 0), (1, 0), (0, 1), (0, 2)),
          indices=(0, 1, 2, 0, 2, 3))
    write("normal-no-uv", no_uv=True)
    diagonal = 1 / math.sqrt(2)
    write("normal-nearly-parallel", positions=((0, 0, 0), (diagonal, diagonal, 1e-13), (-diagonal, diagonal, 0)),
          normals=((diagonal, diagonal, 0),) * 3)
    length = math.sqrt(6)
    for name, node_scale in (("normal-nonuniform", (2, 1, 0.5)),
                             ("normal-mirrored-node", (-2, 1, 0.5))):
        write(name, positions=((0, 0, 0), (1, 1, 0), (-1, 1, 1)),
              normals=((1 / length, -1 / length, 2 / length),) * 3,
              tangents=((1 / math.sqrt(2), 1 / math.sqrt(2), 0, 1),) * 3,
              node_scale=node_scale)


if __name__ == "__main__":
    main()
