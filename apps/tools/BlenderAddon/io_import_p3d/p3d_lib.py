import ctypes
import os
from ctypes import POINTER, c_char_p, c_float, c_int, c_uint8, c_uint32, c_void_p

_dll = None


def _find_dll():
    """Locate PoseidonFormats shared library next to this script or via env var."""
    import sys

    lib_name = {
        "win32": "PoseidonFormats.dll",
        "darwin": "PoseidonFormats.dylib",
    }.get(sys.platform, "PoseidonFormats.so")

    candidates = []
    env = os.environ.get("POSEIDON_FORMATS_DLL")
    if env:
        candidates.append(env)
    here = os.path.dirname(os.path.abspath(__file__))
    candidates.append(os.path.join(here, lib_name))
    candidates.append(os.path.join(here, "..", lib_name))
    # Standard user lib path on Linux
    if sys.platform != "win32":
        candidates.append(os.path.expanduser(f"~/.local/lib/{lib_name}"))
    for path in candidates:
        if os.path.isfile(path):
            return path
    raise FileNotFoundError(
        f"{lib_name} not found. Place it next to the addon, in ~/.local/lib/, "
        "or set POSEIDON_FORMATS_DLL environment variable."
    )


def _setup_dll(dll):
    """Declare argtypes/restype for all pf_* functions."""
    dll.pf_init.restype = c_int
    dll.pf_init.argtypes = []
    dll.pf_init_step.restype = c_int
    dll.pf_init_step.argtypes = [c_int]
    dll.pf_shutdown.restype = None
    dll.pf_shutdown.argtypes = []
    dll.pf_version.restype = c_char_p
    dll.pf_version.argtypes = []
    dll.pf_last_error.restype = c_char_p
    dll.pf_last_error.argtypes = []
    dll.pf_model_load.restype = c_void_p
    dll.pf_model_load.argtypes = [c_char_p]
    dll.pf_model_free.restype = None
    dll.pf_model_free.argtypes = [c_void_p]
    dll.pf_model_format.restype = c_char_p
    dll.pf_model_format.argtypes = [c_void_p]
    dll.pf_model_lod_count.restype = c_int
    dll.pf_model_lod_count.argtypes = [c_void_p]
    dll.pf_model_lod_resolution.restype = c_float
    dll.pf_model_lod_resolution.argtypes = [c_void_p, c_int]
    dll.pf_model_bounding_center.restype = None
    dll.pf_model_bounding_center.argtypes = [c_void_p, POINTER(c_float)]
    dll.pf_model_autocenter.restype = c_int
    dll.pf_model_autocenter.argtypes = [c_void_p]
    dll.pf_lod_vertex_count.restype = c_int
    dll.pf_lod_vertex_count.argtypes = [c_void_p, c_int]
    dll.pf_lod_face_count.restype = c_int
    dll.pf_lod_face_count.argtypes = [c_void_p, c_int]
    dll.pf_lod_get_vertices.restype = None
    dll.pf_lod_get_vertices.argtypes = [c_void_p, c_int, POINTER(c_float)]
    dll.pf_lod_get_normals.restype = None
    dll.pf_lod_get_normals.argtypes = [c_void_p, c_int, POINTER(c_float)]
    dll.pf_lod_get_uvs.restype = None
    dll.pf_lod_get_uvs.argtypes = [c_void_p, c_int, POINTER(c_float)]
    dll.pf_lod_get_faces.restype = None
    dll.pf_lod_get_faces.argtypes = [c_void_p, c_int, POINTER(c_int)]
    dll.pf_lod_get_face_materials.restype = None
    dll.pf_lod_get_face_materials.argtypes = [c_void_p, c_int, POINTER(c_int)]
    dll.pf_lod_get_vertex_flags.restype = None
    dll.pf_lod_get_vertex_flags.argtypes = [c_void_p, c_int, POINTER(c_uint32)]
    dll.pf_lod_get_face_flags.restype = None
    dll.pf_lod_get_face_flags.argtypes = [c_void_p, c_int, POINTER(c_uint32)]
    dll.pf_lod_material_count.restype = c_int
    dll.pf_lod_material_count.argtypes = [c_void_p, c_int]
    dll.pf_lod_material_texture.restype = c_char_p
    dll.pf_lod_material_texture.argtypes = [c_void_p, c_int, c_int]
    dll.pf_lod_material_surface.restype = c_char_p
    dll.pf_lod_material_surface.argtypes = [c_void_p, c_int, c_int]
    dll.pf_lod_selection_count.restype = c_int
    dll.pf_lod_selection_count.argtypes = [c_void_p, c_int]
    dll.pf_lod_selection_name.restype = c_char_p
    dll.pf_lod_selection_name.argtypes = [c_void_p, c_int, c_int]
    dll.pf_lod_selection_vertex_count.restype = c_int
    dll.pf_lod_selection_vertex_count.argtypes = [c_void_p, c_int, c_int]
    dll.pf_lod_selection_get_vertices.restype = None
    dll.pf_lod_selection_get_vertices.argtypes = [c_void_p, c_int, c_int, POINTER(c_int), POINTER(c_float)]
    dll.pf_lod_proxy_count.restype = c_int
    dll.pf_lod_proxy_count.argtypes = [c_void_p, c_int]
    dll.pf_lod_proxy_path.restype = c_char_p
    dll.pf_lod_proxy_path.argtypes = [c_void_p, c_int, c_int]
    dll.pf_lod_proxy_transform.restype = None
    dll.pf_lod_proxy_transform.argtypes = [c_void_p, c_int, c_int, POINTER(c_float)]
    dll.pf_lod_property_count.restype = c_int
    dll.pf_lod_property_count.argtypes = [c_void_p, c_int]
    dll.pf_lod_property_name.restype = c_char_p
    dll.pf_lod_property_name.argtypes = [c_void_p, c_int, c_int]
    dll.pf_lod_property_value.restype = c_char_p
    dll.pf_lod_property_value.argtypes = [c_void_p, c_int, c_int]
    dll.pf_image_load.restype = c_void_p
    dll.pf_image_load.argtypes = [c_char_p]
    dll.pf_image_free.restype = None
    dll.pf_image_free.argtypes = [c_void_p]
    dll.pf_image_width.restype = c_int
    dll.pf_image_width.argtypes = [c_void_p]
    dll.pf_image_height.restype = c_int
    dll.pf_image_height.argtypes = [c_void_p]
    dll.pf_image_format.restype = c_char_p
    dll.pf_image_format.argtypes = [c_void_p]
    dll.pf_image_get_rgba.restype = None
    dll.pf_image_get_rgba.argtypes = [c_void_p, POINTER(c_uint8)]
    dll.pf_pbo_open.restype = c_void_p
    dll.pf_pbo_open.argtypes = [c_char_p]
    dll.pf_pbo_close.restype = None
    dll.pf_pbo_close.argtypes = [c_void_p]
    dll.pf_pbo_entry_count.restype = c_int
    dll.pf_pbo_entry_count.argtypes = [c_void_p]
    dll.pf_pbo_entry_name.restype = c_char_p
    dll.pf_pbo_entry_name.argtypes = [c_void_p, c_int]
    dll.pf_pbo_entry_size.restype = c_int
    dll.pf_pbo_entry_size.argtypes = [c_void_p, c_int]
    dll.pf_pbo_extract.restype = c_int
    dll.pf_pbo_extract.argtypes = [c_void_p, c_int, POINTER(c_uint8), c_int]
    dll.pf_vfs_mount.restype = c_int
    dll.pf_vfs_mount.argtypes = [c_char_p]
    dll.pf_vfs_unmount.restype = None
    dll.pf_vfs_unmount.argtypes = []
    dll.pf_vfs_mounted.restype = c_int
    dll.pf_vfs_mounted.argtypes = []
    dll.pf_vfs_read.restype = c_int
    dll.pf_vfs_read.argtypes = [c_char_p, POINTER(c_uint8), c_int]
    dll.pf_vfs_image_load.restype = c_void_p
    dll.pf_vfs_image_load.argtypes = [c_char_p]
    dll.pf_vfs_find.restype = c_void_p
    dll.pf_vfs_find.argtypes = [c_char_p]
    dll.pf_vfs_find_count.restype = c_int
    dll.pf_vfs_find_count.argtypes = [c_void_p]
    dll.pf_vfs_find_path.restype = c_char_p
    dll.pf_vfs_find_path.argtypes = [c_void_p, c_int]
    dll.pf_vfs_find_free.restype = None
    dll.pf_vfs_find_free.argtypes = [c_void_p]
    dll.pf_rtm_load.restype = c_void_p
    dll.pf_rtm_load.argtypes = [c_char_p]
    dll.pf_rtm_load_from_memory.restype = c_void_p
    dll.pf_rtm_load_from_memory.argtypes = [POINTER(c_uint8), c_int]
    dll.pf_rtm_free.restype = None
    dll.pf_rtm_free.argtypes = [c_void_p]
    dll.pf_rtm_phase_count.restype = c_int
    dll.pf_rtm_phase_count.argtypes = [c_void_p]
    dll.pf_rtm_bone_count.restype = c_int
    dll.pf_rtm_bone_count.argtypes = [c_void_p]
    dll.pf_rtm_step.restype = None
    dll.pf_rtm_step.argtypes = [c_void_p, POINTER(c_float)]
    dll.pf_rtm_bone_name.restype = c_char_p
    dll.pf_rtm_bone_name.argtypes = [c_void_p, c_int]
    dll.pf_rtm_phase_time.restype = c_float
    dll.pf_rtm_phase_time.argtypes = [c_void_p, c_int]
    dll.pf_rtm_bone_matrix.restype = None
    dll.pf_rtm_bone_matrix.argtypes = [c_void_p, c_int, c_int, POINTER(c_float)]


def load():
    """Load and initialize the DLL. Safe to call multiple times."""
    global _dll
    if _dll is not None:
        return _dll
    path = _find_dll()
    dll_dir = os.path.dirname(os.path.abspath(path))
    print(f"[P3D] Loading DLL: {path}")

    # Python 3.8+ needs explicit DLL search directory for dependencies
    if hasattr(os, "add_dll_directory"):
        os.add_dll_directory(dll_dir)

    try:
        _dll = ctypes.CDLL(path)
    except OSError as e:
        # On Linux, the .so uses static TLS from vcpkg dependencies, which
        # prevents dlopen at runtime.  If LD_PRELOAD already loaded it, grab
        # the existing handle with RTLD_NOLOAD.
        import sys

        if sys.platform.startswith("linux"):
            RTLD_NOLOAD = 0x00004  # glibc constant
            for noload_path in [os.path.basename(path), path]:
                try:
                    _dll = ctypes.CDLL(noload_path, mode=RTLD_NOLOAD)
                    print("[P3D] Using LD_PRELOAD handle (RTLD_NOLOAD)")
                    break
                except OSError:
                    continue
            if _dll is None:
                raise RuntimeError(
                    f"Failed to load DLL: {path}\n{e}\n"
                    "On Linux, launch Blender via: blender-cwr  "
                    "(or LD_PRELOAD=~/.local/lib/PoseidonFormats.so blender)"
                ) from e
        else:
            raise RuntimeError(f"Failed to load DLL: {path}\n{e}") from e

    _setup_dll(_dll)
    print("[P3D] DLL loaded, checking safe appframe...")

    try:
        safe = _dll.pf_has_safe_appframe()
        print(f"[P3D] Safe appframe: {safe}")
    except OSError as e:
        print(f"[P3D] pf_has_safe_appframe crashed: {e}")
    steps = [
        (1, "Set safe AppFrame"),
        (2, "SetMemorySystemReady"),
        (3, "InitLibraryElement"),
        (4, "CreateEngineDummy"),
    ]
    for step_num, step_name in steps:
        print(f"[P3D] Step {step_num}: {step_name}...")
        try:
            r = _dll.pf_init_step(step_num)
            print(f"[P3D] Step {step_num}: OK (returned {r})")
        except OSError as e:
            _dll = None
            raise RuntimeError(f"pf_init crashed at step {step_num} ({step_name}): {e}\nDLL path: {path}") from e
    print(f"[P3D] Initialized OK (version: {_dll.pf_version().decode()})")
    return _dll


def unload():
    """Shutdown the DLL."""
    global _dll
    if _dll is not None:
        _dll.pf_shutdown()
        _dll = None


def vfs_mount(game_dir):
    """Mount game directory VFS (loads all PBOs). Returns PBO count."""
    dll = get()
    if isinstance(game_dir, str):
        game_dir = game_dir.encode("utf-8")
    count = dll.pf_vfs_mount(game_dir)
    if count > 0:
        print(f"[P3D] VFS mounted: {count} PBOs from {game_dir.decode()}")
    return count


def vfs_unmount():
    """Unmount VFS."""
    dll = get()
    dll.pf_vfs_unmount()


def vfs_mounted():
    """Check if VFS is mounted."""
    dll = get()
    return dll.pf_vfs_mounted() == 1


def vfs_find(extension=".p3d"):
    """List all files in VFS matching extension. Returns sorted list of paths."""
    dll = get()
    if isinstance(extension, str):
        extension = extension.encode("utf-8")
    handle = dll.pf_vfs_find(extension)
    if not handle:
        return []
    count = dll.pf_vfs_find_count(handle)
    paths = []
    for i in range(count):
        p = dll.pf_vfs_find_path(handle, i)
        if p:
            paths.append(p.decode("utf-8", errors="replace"))
    dll.pf_vfs_find_free(handle)
    return paths


class VFSImage:
    """Load a PAA/PAC texture from VFS by virtual path (e.g. \\heli\\uh60\\body.paa)."""

    def __init__(self, vfs_path):
        dll = get()
        if isinstance(vfs_path, str):
            vfs_path = vfs_path.encode("utf-8")
        self._handle = dll.pf_vfs_image_load(vfs_path)
        if not self._handle:
            err = dll.pf_last_error()
            msg = err.decode("utf-8", errors="replace") if err else "unknown"
            raise RuntimeError(f"Failed to load VFS image: {msg}")
        self._dll = dll

    def __del__(self):
        self.close()

    def close(self):
        if self._handle and self._dll:
            self._dll.pf_image_free(self._handle)
            self._handle = None

    @property
    def width(self):
        return self._dll.pf_image_width(self._handle)

    @property
    def height(self):
        return self._dll.pf_image_height(self._handle)

    def get_rgba(self):
        size = self.width * self.height * 4
        buf = (c_uint8 * size)()
        self._dll.pf_image_get_rgba(self._handle, buf)
        return bytes(buf)


def get():
    """Get the loaded DLL handle, loading if necessary."""
    if _dll is None:
        load()
    return _dll


class P3DModel:
    """RAII wrapper around a loaded P3D model."""

    def __init__(self, path):
        dll = get()
        if isinstance(path, str):
            path = path.encode("utf-8")
        self._handle = dll.pf_model_load(path)
        if not self._handle:
            err = dll.pf_last_error()
            msg = err.decode("utf-8", errors="replace") if err else "unknown"
            raise RuntimeError(f"Failed to load model: {msg}")
        self._dll = dll

    def __del__(self):
        self.close()

    def close(self):
        if self._handle and self._dll:
            self._dll.pf_model_free(self._handle)
            self._handle = None

    @property
    def format(self):
        return self._dll.pf_model_format(self._handle).decode()

    @property
    def lod_count(self):
        return self._dll.pf_model_lod_count(self._handle)

    def lod_resolution(self, lod):
        return self._dll.pf_model_lod_resolution(self._handle, lod)

    def bounding_center(self):
        """Returns (x, y, z) bounding center of the model."""
        buf = (c_float * 3)()
        self._dll.pf_model_bounding_center(self._handle, buf)
        return (buf[0], buf[1], buf[2])

    @property
    def autocenter(self):
        """Returns True if model has autocenter enabled."""
        return bool(self._dll.pf_model_autocenter(self._handle))

    def vertex_count(self, lod):
        return self._dll.pf_lod_vertex_count(self._handle, lod)

    def face_count(self, lod):
        return self._dll.pf_lod_face_count(self._handle, lod)

    def get_vertices(self, lod):
        """Returns list of (x, y, z) tuples."""
        n = self.vertex_count(lod)
        buf = (c_float * (n * 3))()
        self._dll.pf_lod_get_vertices(self._handle, lod, buf)
        return [(buf[i * 3], buf[i * 3 + 1], buf[i * 3 + 2]) for i in range(n)]

    def get_normals(self, lod):
        """Returns list of (nx, ny, nz) tuples."""
        n = self.vertex_count(lod)
        buf = (c_float * (n * 3))()
        self._dll.pf_lod_get_normals(self._handle, lod, buf)
        return [(buf[i * 3], buf[i * 3 + 1], buf[i * 3 + 2]) for i in range(n)]

    def get_uvs(self, lod):
        """Returns list of (u, v) tuples."""
        n = self.vertex_count(lod)
        buf = (c_float * (n * 2))()
        self._dll.pf_lod_get_uvs(self._handle, lod, buf)
        return [(buf[i * 2], buf[i * 2 + 1]) for i in range(n)]

    def get_faces(self, lod):
        """Returns list of (v0, v1, v2) index tuples."""
        n = self.face_count(lod)
        buf = (c_int * (n * 3))()
        self._dll.pf_lod_get_faces(self._handle, lod, buf)
        return [(buf[i * 3], buf[i * 3 + 1], buf[i * 3 + 2]) for i in range(n)]

    def get_face_materials(self, lod):
        """Returns list of material indices, one per face."""
        n = self.face_count(lod)
        buf = (c_int * n)()
        self._dll.pf_lod_get_face_materials(self._handle, lod, buf)
        return list(buf)

    def get_vertex_flags(self, lod):
        """Returns list of uint32 vertex flags (ClipFlags), one per vertex."""
        n = self.vertex_count(lod)
        if n == 0:
            return []
        buf = (c_uint32 * n)()
        self._dll.pf_lod_get_vertex_flags(self._handle, lod, buf)
        return list(buf)

    def get_face_flags(self, lod):
        """Returns list of uint32 face flags (FaceFlags), one per face."""
        n = self.face_count(lod)
        if n == 0:
            return []
        buf = (c_uint32 * n)()
        self._dll.pf_lod_get_face_flags(self._handle, lod, buf)
        return list(buf)

    def material_count(self, lod):
        return self._dll.pf_lod_material_count(self._handle, lod)

    def material_texture(self, lod, mat):
        r = self._dll.pf_lod_material_texture(self._handle, lod, mat)
        return r.decode() if r else ""

    def material_surface(self, lod, mat):
        r = self._dll.pf_lod_material_surface(self._handle, lod, mat)
        return r.decode() if r else ""

    def selection_count(self, lod):
        return self._dll.pf_lod_selection_count(self._handle, lod)

    def selection_name(self, lod, sel):
        r = self._dll.pf_lod_selection_name(self._handle, lod, sel)
        return r.decode() if r else ""

    def selection_vertex_count(self, lod, sel):
        return self._dll.pf_lod_selection_vertex_count(self._handle, lod, sel)

    def get_selection_vertices(self, lod, sel):
        """Returns (indices_list, weights_list)."""
        n = self.selection_vertex_count(lod, sel)
        if n == 0:
            return [], []
        idx_buf = (c_int * n)()
        wgt_buf = (c_float * n)()
        self._dll.pf_lod_selection_get_vertices(self._handle, lod, sel, idx_buf, wgt_buf)
        return list(idx_buf), list(wgt_buf)

    def proxy_count(self, lod):
        return self._dll.pf_lod_proxy_count(self._handle, lod)

    def proxy_path(self, lod, prx):
        r = self._dll.pf_lod_proxy_path(self._handle, lod, prx)
        return r.decode() if r else ""

    def proxy_transform(self, lod, prx):
        """Returns 16 floats (4x4 column-major matrix)."""
        buf = (c_float * 16)()
        self._dll.pf_lod_proxy_transform(self._handle, lod, prx, buf)
        return list(buf)

    def property_count(self, lod):
        return self._dll.pf_lod_property_count(self._handle, lod)

    def property_name(self, lod, prop):
        r = self._dll.pf_lod_property_name(self._handle, lod, prop)
        return r.decode() if r else ""

    def property_value(self, lod, prop):
        r = self._dll.pf_lod_property_value(self._handle, lod, prop)
        return r.decode() if r else ""


class PAAImage:
    """RAII wrapper around a loaded PAA/PAC image."""

    def __init__(self, path):
        dll = get()
        if isinstance(path, str):
            path = path.encode("utf-8")
        self._handle = dll.pf_image_load(path)
        if not self._handle:
            err = dll.pf_last_error()
            msg = err.decode("utf-8", errors="replace") if err else "unknown"
            raise RuntimeError(f"Failed to load image: {msg}")
        self._dll = dll

    def __del__(self):
        self.close()

    def close(self):
        if self._handle and self._dll:
            self._dll.pf_image_free(self._handle)
            self._handle = None

    @property
    def width(self):
        return self._dll.pf_image_width(self._handle)

    @property
    def height(self):
        return self._dll.pf_image_height(self._handle)

    @property
    def format(self):
        return self._dll.pf_image_format(self._handle).decode()

    def get_rgba(self):
        """Returns bytes (W*H*4 RGBA pixels)."""
        size = self.width * self.height * 4
        buf = (c_uint8 * size)()
        self._dll.pf_image_get_rgba(self._handle, buf)
        return bytes(buf)


class RTMAnimation:
    """Wrapper for RTM animation loaded via PoseidonFormats DLL."""

    def __init__(self, path=None, data=None):
        self._dll = load()
        self._handle = None
        if path:
            p = path.encode() if isinstance(path, str) else path
            self._handle = self._dll.pf_rtm_load(p)
        elif data is not None:
            buf = (c_uint8 * len(data))(*data)
            self._handle = self._dll.pf_rtm_load_from_memory(buf, len(data))
        if not self._handle:
            err = self._dll.pf_last_error().decode()
            raise RuntimeError(f"Failed to load RTM: {err}")

    def __del__(self):
        if getattr(self, "_handle", None) and getattr(self, "_dll", None):
            self._dll.pf_rtm_free(self._handle)
            self._handle = None

    def close(self):
        if self._handle:
            self._dll.pf_rtm_free(self._handle)
            self._handle = None

    @property
    def phase_count(self):
        return self._dll.pf_rtm_phase_count(self._handle)

    @property
    def bone_count(self):
        return self._dll.pf_rtm_bone_count(self._handle)

    def step(self):
        """Returns (stepX, stepY, stepZ) movement per animation cycle."""
        buf = (c_float * 3)()
        self._dll.pf_rtm_step(self._handle, buf)
        return (buf[0], buf[1], buf[2])

    def bone_name(self, bone):
        return self._dll.pf_rtm_bone_name(self._handle, bone).decode()

    def bone_names(self):
        return [self.bone_name(i) for i in range(self.bone_count)]

    def phase_time(self, phase):
        return self._dll.pf_rtm_phase_time(self._handle, phase)

    def bone_matrix(self, phase, bone):
        """Returns 4x4 matrix as 16-element list (column-major)."""
        buf = (c_float * 16)()
        self._dll.pf_rtm_bone_matrix(self._handle, phase, bone, buf)
        return list(buf)
