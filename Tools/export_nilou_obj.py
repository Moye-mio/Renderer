# Convert Nilou FBX -> OBJ for 003_Toon_Shading.
#
# This repo's Assimp (assimp-vc140-mt) ACCESS_VIOLATION on the binary FBX.
# ufbx.load_memory() can read it; we bake the bind-pose meshes to OBJ so
# LoadModel() can go through the existing Assimp OBJ path.
#
# Head layout in this FBX: the `Body` mesh holds the skull and hair, while the
# face is three separate plates that all share Mat_Face -- `Face_Eye` (cheeks,
# sockets, eyeballs), `Face` (jaw, lips, oral cavity, skinned to ToothBone) and
# `Brow` (a 6 mm eyebrow decal). They stay separate groups here because M3 must
# exclude eyes and brows from the outline pass and M4 applies the face SDF only
# to the skin plate. Dropping any of them leaves a hole that shows the hair
# behind it.
#
# Each plate gets its own OBJ material so Assimp can tell them apart; all three
# point at the same Face diffuse, and every name still contains "Mat_Face" so
# NilouMaterials keeps routing them to that texture.
#
# The ufbx Python binding faults when several meshes are touched in one
# process, so each mesh is dumped by a short-lived child process.
#
# Requires: pip install ufbx

from __future__ import annotations

import os
import pickle
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FBX = os.path.join(REPO, "Model", "Nilou", "NPC_Avatar_Girl_Sword_Nilou.fbx")
OBJ = os.path.join(REPO, "Model", "Nilou", "Nilou.obj")
MTL = os.path.join(REPO, "Model", "Nilou", "Nilou.mtl")

PREFIX = "Avatar_Girl_Sword_Nilou_"
HAIR_TEX = PREFIX + "Tex_Hair_Diffuse.png"
BODY_TEX = PREFIX + "Tex_Body_Diffuse.png"
FACE_TEX = PREFIX + "Tex_Face_Diffuse.png"

# OBJ group -> (OBJ material name, diffuse png). Written in this order.
PARTS = [
    ("Hair",    PREFIX + "Mat_Hair",      HAIR_TEX),
    ("Body",    PREFIX + "Mat_Body",      BODY_TEX),
    ("Dress",   PREFIX + "Mat_Dress",     BODY_TEX),
    ("FaceEye", PREFIX + "Mat_Face_Eye",  FACE_TEX),
    ("Face",    PREFIX + "Mat_Face",      FACE_TEX),
    ("Brow",    PREFIX + "Mat_Face_Brow", FACE_TEX),
]

# Source mesh index -> {FBX material name: OBJ group}.
# 2 EffectMesh and 3 EyeStar are untextured placeholders and stay out.
SOURCE_MESHES = {
    0: {PREFIX + "Mat_Hair": "Hair",
        PREFIX + "Mat_Body": "Body",
        PREFIX + "Mat_Dress": "Dress"},
    1: {PREFIX + "Mat_Face": "Brow"},
    4: {PREFIX + "Mat_Face": "Face"},
    5: {PREFIX + "Mat_Face": "FaceEye"},
}


def dump_one(idx: int, out_path: str) -> None:
    import ufbx

    scene = ufbx.load_memory(open(FBX, "rb").read())
    mesh = scene.meshes[idx]
    mat_to_part = SOURCE_MESHES[idx]
    mat_names = [str(mesh.materials[i].name) for i in range(len(mesh.materials))]
    groups: dict[str, list] = {}
    unmapped: set[str] = set()
    has_fm = bool(mesh.face_material)

    for fi in range(mesh.num_faces):
        face = mesh.faces[fi]
        mat_idx = int(mesh.face_material[fi]) if has_fm else 0
        if mat_idx < 0 or mat_idx >= len(mat_names):
            continue
        part = mat_to_part.get(mat_names[mat_idx])
        if part is None:
            unmapped.add(mat_names[mat_idx])
            continue
        if face.num_indices < 3:
            continue
        for t in range(1, face.num_indices - 1):
            tri = []
            for corner in (face.index_begin,
                           face.index_begin + t,
                           face.index_begin + t + 1):
                p = ufbx.get_vertex_vec3(mesh.vertex_position, corner)
                n = ufbx.get_vertex_vec3(mesh.vertex_normal, corner)
                uv = ufbx.get_vertex_vec2(mesh.vertex_uv, corner)
                tri.append(((p.x, p.y, p.z), (n.x, n.y, n.z), (uv.x, uv.y)))
            groups.setdefault(part, []).append(tri)

    with open(out_path, "wb") as f:
        pickle.dump({"name": str(mesh.name), "groups": groups,
                     "unmapped": sorted(unmapped)}, f, protocol=4)
    # ufbx python objects can fault during interpreter teardown on this file.
    os._exit(0)


def main() -> int:
    merged: dict[str, list] = {name: [] for name, _, _ in PARTS}
    unmapped: set[str] = set()

    with tempfile.TemporaryDirectory() as tmp:
        for idx in sorted(SOURCE_MESHES):
            blob = os.path.join(tmp, "mesh%d.pkl" % idx)
            rc = subprocess.call(
                [sys.executable, os.path.abspath(__file__), "--dump", str(idx), blob])
            if rc != 0 or not os.path.exists(blob):
                print("dump failed for mesh %d (rc=%d)" % (idx, rc), file=sys.stderr)
                return 1
            with open(blob, "rb") as f:
                part = pickle.load(f)
            print("mesh[%d] %s" % (idx, part["name"]), flush=True)
            for group, tris in part["groups"].items():
                merged[group].extend(tris)
                print("   -> %-8s %6d tris" % (group, len(tris)), flush=True)
            unmapped.update(part["unmapped"])

    if unmapped:
        print("unmapped materials (dropped):", sorted(unmapped), flush=True)

    empty = [name for name, tris in merged.items() if not tris]
    if empty:
        print("empty groups:", empty, file=sys.stderr)
        return 1

    bb_min = [1e30] * 3
    bb_max = [-1e30] * 3

    with open(MTL, "w", encoding="utf-8", newline="\n") as mtl:
        mtl.write("# Nilou materials for 003_Toon_Shading (generated)\n")
        for _, mat_name, tex in PARTS:
            mtl.write("newmtl %s\n" % mat_name)
            mtl.write("Kd 1 1 1\n")
            mtl.write("map_Kd %s\n\n" % tex)

    with open(OBJ, "w", encoding="utf-8", newline="\n") as obj:
        obj.write("# Nilou bind pose (ufbx) for 003_Toon_Shading\n")
        obj.write("mtllib Nilou.mtl\n")
        vert_i = 1
        for group, mat_name, _ in PARTS:
            obj.write("o %s\n" % group)
            obj.write("g %s\n" % group)
            obj.write("usemtl %s\n" % mat_name)
            for tri in merged[group]:
                for p, n, uv in tri:
                    for k in range(3):
                        bb_min[k] = min(bb_min[k], p[k])
                        bb_max[k] = max(bb_max[k], p[k])
                    obj.write("v %.9f %.9f %.9f\n" % p)
                    obj.write("vn %.6f %.6f %.6f\n" % n)
                    obj.write("vt %.9f %.9f\n" % uv)
                obj.write("f %d/%d/%d %d/%d/%d %d/%d/%d\n" % (
                    vert_i, vert_i, vert_i,
                    vert_i + 1, vert_i + 1, vert_i + 1,
                    vert_i + 2, vert_i + 2, vert_i + 2,
                ))
                vert_i += 3

    print("wrote", OBJ, flush=True)
    print("groups", {k: len(v) for k, v in merged.items()}, flush=True)
    print("aabb min", ["%.3f" % x for x in bb_min], flush=True)
    print("aabb max", ["%.3f" % x for x in bb_max], flush=True)
    return 0


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "--dump":
        dump_one(int(sys.argv[2]), sys.argv[3])
    sys.exit(main())
