"""Pack each display strip independently so decoding needs only a small buffer."""
from pathlib import Path
from struct import pack
from zlib import compress, decompress

root = Path(__file__).resolve().parents[1]
art = root / 'Card/STILL/UI'
dest = root / 'Still/SceneAssets.h'
data = bytearray()
offsets = []
entries = []
for file in sorted(art.iterdir()):
    if file.suffix == '.frames':
        w, h, count = 320, 104, 24
    elif file.name.endswith('-rails.raw'):
        w, h, count = 144, 14, 4 if file.name.startswith('mixer') else 1
    elif file.suffix == '.raw':
        w, h, count = (76, 34, 1) if '-' in file.stem else (320, 240, 1)
    else:
        continue
    raw = file.read_bytes()
    assert len(raw) == w*h*count*2, file
    first = len(offsets)
    for frame in range(count):
        for row in range(0, h, 8):
            start = (frame*h+row)*w*2
            strip = raw[start:start+min(h-row,8)*w*2]
            compressed = compress(strip, 9)
            assert decompress(compressed) == strip
            offsets.append(len(data))
            data.extend(compressed)
    entries.append((file.name, w, h, count, first))
offsets.append(len(data))

with dest.open('w') as f:
    f.write('#pragma once\n#include <Arduino.h>\n\n')
    f.write('struct SceneAsset { const char *name; uint16_t width, height, frames; uint32_t first; };\n')
    f.write('const SceneAsset sceneAssets[] = {\n')
    for name, w, h, count, first in entries:
        f.write(f'  {{"{name}", {w}, {h}, {count}, {first}}},\n')
    f.write('};\nconst uint32_t sceneStripOffsets[] PROGMEM = {\n')
    for i in range(0,len(offsets),12):
        f.write('  '+','.join(map(str,offsets[i:i+12]))+',\n')
    f.write('};\nconst uint8_t scenePixels[] PROGMEM = {\n')
    for i in range(0,len(data),24):
        f.write('  '+','.join(map(str,data[i:i+24]))+',\n')
    f.write('};\n')
print(f'{len(entries)} assets, {len(offsets)-1} compressed strips, {len(data):,} bytes')
