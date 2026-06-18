import struct, sys

with open('render/font/embedded.cpp', 'r') as f:
    content = f.read()
import re
m = re.search(r'DEFAULT_FONT_DATA\[\] = \{(.*?)\};', content, re.DOTALL)
data_str = m.group(1)
bytes_list = [int(b.strip(), 16) for b in data_str.replace('\n', '').split(',') if b.strip()]
data = bytes(bytes_list)

num_tables = struct.unpack('>H', data[4:6])[0]
tables = {}
for i in range(num_tables):
    off = 12 + i * 16
    tag = bytes(data[off:off+4]).decode('ascii')
    tab_off = struct.unpack('>I', data[off+8:off+12])[0]
    tab_len = struct.unpack('>I', data[off+12:off+16])[0]
    tables[tag] = (tab_off, tab_len)

loca = tables['loca']
loca_data = data[loca[0]:loca[0]+loca[1]]
head = data[tables['head'][0]:tables['head'][0]+tables['head'][1]]
long_loca = struct.unpack('>h', head[50:52])[0] == 1

glyf_base = tables['glyf'][0]
gid = 34
if long_loca:
    goff = struct.unpack('>I', loca_data[gid*4:gid*4+4])[0]
    noff = struct.unpack('>I', loca_data[(gid+1)*4:(gid+1)*4+4])[0]
else:
    goff = struct.unpack('>H', loca_data[gid*2:gid*2+2])[0] * 2
    noff = struct.unpack('>H', loca_data[(gid+1)*2:(gid+1)*2+2])[0] * 2

gdata = data[glyf_base+goff:glyf_base+noff]
print(f'Glyph size: {len(gdata)} bytes')
num_contours = struct.unpack('>h', gdata[0:2])[0]
xmin = struct.unpack('>h', gdata[2:4])[0]
ymin = struct.unpack('>h', gdata[4:6])[0]
xmax = struct.unpack('>h', gdata[6:8])[0]
ymax = struct.unpack('>h', gdata[8:10])[0]
print(f'contours={num_contours} bbox=({xmin},{ymin})-({xmax},{ymax})')

if num_contours <= 0:
    print("Composite or empty glyph")
    sys.exit(0)

contour_ends = []
for i in range(num_contours):
    contour_ends.append(struct.unpack('>H', gdata[10+i*2:10+i*2+2])[0])

instr_len = struct.unpack('>H', gdata[10+num_contours*2:10+num_contours*2+2])[0]
flags_off = 10 + num_contours * 2 + 2 + instr_len

num_points = contour_ends[-1] + 1
flags = []
fpos = flags_off
i = 0
while i < num_points and fpos < len(gdata):
    f = gdata[fpos]
    fpos += 1
    flags.append(f)
    if f & 0x08:
        repeat = gdata[fpos]
        fpos += 1
        for _ in range(repeat):
            if i + 1 + len(flags) - 1 < num_points:
                flags.append(f)
        i += repeat
    i += 1

xs = []
prev = 0
for f in flags:
    if f & 0x02:
        val = gdata[fpos]
        fpos += 1
        prev += val if (f & 0x10) else -val
    elif not (f & 0x10):
        prev += struct.unpack('>h', gdata[fpos:fpos+2])[0]
        fpos += 2
    xs.append(prev)

ys = []
prev = 0
for f in flags:
    if f & 0x04:
        val = gdata[fpos]
        fpos += 1
        prev += val if (f & 0x20) else -val
    elif not (f & 0x20):
        prev += struct.unpack('>h', gdata[fpos:fpos+2])[0]
        fpos += 2
    ys.append(prev)

print(f'Points: {len(xs)} decoded from {fpos} bytes')
pt = 0
for c in range(num_contours):
    end = contour_ends[c]
    start = 0 if c == 0 else contour_ends[c-1] + 1
    print(f'Contour {c} [{start},{end}]:')
    for i in range(start, end+1):
        on = 'on' if (flags[i] & 1) else '  '
        print(f'  pt{i}: ({xs[i]:5d},{ys[i]:5d}) {on}')
    pt = end + 1

# Write a PPM of the outline at 100x scale
scale = 100
w = (xmax - xmin) * scale // 1000 * 100 + 100
h = (ymax - ymin) * scale // 1000 * 100 + 100
# Actually just use a fixed size
W = 400
H = 800
pixels = [0] * (W * H)
for i in range(len(xs)):
    sx = int(xs[i] * scale // 1000)
    sy = int(ys[i] * scale // 1000)
    # Simple: just draw dots at each point
    if 0 <= sx < W and 0 <= sy < H:
        pixels[sy * W + sx] = 255

# Draw lines between consecutive points in each contour
pt = 0
for c in range(num_contours):
    end = contour_ends[c]
    count = end - (0 if c==0 else contour_ends[c-1]) + 1
    for i in range(count):
        i1 = pt + i
        i2 = pt + (i + 1) % count
        # Bresenham line
        x1, y1 = int(xs[i1] * scale // 1000), int(ys[i1] * scale // 1000)
        x2, y2 = int(xs[i2] * scale // 1000), int(ys[i2] * scale // 1000)
        dx, dy = abs(x2-x1), abs(y2-y1)
        sx = 1 if x2 > x1 else -1
        sy = 1 if y2 > y1 else -1
        err = dx - dy
        while True:
            if 0 <= x1 < W and 0 <= y1 < H:
                pixels[y1 * W + x1] = 200
            if x1 == x2 and y1 == y2: break
            e2 = err * 2
            if e2 > -dy: err -= dy; x1 += sx
            if e2 < dx: err += dx; y1 += sy
    pt += count

# Write PPM
with open('C:/Users/ericl/AppData/Local/Temp/opencode/glyph_A.ppm', 'w') as f:
    f.write(f'P2\n{W} {H}\n255\n')
    for y in reversed(range(H)):
        row = pixels[y*W:(y+1)*W]
        f.write(' '.join(str(p) for p in row) + '\n')

print(f'\nWrote glyph_A.ppm ({W}x{H})')
print(f'Outline has {len(xs)} points in {num_contours} contours')
