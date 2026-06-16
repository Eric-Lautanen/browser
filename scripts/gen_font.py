#!/usr/bin/env python3
"""Generate a minimal TrueType font with readable ASCII glyph outlines."""
import struct, math, os

OUTPUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                      "render", "embedded_font.cpp")

UPEM = 1000
ASCENDER = 800
DESCENDER = -200
LINE_GAP = 0

CHARS = [chr(i) for i in range(32, 127)]
NUM_GLYPHS = 1 + len(CHARS)
NOTDEF_IDX = 0

# Each char defined as list of (x, y, w, h) rectangles in a 5x8 grid
# Grid cell = (unit_w, unit_h) = (120, 100) font units
# Baseline at row 7, descenders at row 8
GRID_COLS, GRID_ROWS = 6, 9
CELL_W, CELL_H = 80, 90
BASE_ROW = 6  # baseline row (0-indexed from top)

def cell_rect(col, row, w=1, h=1):
    return (col * CELL_W, (row) * CELL_H, w * CELL_W, h * CELL_H)

GLYPH_DEFS = {
    ' ': [],
    '!': [(2,2,1,5), (2,0,1,1)],
    '"': [(1,1,1,2), (3,1,1,2)],
    '#': [(1,1,2,6), (3,1,2,6), (0,2,5,1), (0,4,5,1)],
    '$': [(2,0,1,8), (1,2,3,1), (1,5,3,1), (0,1,5,1), (0,6,5,1)],
    '%': [(0,0,2,2), (3,5,2,2), (1,2,2,2), (4,0,1,1), (0,5,1,1)],
    '&': [(2,1,3,2), (3,3,1,1), (0,1,2,5), (2,4,2,3), (4,4,1,1)],
    "'": [(2,1,1,2)],
    '(': [(3,0,2,8), (1,1,2,6)],
    ')': [(1,0,2,8), (3,1,2,6)],
    '*': [(2,0,1,4), (0,1,5,1), (1,2,3,1)],
    '+': [(2,2,1,4), (0,3,5,1)],
    ',': [(2,6,2,2), (3,6,1,1)],
    '-': [(0,3,5,1)],
    '.': [(2,6,1,1)],
    '/': [(3,0,2,8)],
    '0': [(1,0,3,8), (0,1,1,6), (4,1,1,6), (1,0,3,1), (1,7,3,1)],
    '1': [(2,0,1,8), (1,7,3,1), (1,0,1,1)],
    '2': [(0,0,5,2), (4,2,1,2), (0,4,5,2), (0,4,1,2), (0,7,5,1), (0,7,1,1)],
    '3': [(0,0,5,1), (4,1,1,2), (1,3,3,1), (4,4,1,2), (0,7,5,1)],
    '4': [(3,0,1,8), (0,3,5,1), (0,3,1,2)],
    '5': [(0,0,5,1), (0,0,1,3), (1,3,4,1), (4,4,1,2), (0,6,4,1)],
    '6': [(1,0,3,1), (0,1,1,6), (1,3,4,1), (4,4,1,2), (1,6,3,1)],
    '7': [(0,0,5,1), (4,1,1,1), (3,2,1,6)],
    '8': [(1,0,3,1), (0,1,1,6), (4,1,1,6), (1,7,3,1), (1,3,3,1)],
    '9': [(1,0,3,1), (0,1,1,4), (4,1,1,6), (1,3,3,1), (1,6,3,1)],
    ':': [(2,2,1,1), (2,5,1,1)],
    ';': [(2,2,1,1), (2,5,2,2)],
    '<': [(4,0,1,1), (3,1,1,1), (0,3,3,2), (3,5,1,2), (4,7,1,1)],
    '=': [(0,2,5,1), (0,4,5,1)],
    '>': [(0,0,1,1), (1,1,1,1), (0,5,1,2), (4,3,1,2), (1,7,1,1)],
    '?': [(1,0,3,1), (4,1,1,2), (2,3,2,1), (2,5,1,1), (2,7,1,1)],
    '@': [(1,1,3,1), (0,2,1,4), (1,6,3,1), (4,2,1,4), (2,3,2,2)],
    'A': [(0,0,5,1), (0,0,1,7), (4,0,1,7), (0,3,5,1)],
    'B': [(0,0,4,1), (0,0,1,8), (4,1,1,2), (0,3,4,1), (4,4,1,3), (0,7,4,1)],
    'C': [(1,0,3,1), (0,1,1,6), (1,7,3,1), (4,6,1,1)],
    'D': [(0,0,4,1), (0,0,1,8), (4,1,1,6), (0,7,4,1)],
    'E': [(0,0,5,1), (0,0,1,8), (0,3,4,1), (0,7,5,1)],
    'F': [(0,0,5,1), (0,0,1,8), (0,3,4,1)],
    'G': [(1,0,3,1), (0,1,1,7), (1,7,3,1), (4,3,1,4), (2,3,2,1)],
    'H': [(0,0,1,8), (4,0,1,8), (0,3,5,1)],
    'I': [(0,0,5,1), (2,0,1,8), (0,7,5,1)],
    'J': [(4,0,1,7), (1,7,3,1), (0,5,1,2)],
    'K': [(0,0,1,8), (4,0,1,3), (2,3,2,1), (3,4,2,4)],
    'L': [(0,0,1,8), (0,7,5,1)],
    'M': [(0,0,1,8), (4,0,1,8), (1,0,3,4)],
    'N': [(0,0,1,8), (4,0,1,8), (0,0,5,8)],
    'O': [(1,0,3,1), (0,1,1,6), (4,1,1,6), (1,7,3,1)],
    'P': [(0,0,4,1), (0,0,1,8), (4,1,1,2), (0,3,4,1)],
    'Q': [(1,0,3,1), (0,1,1,6), (4,1,1,6), (1,7,3,1), (3,5,2,3)],
    'R': [(0,0,4,1), (0,0,1,8), (4,1,1,2), (0,3,4,1), (3,4,2,4)],
    'S': [(1,0,3,1), (0,1,1,2), (1,3,3,1), (4,4,1,2), (1,7,3,1)],
    'T': [(0,0,5,1), (2,0,1,8)],
    'U': [(0,0,1,7), (4,0,1,7), (1,7,3,1)],
    'V': [(0,0,1,5), (4,0,1,5), (2,5,1,3)],
    'W': [(0,0,1,7), (5,0,1,7), (2,0,2,5)],
    'X': [(0,0,1,3), (4,0,1,3), (0,5,1,3), (4,5,1,3), (2,3,1,2)],
    'Y': [(0,0,1,3), (4,0,1,3), (2,3,1,5)],
    'Z': [(0,0,5,1), (0,7,5,1), (4,0,1,1), (0,5,1,1), (0,3,3,2)],
    '[': [(1,0,3,1), (1,0,1,8), (1,7,3,1)],
    '\\': [(1,0,1,8)],
    ']': [(1,0,3,1), (3,0,1,8), (1,7,3,1)],
    '^': [(2,0,1,2), (1,1,1,1), (3,1,1,1)],
    '_': [(0,7,5,1)],
    '`': [(2,0,1,2), (1,1,1,1)],
    'a': [(0,2,4,1), (0,2,1,5), (4,2,1,3), (0,5,4,1)],
    'b': [(0,0,1,8), (0,2,4,1), (0,6,4,1), (4,2,1,4)],
    'c': [(1,2,3,1), (0,3,1,3), (1,6,3,1)],
    'd': [(4,0,1,8), (0,2,4,1), (0,6,4,1), (0,2,1,4)],
    'e': [(1,2,3,1), (0,3,1,3), (0,4,4,1), (1,6,3,1)],
    'f': [(2,0,2,1), (1,1,1,7), (0,3,3,1)],
    'g': [(0,2,4,1), (0,2,1,6), (4,2,1,4), (0,6,4,1), (1,7,3,1)],
    'h': [(0,0,1,8), (0,2,4,1), (0,5,4,1), (4,2,1,4)],
    'i': [(2,1,1,6), (2,0,1,1)],
    'j': [(3,1,1,6), (1,7,3,1), (3,0,1,1)],
    'k': [(0,0,1,8), (3,2,2,2), (0,4,2,1), (2,5,3,3)],
    'l': [(1,0,3,1), (2,0,1,7), (1,7,3,1)],
    'm': [(0,2,1,5), (0,3,5,1), (5,2,1,5), (2,2,2,3)],
    'n': [(0,2,1,5), (0,2,4,1), (0,5,4,1), (4,2,1,4)],
    'o': [(1,2,3,1), (0,3,1,3), (4,3,1,3), (1,6,3,1)],
    'p': [(0,2,1,6), (0,2,4,1), (0,6,4,1), (4,2,1,4)],
    'q': [(4,2,1,6), (0,2,4,1), (0,6,4,1), (0,2,1,4)],
    'r': [(0,2,1,5), (0,2,3,1)],
    's': [(1,2,3,1), (0,3,1,1), (1,4,3,1), (4,5,1,1), (1,6,3,1)],
    't': [(1,0,1,7), (0,2,4,1), (0,6,2,1)],
    'u': [(0,2,1,4), (4,2,1,4), (0,6,4,1)],
    'v': [(0,2,1,3), (4,2,1,3), (2,5,1,2)],
    'w': [(0,2,1,4), (5,2,1,4), (2,2,2,3)],
    'x': [(0,2,1,2), (4,2,1,2), (0,5,1,2), (4,5,1,2), (2,3,1,2)],
    'y': [(0,2,1,3), (4,2,1,3), (2,5,1,3), (4,8,1,1)],
    'z': [(0,2,5,1), (0,6,5,1), (4,2,1,1), (0,5,1,1), (0,3,3,2)],
    '{': [(3,0,2,1), (1,1,2,2), (1,3,2,1), (1,4,2,2), (3,7,2,1)],
    '|': [(2,0,1,8)],
    '}': [(1,0,2,1), (3,1,2,2), (3,3,2,1), (3,4,2,2), (1,7,2,1)],
    '~': [(0,3,2,1), (3,4,2,1), (1,2,1,1), (2,5,1,1)],
}

def glyph_width(gid):
    if gid == NOTDEF_IDX:
        return 500
    c = CHARS[gid - 1]
    if ord(c) == 32:
        return 300
    return max((x + w for x,w,_1,_2 in GLYPH_DEFS.get(c, [(0,0,1,1)])), default=5) * CELL_W + CELL_W

def glyph_outline(gid):
    if gid == NOTDEF_IDX:
        return (1, [3], [50,50, 450,50, 450,950, 50,950])
    c = CHARS[gid - 1]
    if ord(c) == 32:
        return None
    segs = GLYPH_DEFS.get(c, [(1,1,3,5)])
    contours = []
    for x,y,w,h in segs:
        x0 = x * CELL_W
        x1 = x0 + w * CELL_W
        y0 = BASE_ROW * CELL_H - (y + h) * CELL_H + CELL_H
        y1 = y0 + h * CELL_H
        contours.extend([x0, y0, x1, y0, x1, y1, x0, y1])
    n_pts = len(contours) // 2
    end_pts = [4*i+3 for i in range(len(segs))]
    return (len(segs), end_pts, contours)

def pad4(data):
    while len(data) % 4 != 0:
        data += b'\0'
    return data

def checksum(data):
    if len(data) % 4 != 0:
        data = pad4(data)
    s = 0
    for i in range(0, len(data), 4):
        s = (s + struct.unpack('>I', data[i:i+4])[0]) & 0xFFFFFFFF
    return s

def build_cmap():
    ranges = {}
    for i, c in enumerate(CHARS):
        gid = i + 1
        cp = ord(c)
        ranges[cp] = gid
    sorted_cps = sorted(ranges.keys())
    start_code, end_code, id_delta, id_range_offset = [], [], [], []
    i = 0
    while i < len(sorted_cps):
        cp_start = sorted_cps[i]
        gid_start = ranges[cp_start]
        cp_end = cp_start
        i += 1
        while i < len(sorted_cps) and sorted_cps[i] == cp_end + 1:
            expected_gid = gid_start + (sorted_cps[i] - cp_start)
            if ranges[sorted_cps[i]] != expected_gid:
                break
            cp_end = sorted_cps[i]
            i += 1
        start_code.append(cp_start)
        end_code.append(cp_end)
        delta = gid_start - cp_start
        id_delta.append(delta & 0xFFFF)
        id_range_offset.append(0)
    start_code.append(0xFFFF)
    end_code.append(0xFFFF)
    id_delta.append(1)
    id_range_offset.append(0)
    seg_count = len(start_code)
    search_range = 2 * (2 ** int(math.log2(seg_count)))
    entry_selector = int(math.log2(seg_count))
    range_shift = 2 * seg_count - search_range
    body = b''
    body += struct.pack('>HHH', 0, 2 * seg_count, search_range)
    body += struct.pack('>HH', entry_selector, range_shift)
    for e in end_code: body += struct.pack('>H', e)
    body += struct.pack('>H', 0)
    for s in start_code: body += struct.pack('>H', s)
    for d in id_delta: body += struct.pack('>h', d if d < 0x8000 else d - 0x10000)
    for r in id_range_offset: body += struct.pack('>H', r)
    subtable_len = 4 + len(body)
    subtable = struct.pack('>HH', 4, subtable_len) + body
    cmap_header = struct.pack('>HH', 0, 1)
    sub_offset = 4 + 8
    encoding_record = struct.pack('>HHI', 3, 1, sub_offset)
    return cmap_header + encoding_record + subtable

def make_font():
    outlines, adv_widths = [], []
    for gid in range(NUM_GLYPHS):
        outlines.append(glyph_outline(gid))
        adv_widths.append(glyph_width(gid))

    glyf_data, loca_offsets = b'', []
    x_min_all, y_min_all = 32767, 32767
    x_max_all, y_max_all = -32768, -32768

    for gid in range(NUM_GLYPHS):
        loca_offsets.append(len(glyf_data) // 2)
        ol = outlines[gid]
        if ol is None:
            glyf_data += struct.pack('>hhhhh', 0, 0, 0, 0, 0)
        else:
            num_contours, end_pts, pts = ol
            xs = pts[0::2]; ys = pts[1::2]
            xmin, ymin = int(min(xs)), int(min(ys))
            xmax, ymax = int(max(xs)), int(max(ys))
            x_min_all = min(x_min_all, xmin)
            y_min_all = min(y_min_all, ymin)
            x_max_all = max(x_max_all, xmax)
            y_max_all = max(y_max_all, ymax)
            header = struct.pack('>hhhhh', num_contours, xmin, ymin, xmax, ymax)
            end_pts_data = b''.join(struct.pack('>H', ep) for ep in end_pts)
            instr = struct.pack('>H', 0)
            flags_data, x_data, y_data = b'', b'', b''
            prev_x, prev_y = 0, 0
            for i in range(len(pts) // 2):
                x, y = int(pts[i*2]), int(pts[i*2+1])
                dx, dy = x - prev_x, y - prev_y
                flags = 1
                if -256 <= dx < 256:
                    flags |= 0x02
                    if dx >= 0: flags |= 0x10
                    x_data += struct.pack('B', abs(dx))
                else:
                    x_data += struct.pack('>h', dx)
                if -256 <= dy < 256:
                    flags |= 0x04
                    if dy >= 0: flags |= 0x20
                    y_data += struct.pack('B', abs(dy))
                else:
                    y_data += struct.pack('>h', dy)
                prev_x, prev_y = x, y
                flags_data += struct.pack('B', flags)
            glyf_data += header + end_pts_data + instr + flags_data + x_data + y_data
            if len(glyf_data) % 2 != 0:
                glyf_data += b'\x00'

    loca_offsets.append(len(glyf_data) // 2)
    loca_data = b''.join(struct.pack('>H', off) for off in loca_offsets)
    hmtx_data = b''.join(struct.pack('>Hh', adv_widths[gid], 50) for gid in range(NUM_GLYPHS))

    maxp_data = struct.pack('>IH', 0x00010000, NUM_GLYPHS) + b'\0' * 28
    hhea_data = struct.pack('>I', 0x00010000) + struct.pack('>hhh', ASCENDER, DESCENDER, LINE_GAP) + b'\0' * 26 + struct.pack('>hH', 0, NUM_GLYPHS)
    name_data = struct.pack('>HHH', 0, 1, 6) + struct.pack('>HHHHHH', 1, 0, 0, 0, 0, 0)
    post_data = struct.pack('>I', 0x00030000) + struct.pack('>h', 0) * 2 + struct.pack('>HI', 0, 0) * 5
    os2_data = struct.pack('>HhHH', 4, 500, 500, 5) + struct.pack('>H', 0) + b'\0' * 30 + struct.pack('>IIII', 0,0,0,0) + b'BBRW' + struct.pack('>H', 0x40) + struct.pack('>HH', 32, 126) + struct.pack('>hhh', ASCENDER, DESCENDER, LINE_GAP) + struct.pack('>HH', ASCENDER, abs(DESCENDER)) + struct.pack('>II', 0, 0) + struct.pack('>hh', 0, 0) + struct.pack('>HHH', 0, 0, 3)
    head_data = struct.pack('>IIII', 0x00010000, 0, 0, 0x5F0F3CF5) + struct.pack('>HH', 0, UPEM) + struct.pack('>qq', 0, 0) + struct.pack('>hhhh', x_min_all, y_min_all, x_max_all, y_max_all) + struct.pack('>HHh', 0, 0, 2) + struct.pack('>hh', 0, 0)
    cmap_data = build_cmap()

    tables = [('cmap',cmap_data),('head',head_data),('hhea',hhea_data),('hmtx',hmtx_data),('maxp',maxp_data),('name',name_data),('OS/2',os2_data),('post',post_data),('loca',loca_data),('glyf',glyf_data)]
    num_tables = len(tables)
    entry_selector = int(math.log2(num_tables))
    search_range = 16 * (2 ** entry_selector)
    range_shift = 16 * num_tables - search_range
    offset = 12 + num_tables * 16
    table_dir, raw_tables, head_offset = b'', [], 0
    for tag, data in tables:
        padded = data
        while len(padded) % 4: padded += b'\0'
        chk = checksum(padded)
        table_dir += tag.encode() + struct.pack('>III', chk, offset, len(data))
        if tag == 'head': head_offset = offset + 8
        raw_tables.append(padded)
        offset += len(padded)
    sfnt = struct.pack('>IHHHH', 0x00010000, num_tables, search_range, entry_selector, range_shift)
    font_data = bytearray(sfnt + table_dir + b''.join(raw_tables))
    font_data[head_offset:head_offset+4] = b'\0\0\0\0'
    total = sum(struct.unpack('>I', font_data[i:i+4].ljust(4, b'\0'))[0] for i in range(0, len(font_data), 4)) & 0xFFFFFFFF
    font_data[head_offset:head_offset+4] = struct.pack('>I', (0xB1B0AFBA - total) & 0xFFFFFFFF)
    return bytes(font_data)

font_bytes = make_font()

with open(OUTPUT, 'w') as f:
    f.write('#include "../tests/utility.hpp"\n\nnamespace browser::render {\n\nextern const u8 DEFAULT_FONT_DATA[] = {\n')
    for i in range(0, len(font_bytes), 12):
        chunk = font_bytes[i:i+12]
        f.write('    ' + ', '.join(f'0x{b:02X}' for b in chunk) + ',\n')
    f.write('};\n\nextern const u32 DEFAULT_FONT_DATA_SIZE = ' + str(len(font_bytes)) + ';\n\n} // namespace browser::render\n')

print(f"OK: Wrote {len(font_bytes)} bytes to {OUTPUT}")
print(f"Chars: {len(GLYPH_DEFS)} defined")
