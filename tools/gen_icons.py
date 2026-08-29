#!/usr/bin/env python3
"""Generates the browser chrome icon bitmaps (render/icons_bitmaps.inc).

Each icon is defined as vector geometry in a 16x16 icon space (y grows
downward) and rasterized to the project's 1-bit format (255 = filled) with
4x4 supersampling per pixel, which yields clean diagonal strokes and smooth
curves at 16 px. Run from the repo root:

    python3 tools/gen_icons.py

Output is committed; re-run only when changing icon geometry.
"""

import math
import os

SIZE = 16
SS = 4  # supersampling grid per axis


# ---------------------------------------------------------------------------
# Geometry helpers (all operate in icon-space floats)
# ---------------------------------------------------------------------------

def point_in_polygon(pt, poly):
    x, y = pt
    inside = False
    n = len(poly)
    j = n - 1
    for i in range(n):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > y) != (yj > y):
            x_int = (xj - xi) * (y - yi) / (yj - yi) + xi
            if x < x_int:
                inside = not inside
        j = i
    return inside


def dist_to_segment(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    len2 = dx * dx + dy * dy
    if len2 == 0.0:
        return math.hypot(px - ax, py - ay)
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / len2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def scale_toward_center(v, c, inset):
    """Move polygon vertex v toward c by inset px (for outline stars)."""
    vx, vy = v[0] - c[0], v[1] - c[1]
    d = math.hypot(vx, vy)
    f = max(0.1, d - inset) / d
    return (c[0] + vx * f, c[1] + vy * f)


def star_polygon(cx, cy, r_outer, r_inner, points=5, rotation=-90.0):
    poly = []
    for i in range(points * 2):
        r = r_outer if i % 2 == 0 else r_inner
        a = math.radians(rotation + i * 180.0 / points)
        poly.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return poly


def in_rect(x, y, x0, y0, x1, y1):
    return x0 <= x <= x1 and y0 <= y <= y1


def in_rounded_rect(x, y, x0, y0, x1, y1, rad):
    if not in_rect(x, y, x0, y0, x1, y1):
        return False
    cx = min(max(x, x0 + rad), x1 - rad)
    cy = min(max(y, y0 + rad), y1 - rad)
    return math.hypot(x - cx, y - cy) <= rad


def in_stroke(x, y, ax, ay, bx, by, w):
    return dist_to_segment(x, y, ax, ay, bx, by) <= w * 0.5


def in_ring(x, y, cx, cy, r_in, r_out, a_start_deg, a_end_deg):
    """Ring sector from a_start to a_end going clockwise on screen
    (angles in degrees, 0=+x, 90=+y/down)."""
    dx, dy = x - cx, y - cy
    r = math.hypot(dx, dy)
    if r < r_in or r > r_out:
        return False
    a = math.degrees(math.atan2(dy, dx))  # -180..180, increases clockwise on screen
    # normalize a into [a_start, a_start + sweep)
    sweep = (a_end_deg - a_start_deg) % 360.0
    rel = (a - a_start_deg) % 360.0
    return rel <= sweep


def arrow_head(cx, cy, tip_angle_deg, length, half_width):
    """Triangle whose tip points along tip_angle_deg (screen clockwise)."""
    a = math.radians(tip_angle_deg)
    tx, ty = math.cos(a), math.sin(a)
    nx, ny = -ty, tx
    tip = (cx + tx * length, cy + ty * length)
    b1 = (cx + nx * half_width, cy + ny * half_width)
    b2 = (cx - nx * half_width, cy - ny * half_width)
    return [tip, b1, b2]


# ---------------------------------------------------------------------------
# Icon definitions: each is a function (x, y) -> bool, 16x16 icon space
# ---------------------------------------------------------------------------

ARROW_BODY = [(2.5, 8.0), (8.5, 2.75), (8.5, 6.0), (13.5, 6.0), (13.5, 10.0), (8.5, 10.0), (8.5, 13.25)]


def mirror_poly(poly):
    return [(16.0 - x, y) for x, y in poly]


REFRESH_CX, REFRESH_CY = 8.0, 8.0
REFRESH_R_IN, REFRESH_R_OUT = 4.2, 6.6
REFRESH_START, REFRESH_END = 285.0, 215.0  # sweep clockwise 290°; 70° gap at the top


def refresh_test(x, y):
    if in_ring(x, y, REFRESH_CX, REFRESH_CY, REFRESH_R_IN, REFRESH_R_OUT, REFRESH_START, REFRESH_END):
        return True
    # Arrowhead at the clockwise end of the arc (upper-left of the gap),
    # pointing along the direction of travel (clockwise tangent). It is
    # taller than the ring band so it reads as an arrowhead, not a blob.
    a = math.radians(REFRESH_END)
    px = REFRESH_CX + (REFRESH_R_OUT + REFRESH_R_IN) * 0.5 * math.cos(a)
    py = REFRESH_CY + (REFRESH_R_OUT + REFRESH_R_IN) * 0.5 * math.sin(a)
    tangent = REFRESH_END + 90.0  # clockwise tangent direction
    head = arrow_head(px, py, tangent, 5.6, 4.0)
    head = [(hx + 1.6 * math.cos(math.radians(tangent)), hy + 1.6 * math.sin(math.radians(tangent)))
            for hx, hy in head]
    return point_in_polygon((x, y), head)


STAR = star_polygon(8.0, 8.0, 7.1, 3.4)


def star_outline_test(x, y):
    # Uniform stroke along the star's edges (fill XOR won't work here — the
    # inner radius is too tight for a clean inset at 16 px)
    stroke = 1.7
    n = len(STAR)
    j = n - 1
    for i in range(n):
        if dist_to_segment(x, y, STAR[j][0], STAR[j][1], STAR[i][0], STAR[i][1]) <= stroke * 0.5:
            return True
        j = i
    return False


def make_icons():
    icons = {}

    icons["ICON_BACK"] = lambda x, y: point_in_polygon((x, y), ARROW_BODY)
    icons["ICON_FORWARD"] = lambda x, y: point_in_polygon((x, y), mirror_poly(ARROW_BODY))
    icons["ICON_REFRESH"] = refresh_test
    icons["ICON_STOP"] = lambda x, y: in_rounded_rect(x, y, 4.6, 4.6, 11.4, 11.4, 1.6)
    icons["ICON_CLOSE"] = lambda x, y: (in_stroke(x, y, 3.3, 3.3, 12.7, 12.7, 1.9)
                                        or in_stroke(x, y, 12.7, 3.3, 3.3, 12.7, 1.9))
    icons["ICON_MINIMIZE"] = lambda x, y: in_rect(x, y, 3.0, 10.9, 13.0, 12.7)
    icons["ICON_MAXIMIZE"] = lambda x, y: (in_rect(x, y, 3.2, 3.2, 12.8, 12.8)
                                           and not in_rect(x, y, 5.2, 5.2, 10.8, 10.8))
    icons["ICON_MENU"] = lambda x, y: (in_rect(x, y, 3.0, 3.3, 13.0, 5.1)
                                       or in_rect(x, y, 3.0, 7.1, 13.0, 8.9)
                                       or in_rect(x, y, 3.0, 10.9, 13.0, 12.7))
    icons["ICON_BOOKMARK_ON"] = lambda x, y: point_in_polygon((x, y), STAR)
    icons["ICON_BOOKMARK_OFF"] = star_outline_test
    return icons


# ---------------------------------------------------------------------------
# Rasterization + emission
# ---------------------------------------------------------------------------

def rasterize(test):
    rows = []
    for row in range(SIZE):
        vals = []
        for col in range(SIZE):
            hits = 0
            for sy in range(SS):
                for sx in range(SS):
                    x = col + (sx + 0.5) / SS
                    y = row + (sy + 0.5) / SS
                    if test(x, y):
                        hits += 1
            vals.append(255 if hits * 2 >= SS * SS else 0)
        rows.append(vals)
    return rows


def emit():
    icons = make_icons()
    order = ["ICON_BACK", "ICON_FORWARD", "ICON_REFRESH", "ICON_STOP", "ICON_CLOSE",
             "ICON_MINIMIZE", "ICON_MAXIMIZE", "ICON_MENU", "ICON_BOOKMARK_ON",
             "ICON_BOOKMARK_OFF"]
    lines = []
    lines.append("// ---------------------------------------------------------------------------")
    lines.append("// Icon bitmaps — GENERATED by tools/gen_icons.py. Do not edit by hand.")
    lines.append("// 16x16, 1-bit (255 = filled), rasterized with 4x4 supersampling.")
    lines.append("// ---------------------------------------------------------------------------")
    lines.append("")
    lines.append("// clang-format off")
    for name in order:
        rows = rasterize(icons[name])
        lines.append(f"static const u8 {name}[ICON_W * ICON_H] = {{")
        for vals in rows:
            lines.append("    " + ", ".join(str(v) for v in vals) + ",")
        lines.append("};")
        lines.append("")
    lines.append("// clang-format on")
    return "\n".join(lines)


if __name__ == "__main__":
    out = os.path.join(os.path.dirname(__file__), "..", "render", "icons_bitmaps.inc")
    with open(os.path.abspath(out), "w", newline="\n") as f:
        f.write(emit())
    print("wrote", os.path.abspath(out))
