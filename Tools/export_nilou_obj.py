# Convert Nilou FBX -> OBJ for 003_Toon_Shading.
#
# This repo's Assimp (assimp-vc140-mt) ACCESS_VIOLATION on the binary FBX.
# ufbx.load_memory() can read it; we bake T-pose bind-pose meshes to OBJ so
# LoadModel() can go through the existing Assimp OBJ path.
#
# Only Body + Face ufbx meshes. Body is split by material into Hair/Body/Dress.
# Skip Face_Eye / EffectMesh / EyeStar / Brow.
# Requires: pip install ufbx

from __future__ import annotations

import os
import sys

import ufbx

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FBX = os.path.join(REPO, "Model", "Nilou", "NPC_Avatar_Girl_Sword_Nilou.fbx")
OBJ = os.path.join(REPO, "Model", "Nilou", "Nilou.obj")
MTL = os.path.join(REPO, "Model", "Nilou", "Nilou.mtl")

# FBX material name -> OBJ object / group name (NilouMaterials KeepMesh keys).
MAT_TO_PART = {
    "Avatar_Girl_Sword_Nilou_Mat_Hair": "Hair",
    "Avatar_Girl_Sword_Nilou_Mat_Body": "Body",
    "Avatar_Girl_Sword_Nilou_Mat_Dress": "Dress",
    "Avatar_Girl_Sword_Nilou_Mat_Face": "Face",
}

DIFFUSE_FOR_MAT = {
    "Avatar_Girl_Sword_Nilou_Mat_Hair": "Avatar_Girl_Sword_Nilou_Tex_Hair_Diffuse.png",
    "Avatar_Girl_Sword_Nilou_Mat_Body": "Avatar_Girl_Sword_Nilou_Tex_Body_Diffuse.png",
    "Avatar_Girl_Sword_Nilou_Mat_Dress": "Avatar_Girl_Sword_Nilou_Tex_Body_Diffuse.png",
    "Avatar_Girl_Sword_Nilou_Mat_Face": "Avatar_Girl_Sword_Nilou_Tex_Face_Diffuse.png",
}

# Do not iterate scene.meshes: some deformer meshes trip the Python binding.
SOURCE_MESH_INDICES = (0, 4)  # Body, Face


def extract_mesh(mesh: ufbx.Mesh) -> dict[str, list]:
    mat_names = [str(mesh.materials[i].name) for i in range(len(mesh.materials))]
    groups: dict[str, list] = {name: [] for name in MAT_TO_PART}
    nfaces = mesh.num_faces
    has_fm = bool(mesh.face_material)
    for fi in range(nfaces):
        face = mesh.faces[fi]
        mat_idx = int(mesh.face_material[fi]) if has_fm else 0
        if mat_idx < 0 or mat_idx >= len(mat_names):
            continue
        mat_name = mat_names[mat_idx]
        if mat_name not in MAT_TO_PART:
            continue
        if face.num_indices < 3:
            continue
        for t in range(1, face.num_indices - 1):
            corners = (
                face.index_begin + 0,
                face.index_begin + t,
                face.index_begin + t + 1,
            )
            tri = []
            for idx in corners:
                p = ufbx.get_vertex_vec3(mesh.vertex_position, idx)
                n = ufbx.get_vertex_vec3(mesh.vertex_normal, idx)
                uv = ufbx.get_vertex_vec2(mesh.vertex_uv, idx)
                tri.append(((p.x, p.y, p.z), (n.x, n.y, n.z), (uv.x, uv.y)))
            groups[mat_name].append(tri)
    return groups


def main() -> int:
    print("loading", FBX, flush=True)
    data = open(FBX, "rb").read()
    scene = ufbx.load_memory(data)
    print("loaded", flush=True)

    merged: dict[str, list] = {name: [] for name in MAT_TO_PART}
    for idx in SOURCE_MESH_INDICES:
        mesh = scene.meshes[idx]
        print("extract", idx, mesh.name, "faces", mesh.num_faces, flush=True)
        part = extract_mesh(mesh)
        for k, tris in part.items():
            merged[k].extend(tris)
            if tris:
                print("  ", k, len(tris), flush=True)

    counts = {MAT_TO_PART[k]: len(v) for k, v in merged.items()}
    if any(c == 0 for c in counts.values()):
        print("missing parts:", counts, file=sys.stderr)
        return 1

    bb_min = [1e30, 1e30, 1e30]
    bb_max = [-1e30, -1e30, -1e30]

    print("writing", MTL, flush=True)
    with open(MTL, "w", encoding="utf-8", newline="\n") as mtl:
        mtl.write("# Nilou materials for 003_Toon_Shading (generated)\n")
        for mat_name, tex in DIFFUSE_FOR_MAT.items():
            mtl.write("newmtl %s\n" % mat_name)
            mtl.write("Kd 1 1 1\n")
            mtl.write("map_Kd %s\n\n" % tex)

    print("writing", OBJ, flush=True)
    with open(OBJ, "w", encoding="utf-8", newline="\n") as obj:
        obj.write("# Nilou T-pose (ufbx bind pose) for 003_Toon_Shading\n")
        obj.write("mtllib Nilou.mtl\n")
        vert_i = 1
        for mat_name, part in MAT_TO_PART.items():
            tris = merged[mat_name]
            obj.write("o %s\n" % part)
            obj.write("g %s\n" % part)
            obj.write("usemtl %s\n" % mat_name)
            for tri in tris:
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
    print("wrote", MTL, flush=True)
    print("triangles", counts, flush=True)
    print("aabb min", bb_min, flush=True)
    print("aabb max", bb_max, flush=True)
    print("height", bb_max[1] - bb_min[1], flush=True)
    return 0


if __name__ == "__main__":
    code = 1
    try:
        code = main()
    except Exception as exc:
        print("export failed:", exc, file=sys.stderr)
        raise
    finally:
        # ufbx python objects can ACCESS_VIOLATION during GC on this file.
        os._exit(code)
