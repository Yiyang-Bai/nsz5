"""ctypes bindings for the nsz codec C library (work/libnsz.so)."""
import ctypes
import os
from pathlib import Path

import numpy as np

_LIB_PATH = Path(__file__).resolve().parent.parent / "work" / "libnsz.so"
_lib = ctypes.CDLL(str(_LIB_PATH))

_lib.nsz_compress.argtypes = [
    ctypes.POINTER(ctypes.c_int16),
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
    ctypes.POINTER(ctypes.c_size_t),
]
_lib.nsz_compress.restype = ctypes.c_int

_lib.nsz_decompress.argtypes = [
    ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_int16),
    ctypes.c_size_t,
]
_lib.nsz_decompress.restype = ctypes.c_int

_lib.nsz_peek_sample_count.argtypes = [
    ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_uint32),
]
_lib.nsz_peek_sample_count.restype = ctypes.c_int

_lib.nsz_load_prior.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]
_lib.nsz_load_prior.restype = ctypes.c_int
_lib.nsz_clear_prior.argtypes = []
_lib.nsz_clear_prior.restype = None
_lib.nsz_prior_version.argtypes = []
_lib.nsz_prior_version.restype = ctypes.c_uint16

_lib.nsz_free.argtypes = [ctypes.c_void_p]
_lib.nsz_free.restype = None

_lib.nsz_compress_batch.argtypes = [
    ctypes.POINTER(ctypes.c_int16),
    ctypes.POINTER(ctypes.c_uint64),
    ctypes.c_size_t,
    ctypes.c_int,
    ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
    ctypes.POINTER(ctypes.c_size_t),
]
_lib.nsz_compress_batch.restype = ctypes.c_int

_lib.nsz_decompress_batch.argtypes = [
    ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
    ctypes.POINTER(ctypes.c_size_t),
    ctypes.c_size_t,
    ctypes.c_int,
    ctypes.POINTER(ctypes.c_int16),
    ctypes.POINTER(ctypes.c_uint64),
]
_lib.nsz_decompress_batch.restype = ctypes.c_int


class NszError(RuntimeError):
    pass


def compress(samples: np.ndarray) -> bytes:
    """Compress an int16 array into one nsz chunk."""
    samples = np.ascontiguousarray(samples, dtype=np.int16)
    out_ptr = ctypes.POINTER(ctypes.c_uint8)()
    out_size = ctypes.c_size_t()
    rc = _lib.nsz_compress(
        samples.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)),
        samples.size,
        ctypes.byref(out_ptr),
        ctypes.byref(out_size),
    )
    if rc != 0:
        raise NszError(f"nsz_compress failed (rc={rc})")
    try:
        return ctypes.string_at(out_ptr, out_size.value)
    finally:
        _lib.nsz_free(out_ptr)


def decompress(blob: bytes, n: int) -> np.ndarray:
    """Decompress one nsz chunk into an int16 array of length n."""
    out = np.empty(n, dtype=np.int16)
    buf = (ctypes.c_uint8 * len(blob)).from_buffer_copy(blob)
    rc = _lib.nsz_decompress(
        buf, len(blob), out.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)), n
    )
    if rc != 0:
        raise NszError(f"nsz_decompress failed (rc={rc})")
    return out


def peek_sample_count(blob: bytes) -> int:
    buf = (ctypes.c_uint8 * len(blob)).from_buffer_copy(blob[:64])
    count = ctypes.c_uint32()
    rc = _lib.nsz_peek_sample_count(buf, min(len(blob), 64), ctypes.byref(count))
    if rc != 0:
        raise NszError(f"nsz_peek_sample_count failed (rc={rc})")
    return count.value


def compress_batch(samples: np.ndarray, offsets: np.ndarray,
                   threads: int) -> list:
    """Compress many chunks in one C call (OpenMP-parallel, internally
    verified). Chunk i is samples[offsets[i]:offsets[i+1]]."""
    samples = np.ascontiguousarray(samples, dtype=np.int16)
    offsets = np.ascontiguousarray(offsets, dtype=np.uint64)
    n = len(offsets) - 1
    outs = (ctypes.POINTER(ctypes.c_uint8) * n)()
    sizes = (ctypes.c_size_t * n)()
    rc = _lib.nsz_compress_batch(
        samples.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)),
        offsets.ctypes.data_as(ctypes.POINTER(ctypes.c_uint64)),
        n, threads, outs, sizes,
    )
    if rc != 0:
        raise NszError(f"nsz_compress_batch failed (rc={rc})")
    try:
        return [ctypes.string_at(outs[i], sizes[i]) for i in range(n)]
    finally:
        for i in range(n):
            _lib.nsz_free(outs[i])


def decompress_batch(blobs: list, counts: list, threads: int) -> np.ndarray:
    """Decompress many chunks in one C call. Returns concatenated samples;
    chunk i occupies the slice [sum(counts[:i]) : sum(counts[:i+1]))."""
    n = len(blobs)
    offsets = np.zeros(n + 1, dtype=np.uint64)
    np.cumsum(counts, out=offsets[1:])
    total = int(offsets[-1])
    out = np.empty(total, dtype=np.int16)
    bufs = [(ctypes.c_uint8 * len(b)).from_buffer_copy(b) for b in blobs]
    ins = (ctypes.POINTER(ctypes.c_uint8) * n)(
        *[ctypes.cast(b, ctypes.POINTER(ctypes.c_uint8)) for b in bufs])
    sizes = (ctypes.c_size_t * n)(*[len(b) for b in blobs])
    rc = _lib.nsz_decompress_batch(
        ins, sizes, n, threads,
        out.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)),
        offsets.ctypes.data_as(ctypes.POINTER(ctypes.c_uint64)),
    )
    if rc != 0:
        raise NszError(f"nsz_decompress_batch failed (rc={rc})")
    return out


def load_prior(data: bytes) -> None:
    buf = (ctypes.c_uint8 * len(data)).from_buffer_copy(data)
    rc = _lib.nsz_load_prior(buf, len(data))
    if rc != 0:
        raise NszError(f"nsz_load_prior failed (rc={rc})")


def clear_prior() -> None:
    _lib.nsz_clear_prior()


def prior_version() -> int:
    return _lib.nsz_prior_version()
