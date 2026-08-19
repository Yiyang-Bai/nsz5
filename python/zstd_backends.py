"""Alternate zstd versions for byte-exact VBZ blob regeneration.

The VBZ inner stream (svb16 of zigzag deltas) is fully deterministic, but
the outer zstd frame differs between zstd library versions. POD5 files
written by different MinKNOW releases therefore need the matching zstd
version to regenerate their blobs bit-for-bit.

Each backend is a specific libzstd shared library, discovered under
work/zstd_env_<version>/lib/libzstd.so.1 (installed via conda-forge).
Compression always uses the simple one-shot API at level 1, which is what
the VBZ codec uses.
"""
import ctypes
from pathlib import Path

_ROOT = Path(__file__).resolve().parent.parent

# candidate versions in preference order (newer first); a container only
# depends on the versions actually recorded in it
CANDIDATE_VERSIONS = ["1.5.6", "1.5.5", "1.5.2", "1.5.0", "1.4.9"]

_handles = {}


def _load(version: str):
    if version in _handles:
        return _handles[version]
    path = _ROOT / "work" / f"zstd_env_{version}" / "lib" / "libzstd.so.1"
    if not path.exists():
        raise FileNotFoundError(
            f"libzstd {version} not found at {path}; install with "
            f"`mamba create -p work/zstd_env_{version} -c conda-forge "
            f"--no-deps zstd={version}`"
        )
    lib = ctypes.CDLL(str(path))
    lib.ZSTD_compressBound.restype = ctypes.c_size_t
    lib.ZSTD_compressBound.argtypes = [ctypes.c_size_t]
    lib.ZSTD_compress.restype = ctypes.c_size_t
    lib.ZSTD_compress.argtypes = [
        ctypes.c_char_p, ctypes.c_size_t,
        ctypes.c_char_p, ctypes.c_size_t, ctypes.c_int,
    ]
    lib.ZSTD_isError.restype = ctypes.c_uint
    lib.ZSTD_isError.argtypes = [ctypes.c_size_t]
    _handles[version] = lib
    return lib


def available_versions():
    out = []
    for v in CANDIDATE_VERSIONS:
        try:
            _load(v)
            out.append(v)
        except FileNotFoundError:
            pass
    return out


def compress_level1(version: str, data: bytes) -> bytes:
    lib = _load(version)
    bound = lib.ZSTD_compressBound(len(data))
    dst = ctypes.create_string_buffer(bound)
    n = lib.ZSTD_compress(dst, bound, data, len(data), 1)
    if lib.ZSTD_isError(n):
        raise RuntimeError(f"zstd {version} compression failed")
    return dst.raw[:n]
