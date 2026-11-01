# BC7 (mode 6) encoder and DDS header, from Texto 3D (tools/make-art.py).
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

WEIGHTS = np.array([0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64], dtype=np.int32)


def encode_bc7(rgb):
    """rgb: HxWx3 uint8 (H, W multiples of 4) -> bytes, 16 per 4x4 block, rows of blocks."""
    h, w, _ = rgb.shape
    blocks = rgb.reshape(h // 4, 4, w // 4, 4, 3).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 3).astype(np.int32)
    lo_c = blocks.min(axis=1)
    hi_c = blocks.max(axis=1)
    # 7-bit endpoints with p-bit 1 (alpha 255 needs it): value = (q << 1) | 1
    q0 = np.clip((lo_c - 1 + 1) // 2, 0, 127)
    q1 = np.clip((hi_c - 1 + 1) // 2, 0, 127)
    e0 = (q0 << 1) | 1
    e1 = (q1 << 1) | 1
    # every pixel's best of the 16 interpolated colors
    pal = ((64 - WEIGHTS)[None, :, None] * e0[:, None, :] + WEIGHTS[None, :, None] * e1[:, None, :] + 32) >> 6
    dist = ((blocks[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(axis=3)
    idx = dist.argmin(axis=2).astype(np.int64)  # blocks x 16
    # the anchor (pixel 0) index must be < 8: otherwise swap the endpoints and mirror the indices
    flip = idx[:, 0] >= 8
    q0f = np.where(flip[:, None], q1, q0)
    q1f = np.where(flip[:, None], q0, q1)
    idx = np.where(flip[:, None], 15 - idx, idx)
    q0, q1 = q0f.astype(np.uint64), q1f.astype(np.uint64)

    lo = np.full(len(blocks), 1 << 6, dtype=np.uint64)  # mode 6
    shift = 7
    for ch in range(3):
        lo |= q0[:, ch] << np.uint64(shift)
        lo |= q1[:, ch] << np.uint64(shift + 7)
        shift += 14
    lo |= np.uint64(127) << np.uint64(shift)  # A0
    lo |= np.uint64(127) << np.uint64(shift + 7)  # A1
    lo |= np.uint64(1) << np.uint64(63)  # P0
    hi = np.ones(len(blocks), dtype=np.uint64)  # P1
    hi |= idx[:, 0].astype(np.uint64) << np.uint64(1)
    for k in range(1, 16):
        hi |= idx[:, k].astype(np.uint64) << np.uint64(4 + (k - 1) * 4)
    out = np.empty((len(blocks), 2), dtype='<u8')
    out[:, 0], out[:, 1] = lo, hi
    return out.tobytes()


def decode_bc7_mode6(data, w, h):
    """Decodes what encode_bc7 writes (mode 6 only), for the check."""
    words = np.frombuffer(data, dtype='<u8').reshape(-1, 2)
    lo, hi = words[:, 0], words[:, 1]
    assert np.all((lo & np.uint64(0x7F)) == np.uint64(1 << 6)), 'not mode 6'

    def bits(v, pos, n):
        return ((v >> np.uint64(pos)) & np.uint64((1 << n) - 1)).astype(np.int32)

    p0, p1 = bits(lo, 63, 1), bits(hi, 0, 1)
    e0 = np.stack([(bits(lo, 7 + 14 * c, 7) << 1) | p0 for c in range(3)], axis=1)
    e1 = np.stack([(bits(lo, 14 + 14 * c, 7) << 1) | p1 for c in range(3)], axis=1)
    idx = np.stack([bits(hi, 1, 3)] + [bits(hi, 4 + (k - 1) * 4, 4) for k in range(1, 16)], axis=1)
    wgt = WEIGHTS[idx]
    px = ((64 - wgt)[:, :, None] * e0[:, None, :] + wgt[:, :, None] * e1[:, None, :] + 32) >> 6
    return px.reshape(h // 4, w // 4, 4, 4, 3).transpose(0, 2, 1, 3, 4).reshape(h, w, 3).astype(np.uint8)


def dds_header(w, h, data_len):
    """The 148-byte header NES FCEUmm PS5's console-tested pic0.dds carries (DDS + DX10, BC7_UNORM)."""
    header = struct.pack('<4sIIIIIII', b'DDS ', 124, 0xA1007, h, w, data_len, 1, 1) + b'\0' * 44
    header += struct.pack('<II4sIIIII', 32, 0x4, b'DX10', 0, 0, 0, 0, 0)
    header += struct.pack('<IIIII', 0x1000, 0, 0, 0, 0)
    header += struct.pack('<IIIII', 98, 3, 0, 1, 1)
    assert len(header) == 148
    return header


