"""Compose the fp L Screen Flip menu from a verified, external MAIN image."""
import hashlib
import json
from pathlib import Path

from ui import fpui

RECIPE = Path(__file__).with_name('menu_recipe.json')
MAIN_SHA = 'fb8c20968e01ff9d8edf5f3e3f681c7341958048a004e717092c75e11106d4b9'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def image(path):
    data = Path(path).read_bytes()
    if len(data) != 49310208 or digest(data) != MAIN_SHA:
        raise ValueError('expected the external, declared-length fp L 3.02 MAIN image')
    return data


def build(data):
    row = json.loads(RECIPE.read_text())
    start, size = row['donor_offset'], row['donor_length']
    donor = data[start:start+size]
    if digest(donor) != row['donor_sha256']:
        raise ValueError('wrong stock menu donor')
    block = fpui.Block()
    for text, offset in row['strings']:
        block.string(text, offset)
    for op, args in row['ops']:
        block.op(op, *args)
    frag = bytearray(row['fragment_length'])
    at = row['donor_fragment_offset']
    frag[at:at+size] = donor
    for at, value in row['edits']:
        value = bytes.fromhex(value)
        if not 0 <= at <= len(frag)-len(value):
            raise ValueError('menu edit outside fragment')
        frag[at:at+len(value)] = value
    block.fragment(frag)
    encoded = block.encode()
    if digest(encoded) != row['block_sha256']:
        raise ValueError('menu no longer matches the camera-tested row')
    return encoded, {'page': row['page'], 'block_sha256': digest(encoded)}
