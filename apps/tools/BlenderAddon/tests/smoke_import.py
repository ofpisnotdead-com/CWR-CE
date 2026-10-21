"""Check a packaged add-on's archive browser, mesh import and textures."""
import argparse
from pathlib import Path
import sys
import tempfile
import zipfile

import bpy


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--addon", required=True, type=Path)
parser.add_argument("--game-data", required=True, type=Path)
parser.add_argument("--model", default="data3d/str borovice.p3d")
parser.add_argument("--output", type=Path)
args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])

with tempfile.TemporaryDirectory(prefix="poseidon-addon-test-") as temporary:
    with zipfile.ZipFile(args.addon) as archive:
        archive.extractall(temporary)
    sys.path.insert(0, temporary)
    import io_import_p3d

    io_import_p3d.register()
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.context.scene.poseidon.game_data_dir = str(args.game_data.resolve())
    assert bpy.ops.poseidon.scan_vfs() == {"FINISHED"}
    items = bpy.context.scene.poseidon.vfs_p3d_list
    index = next(i for i, item in enumerate(items) if item.vfs_path == args.model)
    bpy.context.scene.poseidon.vfs_p3d_index = index
    assert bpy.ops.poseidon.import_vfs_p3d() == {"FINISHED"}
    meshes = [obj for obj in bpy.data.objects if obj.type == "MESH" and "cwr_lod_index" in obj]
    assert meshes and sum(len(obj.data.polygons) for obj in meshes) > 0
    materials = [mat for obj in meshes for mat in obj.data.materials if mat]
    textures = [node.image for mat in materials if mat.use_nodes
                for node in mat.node_tree.nodes if node.type == "TEX_IMAGE" and node.image]
    assert textures and all(image.size[0] > 0 and image.size[1] > 0 for image in textures)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(args.output.resolve()))
    print(f"PASS: {len(items)} models scanned; {args.model}; {len(meshes)} LODs; {len(textures)} textures")
    io_import_p3d.unregister()
