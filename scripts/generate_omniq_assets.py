#!/usr/bin/env python3

import os
import sys
import json
import math
import time
import shutil
import cairo
from PIL import Image, ImageDraw, ImageFont, ImageFilter

C_BASE_DARK = (0x0D / 255.0, 0x0B / 255.0, 0x18 / 255.0)
C_SURF_DARK = (0x14 / 255.0, 0x11 / 255.0, 0x26 / 255.0)
C_ELEV_DARK = (0x1A / 255.0, 0x15 / 255.0, 0x30 / 255.0)
C_CYAN      = (0x00 / 255.0, 0xE5 / 255.0, 0xFF / 255.0)
C_VIOLET    = (0x8A / 255.0, 0x4F / 255.0, 0xFF / 255.0)
C_MINT      = (0x00 / 255.0, 0xFF / 255.0, 0x9D / 255.0)
C_CRIMSON   = (0xFF / 255.0, 0x33 / 255.0, 0x66 / 255.0)
C_AMBER     = (1.00, 0.76, 0.18)

RESOLUTIONS = [16, 22, 24, 32, 48, 64, 96, 128, 256, 512]

def draw_round_rect(ctx, x, y, w, h, r):
    ctx.new_sub_path()
    ctx.arc(x + w - r, y + r, r, -math.pi / 2, 0)
    ctx.arc(x + w - r, y + h - r, r, 0, math.pi / 2)
    ctx.arc(x + r, y + h - r, r, math.pi / 2, math.pi)
    ctx.arc(x + r, y + r, r, math.pi, 3 * math.pi / 2)
    ctx.close_path()

def draw_glass_squircle(ctx, size, accent="cyan"):
    if size < 32:
        return

    ctx.save()
    draw_round_rect(ctx, 0.08, 0.11, 0.84, 0.84, 0.21)
    ctx.set_source_rgba(0.01, 0.01, 0.03, 0.55)
    ctx.fill()
    ctx.restore()

    draw_round_rect(ctx, 0.07, 0.07, 0.86, 0.86, 0.21)
    pat = cairo.LinearGradient(0, 0.07, 0, 0.93)
    pat.add_color_stop_rgba(0.0, 0.14, 0.11, 0.28, 0.95)
    pat.add_color_stop_rgba(1.0, 0.05, 0.04, 0.11, 0.98)
    ctx.set_source(pat)
    ctx.fill_preserve()

    bpat = cairo.LinearGradient(0.07, 0.07, 0.93, 0.93)
    if accent == "violet":
        bpat.add_color_stop_rgba(0.0, *C_VIOLET, 0.90)
        bpat.add_color_stop_rgba(1.0, *C_CYAN, 0.55)
    elif accent == "mint":
        bpat.add_color_stop_rgba(0.0, *C_MINT, 0.90)
        bpat.add_color_stop_rgba(1.0, *C_CYAN, 0.55)
    elif accent == "crimson":
        bpat.add_color_stop_rgba(0.0, *C_CRIMSON, 0.92)
        bpat.add_color_stop_rgba(1.0, *C_VIOLET, 0.60)
    else:
        bpat.add_color_stop_rgba(0.0, *C_CYAN, 0.90)
        bpat.add_color_stop_rgba(1.0, *C_VIOLET, 0.60)

    ctx.set_source(bpat)
    ctx.set_line_width(0.028)
    ctx.stroke()

    ctx.save()
    draw_round_rect(ctx, 0.10, 0.10, 0.80, 0.36, 0.17)
    gpat = cairo.LinearGradient(0, 0.10, 0, 0.46)
    gpat.add_color_stop_rgba(0.0, 1.0, 1.0, 1.0, 0.16)
    gpat.add_color_stop_rgba(1.0, 1.0, 1.0, 1.0, 0.0)
    ctx.set_source(gpat)
    ctx.fill()
    ctx.restore()

def glyph_omniq_logo(ctx):
    ctx.save()
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    cx, cy = 0.48, 0.48
    r_outer = 0.28

    segments = [
        (-math.pi / 2, -math.pi / 6, C_CYAN),
        (-math.pi / 6 + 0.16, math.pi * 0.42, C_VIOLET),
        (math.pi * 0.42 + 0.16, math.pi * 1.08, C_MINT),
        (math.pi * 1.08 + 0.16, math.pi * 1.5 - 0.16, C_CYAN),
    ]
    ctx.set_line_width(0.065)
    for a1, a2, col in segments:
        ctx.set_source_rgba(*col, 0.98)
        ctx.arc(cx, cy, r_outer, a1, a2)
        ctx.stroke()

    ctx.save()
    ctx.set_dash([0.045, 0.035], 0)
    ctx.set_line_width(0.03)
    ctx.set_source_rgba(*C_CYAN, 0.88)
    ctx.arc(cx, cy, 0.145, 0, 2 * math.pi)
    ctx.stroke()
    ctx.restore()

    ctx.arc(cx, cy, 0.055, 0, 2 * math.pi)
    ctx.set_source_rgba(1.0, 1.0, 1.0, 0.98)
    ctx.fill()

    ctx.set_line_width(0.065)
    pat = cairo.LinearGradient(cx + 0.11, cy + 0.11, 0.80, 0.80)
    pat.add_color_stop_rgba(0.0, *C_CYAN, 1.0)
    pat.add_color_stop_rgba(1.0, *C_VIOLET, 1.0)
    ctx.set_source(pat)
    ctx.move_to(cx + 0.12, cy + 0.12)
    ctx.line_to(0.79, 0.79)
    ctx.stroke()

    ctx.arc(0.80, 0.80, 0.04, 0, 2 * math.pi)
    ctx.set_source_rgba(*C_MINT, 1.0)
    ctx.fill()
    ctx.restore()

def glyph_apps_grid(ctx):
    ctx.save()
    coords = [0.27, 0.50, 0.73]
    for ri, cy in enumerate(coords):
        for ci, cx in enumerate(coords):
            col = C_CYAN if (ri + ci) % 2 == 0 else C_VIOLET
            if ri == 1 and ci == 1:
                col = C_MINT
            draw_round_rect(ctx, cx - 0.075, cy - 0.075, 0.15, 0.15, 0.04)
            ctx.set_source_rgba(*col, 0.95)
            ctx.fill()
    ctx.restore()

def glyph_spotlight(ctx):
    ctx.save()
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    cx, cy, r = 0.44, 0.44, 0.19
    ctx.arc(cx, cy, r, 0, 2 * math.pi)
    ctx.set_source_rgba(*C_BASE_DARK, 0.75)
    ctx.fill_preserve()

    pat = cairo.LinearGradient(0.24, 0.24, 0.65, 0.65)
    pat.add_color_stop_rgba(0.0, *C_CYAN, 1.0)
    pat.add_color_stop_rgba(1.0, *C_VIOLET, 0.95)
    ctx.set_source(pat)
    ctx.set_line_width(0.055)
    ctx.stroke()

    ctx.set_line_width(0.07)
    ctx.move_to(0.58, 0.58)
    ctx.line_to(0.77, 0.77)
    ctx.stroke()

    ctx.set_line_width(0.028)
    ctx.set_source_rgba(*C_MINT, 0.95)
    ctx.move_to(cx - 0.06, cy)
    ctx.line_to(cx + 0.06, cy)
    ctx.move_to(cx, cy - 0.06)
    ctx.line_to(cx, cy + 0.06)
    ctx.stroke()
    ctx.restore()

def glyph_terminal(ctx):
    ctx.save()
    draw_round_rect(ctx, 0.17, 0.21, 0.66, 0.58, 0.09)
    ctx.set_source_rgba(0.05, 0.04, 0.11, 0.96)
    ctx.fill_preserve()
    ctx.set_line_width(0.035)
    ctx.set_source_rgba(*C_CYAN, 0.85)
    ctx.stroke()

    for dx, col in [(0.27, C_CRIMSON), (0.35, C_AMBER), (0.43, C_MINT)]:
        ctx.arc(dx, 0.30, 0.024, 0, 2 * math.pi)
        ctx.set_source_rgba(*col, 0.95)
        ctx.fill()

    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    ctx.set_line_width(0.045)
    ctx.set_source_rgba(*C_CYAN, 1.0)
    ctx.move_to(0.28, 0.45)
    ctx.line_to(0.42, 0.55)
    ctx.line_to(0.28, 0.65)
    ctx.stroke()

    ctx.set_source_rgba(*C_MINT, 1.0)
    ctx.move_to(0.48, 0.65)
    ctx.line_to(0.66, 0.65)
    ctx.stroke()
    ctx.restore()

def glyph_files(ctx):
    ctx.save()
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    ctx.move_to(0.20, 0.34)
    ctx.line_to(0.39, 0.34)
    ctx.line_to(0.45, 0.41)
    ctx.line_to(0.80, 0.41)
    ctx.line_to(0.80, 0.73)
    ctx.line_to(0.20, 0.73)
    ctx.close_path()

    pat = cairo.LinearGradient(0.20, 0.34, 0.80, 0.73)
    pat.add_color_stop_rgba(0.0, *C_CYAN, 0.88)
    pat.add_color_stop_rgba(1.0, 0.0, 0.48, 0.85, 0.92)
    ctx.set_source(pat)
    ctx.fill_preserve()
    ctx.set_line_width(0.032)
    ctx.set_source_rgba(*C_CYAN, 1.0)
    ctx.stroke()

    ctx.move_to(0.20, 0.48)
    ctx.line_to(0.80, 0.48)
    ctx.line_to(0.76, 0.73)
    ctx.line_to(0.24, 0.73)
    ctx.close_path()
    ctx.set_source_rgba(*C_VIOLET, 0.65)
    ctx.fill_preserve()
    ctx.set_source_rgba(0.85, 0.78, 1.0, 0.85)
    ctx.stroke()
    ctx.restore()

def glyph_circuits_folder(ctx):
    glyph_files(ctx)
    ctx.save()
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_width(0.028)
    ctx.set_source_rgba(1.0, 1.0, 1.0, 0.95)
    ctx.move_to(0.33, 0.58)
    ctx.line_to(0.67, 0.58)
    ctx.move_to(0.33, 0.66)
    ctx.line_to(0.67, 0.66)
    ctx.stroke()
    draw_round_rect(ctx, 0.41, 0.53, 0.10, 0.10, 0.02)
    ctx.set_source_rgba(*C_MINT, 1.0)
    ctx.fill()
    ctx.arc(0.60, 0.66, 0.032, 0, 2 * math.pi)
    ctx.set_source_rgba(*C_CYAN, 1.0)
    ctx.fill()
    ctx.restore()

def glyph_settings(ctx):
    ctx.save()
    cx, cy = 0.50, 0.50
    teeth = 8
    r_outer, r_inner, r_center = 0.27, 0.20, 0.09
    ctx.new_path()
    for i in range(teeth * 2):
        angle = i * math.pi / teeth
        r = r_outer if i % 2 == 0 else r_inner
        x = cx + r * math.cos(angle)
        y = cy + r * math.sin(angle)
        if i == 0:
            ctx.move_to(x, y)
        else:
            ctx.line_to(x, y)
    ctx.close_path()
    pat = cairo.LinearGradient(0.23, 0.23, 0.77, 0.77)
    pat.add_color_stop_rgba(0.0, *C_VIOLET, 1.0)
    pat.add_color_stop_rgba(1.0, *C_CYAN, 1.0)
    ctx.set_source(pat)
    ctx.fill_preserve()
    ctx.set_line_width(0.022)
    ctx.set_source_rgba(1.0, 1.0, 1.0, 0.75)
    ctx.stroke()

    ctx.arc(cx, cy, r_center, 0, 2 * math.pi)
    ctx.set_source_rgba(*C_BASE_DARK, 1.0)
    ctx.fill_preserve()
    ctx.set_source_rgba(*C_CYAN, 0.95)
    ctx.stroke()
    ctx.restore()

def glyph_tasks(ctx):
    ctx.save()
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_width(0.06)
    bars = [
        (0.28, 0.50, 0.75, C_CYAN),
        (0.42, 0.34, 0.75, C_VIOLET),
        (0.56, 0.24, 0.75, C_MINT),
        (0.70, 0.40, 0.75, C_CRIMSON),
    ]
    for x, y1, y2, col in bars:
        ctx.set_source_rgba(*col, 0.98)
        ctx.move_to(x, y2)
        ctx.line_to(x, y1)
        ctx.stroke()

    ctx.set_line_width(0.028)
    ctx.set_source_rgba(1.0, 1.0, 1.0, 0.9)
    ctx.move_to(0.28, 0.46)
    ctx.line_to(0.42, 0.30)
    ctx.line_to(0.56, 0.20)
    ctx.line_to(0.70, 0.36)
    ctx.stroke()
    ctx.restore()

def glyph_cockpit(ctx):
    ctx.save()
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)

    ctx.set_line_width(0.03)
    ctx.set_source_rgba(*C_VIOLET, 0.65)
    ctx.arc(0.50, 0.50, 0.30, math.pi * 0.8, math.pi * 2.2)
    ctx.stroke()

    pts = [
        (0.18, 0.52), (0.28, 0.52), (0.34, 0.36),
        (0.40, 0.68), (0.46, 0.23), (0.52, 0.77),
        (0.58, 0.28), (0.64, 0.64), (0.70, 0.52), (0.82, 0.52)
    ]
    ctx.move_to(*pts[0])
    for px, py in pts[1:]:
        ctx.line_to(px, py)
    pat = cairo.LinearGradient(0.18, 0.5, 0.82, 0.5)
    pat.add_color_stop_rgba(0.0, *C_CYAN, 1.0)
    pat.add_color_stop_rgba(0.5, *C_MINT, 1.0)
    pat.add_color_stop_rgba(1.0, *C_CRIMSON, 1.0)
    ctx.set_source(pat)
    ctx.set_line_width(0.042)
    ctx.stroke()
    ctx.restore()

def glyph_hilbert(ctx):
    ctx.save()
    cx, cy, r = 0.50, 0.50, 0.28

    g = cairo.RadialGradient(cx - 0.08, cy - 0.08, 0.02, cx, cy, r)
    g.add_color_stop_rgba(0.0, 0.16, 0.22, 0.42, 0.75)
    g.add_color_stop_rgba(1.0, *C_BASE_DARK, 0.90)
    ctx.set_source(g)
    ctx.arc(cx, cy, r, 0, 2 * math.pi)
    ctx.fill_preserve()
    ctx.set_line_width(0.034)
    ctx.set_source_rgba(*C_CYAN, 0.95)
    ctx.stroke()

    ctx.save()
    ctx.translate(cx, cy)
    ctx.scale(1.0, 0.36)
    ctx.arc(0, 0, r, 0, 2 * math.pi)
    ctx.restore()
    ctx.set_line_width(0.024)
    ctx.set_source_rgba(*C_VIOLET, 0.85)
    ctx.stroke()

    ctx.save()
    ctx.translate(cx, cy)
    ctx.scale(0.38, 1.0)
    ctx.arc(0, 0, r, 0, 2 * math.pi)
    ctx.restore()
    ctx.set_line_width(0.022)
    ctx.set_source_rgba(*C_CYAN, 0.55)
    ctx.stroke()

    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_width(0.042)
    ctx.set_source_rgba(*C_MINT, 1.0)
    vx, vy = cx + 0.17, cy - 0.18
    ctx.move_to(cx, cy)
    ctx.line_to(vx, vy)
    ctx.stroke()

    ctx.arc(vx, vy, 0.045, 0, 2 * math.pi)
    ctx.set_source_rgba(*C_CRIMSON, 1.0)
    ctx.fill_preserve()
    ctx.set_line_width(0.018)
    ctx.set_source_rgba(1.0, 1.0, 1.0, 0.95)
    ctx.stroke()
    ctx.restore()

def glyph_circuit_maker(ctx):
    ctx.save()
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ys = [0.30, 0.50, 0.70]
    ctx.set_line_width(0.028)
    ctx.set_source_rgba(*C_CYAN, 0.75)
    for y in ys:
        ctx.move_to(0.18, y)
        ctx.line_to(0.82, y)
        ctx.stroke()

    draw_round_rect(ctx, 0.25, 0.20, 0.19, 0.20, 0.04)
    ctx.set_source_rgba(*C_VIOLET, 0.95)
    ctx.fill_preserve()
    ctx.set_line_width(0.025)
    ctx.set_source_rgba(1.0, 1.0, 1.0, 0.9)
    ctx.stroke()

    ctx.set_line_width(0.026)
    ctx.move_to(0.31, 0.25); ctx.line_to(0.31, 0.35)
    ctx.move_to(0.38, 0.25); ctx.line_to(0.38, 0.35)
    ctx.move_to(0.31, 0.30); ctx.line_to(0.38, 0.30)
    ctx.stroke()

    cx = 0.57
    ctx.set_line_width(0.032)
    ctx.set_source_rgba(*C_MINT, 0.98)
    ctx.move_to(cx, ys[0])
    ctx.line_to(cx, ys[1])
    ctx.stroke()

    ctx.arc(cx, ys[0], 0.038, 0, 2 * math.pi)
    ctx.fill()

    ctx.arc(cx, ys[1], 0.068, 0, 2 * math.pi)
    ctx.set_source_rgba(*C_BASE_DARK, 0.95)
    ctx.fill_preserve()
    ctx.set_source_rgba(*C_MINT, 1.0)
    ctx.stroke()
    ctx.move_to(cx - 0.045, ys[1]); ctx.line_to(cx + 0.045, ys[1])
    ctx.move_to(cx, ys[1] - 0.045); ctx.line_to(cx, ys[1] + 0.045)
    ctx.stroke()

    draw_round_rect(ctx, 0.62, 0.60, 0.16, 0.20, 0.04)
    ctx.set_source_rgba(*C_CYAN, 0.90)
    ctx.fill()
    ctx.set_source_rgba(*C_BASE_DARK, 0.95)
    ctx.set_line_width(0.024)
    ctx.arc(0.70, 0.73, 0.05, math.pi, 2 * math.pi)
    ctx.stroke()
    ctx.move_to(0.70, 0.73); ctx.line_to(0.74, 0.65); ctx.stroke()
    ctx.restore()

def glyph_qec(ctx):
    ctx.save()
    plaquettes = [
        (0.50, 0.32, C_CYAN),
        (0.32, 0.50, C_VIOLET),
        (0.68, 0.50, C_VIOLET),
        (0.50, 0.68, C_CYAN),
    ]
    d = 0.15
    for px, py, col in plaquettes:
        ctx.move_to(px, py - d)
        ctx.line_to(px + d, py)
        ctx.line_to(px, py + d)
        ctx.line_to(px - d, py)
        ctx.close_path()
        ctx.set_source_rgba(*col, 0.42)
        ctx.fill_preserve()
        ctx.set_line_width(0.025)
        ctx.set_source_rgba(*col, 0.95)
        ctx.stroke()

    ctx.set_line_width(0.036)
    ctx.set_source_rgba(*C_MINT, 1.0)
    ctx.move_to(0.32, 0.50)
    ctx.line_to(0.68, 0.50)
    ctx.stroke()

    for nx, ny, col in [(0.32, 0.50, C_CRIMSON), (0.68, 0.50, C_CRIMSON), (0.50, 0.50, C_MINT)]:
        ctx.arc(nx, ny, 0.042, 0, 2 * math.pi)
        ctx.set_source_rgba(*col, 1.0)
        ctx.fill()
    ctx.restore()

def glyph_vqe_chem(ctx):
    ctx.save()
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    atoms = [(0.32, 0.36, C_CYAN, 0.065), (0.68, 0.36, C_VIOLET, 0.065), (0.50, 0.66, C_MINT, 0.075)]
    ctx.set_line_width(0.022)
    ctx.set_source_rgba(*C_CYAN, 0.45)
    ctx.arc(0.50, 0.48, 0.25, 0, 2 * math.pi)
    ctx.stroke()

    ctx.set_line_width(0.038)
    ctx.set_source_rgba(0.9, 0.9, 1.0, 0.85)
    ctx.move_to(0.32, 0.36); ctx.line_to(0.68, 0.36)
    ctx.line_to(0.50, 0.66); ctx.close_path()
    ctx.stroke()

    for ax, ay, col, rad in atoms:
        ctx.arc(ax, ay, rad, 0, 2 * math.pi)
        ctx.set_source_rgba(*col, 1.0)
        ctx.fill_preserve()
        ctx.set_line_width(0.02)
        ctx.set_source_rgba(1.0, 1.0, 1.0, 0.9)
        ctx.stroke()
    ctx.restore()

def glyph_debugger(ctx):
    ctx.save()
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.arc(0.50, 0.50, 0.27, 0, 2 * math.pi)
    ctx.set_line_width(0.032)
    ctx.set_source_rgba(*C_CRIMSON, 0.90)
    ctx.stroke()

    for bx, bh, col in [(0.36, 0.20, C_CYAN), (0.46, 0.32, C_MINT), (0.56, 0.14, C_VIOLET), (0.64, 0.26, C_CYAN)]:
        draw_round_rect(ctx, bx - 0.03, 0.66 - bh, 0.06, bh, 0.02)
        ctx.set_source_rgba(*col, 0.95)
        ctx.fill()

    ctx.arc(0.71, 0.29, 0.048, 0, 2 * math.pi)
    ctx.set_source_rgba(*C_CRIMSON, 1.0)
    ctx.fill()
    ctx.restore()

def glyph_qpu_3d(ctx):
    ctx.save()
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    stages = [
        (0.24, 0.52, C_AMBER),
        (0.37, 0.42, C_CYAN),
        (0.50, 0.32, C_VIOLET),
        (0.63, 0.22, C_MINT),
    ]
    ctx.set_line_width(0.026)
    ctx.set_source_rgba(0.85, 0.88, 1.0, 0.75)
    for rx in [0.40, 0.50, 0.60]:
        ctx.move_to(rx, 0.24)
        ctx.line_to(rx, 0.65)
        ctx.stroke()

    for sy, sw, col in stages:
        draw_round_rect(ctx, 0.50 - sw / 2, sy - 0.028, sw, 0.056, 0.022)
        ctx.set_source_rgba(*col, 0.95)
        ctx.fill()

    draw_round_rect(ctx, 0.43, 0.65, 0.14, 0.14, 0.03)
    ctx.set_source_rgba(*C_CYAN, 1.0)
    ctx.fill_preserve()
    ctx.set_line_width(0.02)
    ctx.set_source_rgba(1.0, 1.0, 1.0, 0.9)
    ctx.stroke()
    ctx.restore()

def glyph_pqc_audit(ctx):
    ctx.save()
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    ctx.move_to(0.50, 0.19)
    ctx.line_to(0.76, 0.29)
    ctx.line_to(0.73, 0.55)
    ctx.curve_to(0.70, 0.69, 0.58, 0.78, 0.50, 0.82)
    ctx.curve_to(0.42, 0.78, 0.30, 0.69, 0.27, 0.55)
    ctx.line_to(0.24, 0.29)
    ctx.close_path()

    pat = cairo.LinearGradient(0.24, 0.19, 0.76, 0.82)
    pat.add_color_stop_rgba(0.0, *C_VIOLET, 0.85)
    pat.add_color_stop_rgba(1.0, 0.06, 0.18, 0.35, 0.95)
    ctx.set_source(pat)
    ctx.fill_preserve()
    ctx.set_line_width(0.034)
    ctx.set_source_rgba(*C_CYAN, 0.98)
    ctx.stroke()

    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_width(0.048)
    ctx.set_source_rgba(*C_MINT, 1.0)
    ctx.move_to(0.38, 0.51)
    ctx.line_to(0.47, 0.60)
    ctx.line_to(0.63, 0.41)
    ctx.stroke()
    ctx.restore()

def glyph_editor(ctx):
    ctx.save()
    draw_round_rect(ctx, 0.24, 0.20, 0.52, 0.60, 0.06)
    ctx.set_source_rgba(0.08, 0.06, 0.18, 0.92)
    ctx.fill_preserve()
    ctx.set_line_width(0.032)
    ctx.set_source_rgba(*C_VIOLET, 0.95)
    ctx.stroke()

    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_width(0.032)
    for ly, lx2, col in [
        (0.33, 0.56, C_CYAN),
        (0.44, 0.68, C_MINT),
        (0.55, 0.52, C_CRIMSON),
        (0.66, 0.64, C_VIOLET),
    ]:
        ctx.set_source_rgba(*col, 0.95)
        ctx.move_to(0.32, ly)
        ctx.line_to(lx2, ly)
        ctx.stroke()
    ctx.restore()

def glyph_browser(ctx):
    ctx.save()
    ctx.arc(0.50, 0.50, 0.27, 0, 2 * math.pi)
    ctx.set_source_rgba(0.06, 0.12, 0.28, 0.85)
    ctx.fill_preserve()
    ctx.set_line_width(0.036)
    ctx.set_source_rgba(*C_CYAN, 0.98)
    ctx.stroke()

    ctx.set_line_width(0.026)
    ctx.set_source_rgba(*C_VIOLET, 0.85)
    ctx.move_to(0.23, 0.50); ctx.line_to(0.77, 0.50); ctx.stroke()

    ctx.save()
    ctx.translate(0.50, 0.50)
    ctx.scale(0.50, 1.0)
    ctx.arc(0, 0, 0.27, 0, 2 * math.pi)
    ctx.restore()
    ctx.set_source_rgba(*C_CYAN, 0.78)
    ctx.stroke()

    ctx.arc(0.50, 0.50, 0.042, 0, 2 * math.pi)
    ctx.set_source_rgba(*C_MINT, 1.0)
    ctx.fill()
    ctx.restore()

def glyph_ports(ctx):
    ctx.save()
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    ctx.move_to(0.23, 0.28)
    ctx.line_to(0.77, 0.28)
    ctx.line_to(0.77, 0.63)
    ctx.line_to(0.64, 0.63)
    ctx.line_to(0.64, 0.74)
    ctx.line_to(0.36, 0.74)
    ctx.line_to(0.36, 0.63)
    ctx.line_to(0.23, 0.63)
    ctx.close_path()
    ctx.set_source_rgba(0.07, 0.12, 0.26, 0.9)
    ctx.fill_preserve()
    ctx.set_line_width(0.035)
    ctx.set_source_rgba(*C_CYAN, 0.95)
    ctx.stroke()

    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_width(0.03)
    ctx.set_source_rgba(*C_MINT, 0.95)
    for px in [0.37, 0.46, 0.54, 0.63]:
        ctx.move_to(px, 0.35); ctx.line_to(px, 0.48); ctx.stroke()
    ctx.restore()

def glyph_drive(ctx):
    ctx.save()
    draw_round_rect(ctx, 0.22, 0.31, 0.56, 0.38, 0.07)
    ctx.set_source_rgba(0.09, 0.14, 0.30, 0.92)
    ctx.fill_preserve()
    ctx.set_line_width(0.035)
    ctx.set_source_rgba(*C_CYAN, 0.95)
    ctx.stroke()

    ctx.arc(0.68, 0.50, 0.035, 0, 2 * math.pi)
    ctx.set_source_rgba(*C_MINT, 1.0)
    ctx.fill()

    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_width(0.024)
    ctx.set_source_rgba(*C_CYAN, 0.75)
    ctx.move_to(0.29, 0.45); ctx.line_to(0.57, 0.45); ctx.stroke()
    ctx.move_to(0.29, 0.55); ctx.line_to(0.57, 0.55); ctx.stroke()
    ctx.restore()

def glyph_disks(ctx):
    glyph_drive(ctx)
    ctx.save()
    ctx.arc(0.42, 0.50, 0.08, 0, 2 * math.pi)
    ctx.set_line_width(0.026)
    ctx.set_source_rgba(*C_VIOLET, 0.95)
    ctx.stroke()
    ctx.restore()

def glyph_installer(ctx):
    ctx.save()
    draw_round_rect(ctx, 0.22, 0.56, 0.56, 0.20, 0.05)
    ctx.set_source_rgba(0.10, 0.14, 0.30, 0.95)
    ctx.fill_preserve()
    ctx.set_line_width(0.032)
    ctx.set_source_rgba(*C_CYAN, 0.95)
    ctx.stroke()

    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    ctx.set_line_width(0.052)
    ctx.set_source_rgba(*C_MINT, 1.0)
    ctx.move_to(0.50, 0.20)
    ctx.line_to(0.50, 0.50)
    ctx.stroke()
    ctx.move_to(0.36, 0.38)
    ctx.line_to(0.50, 0.51)
    ctx.line_to(0.64, 0.38)
    ctx.stroke()
    ctx.restore()

def glyph_screenshot(ctx):
    ctx.save()
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    ctx.set_line_width(0.048)
    ctx.set_source_rgba(*C_CYAN, 1.0)
    for (x1, y1), (x2, y2), (x3, y3) in [
        ((0.20, 0.35), (0.20, 0.20), (0.35, 0.20)),
        ((0.65, 0.20), (0.80, 0.20), (0.80, 0.35)),
        ((0.80, 0.65), (0.80, 0.80), (0.65, 0.80)),
        ((0.35, 0.80), (0.20, 0.80), (0.20, 0.65)),
    ]:
        ctx.move_to(x1, y1); ctx.line_to(x2, y2); ctx.line_to(x3, y3); ctx.stroke()

    ctx.arc(0.50, 0.50, 0.11, 0, 2 * math.pi)
    ctx.set_line_width(0.032)
    ctx.set_source_rgba(*C_CRIMSON, 0.95)
    ctx.stroke()
    ctx.arc(0.50, 0.50, 0.03, 0, 2 * math.pi)
    ctx.set_source_rgba(1.0, 1.0, 1.0, 1.0)
    ctx.fill()
    ctx.restore()

def glyph_wallpaper(ctx):
    ctx.save()
    draw_round_rect(ctx, 0.18, 0.24, 0.64, 0.52, 0.07)
    ctx.set_source_rgba(0.08, 0.06, 0.20, 0.94)
    ctx.fill_preserve()
    ctx.set_line_width(0.034)
    ctx.set_source_rgba(*C_VIOLET, 0.95)
    ctx.stroke()

    ctx.arc(0.63, 0.38, 0.065, 0, 2 * math.pi)
    ctx.set_source_rgba(*C_CYAN, 0.98)
    ctx.fill()

    ctx.move_to(0.20, 0.73)
    ctx.line_to(0.38, 0.46)
    ctx.line_to(0.53, 0.62)
    ctx.line_to(0.64, 0.52)
    ctx.line_to(0.80, 0.73)
    ctx.close_path()
    pat = cairo.LinearGradient(0.20, 0.46, 0.80, 0.73)
    pat.add_color_stop_rgba(0.0, *C_VIOLET, 0.90)
    pat.add_color_stop_rgba(1.0, *C_MINT, 0.85)
    ctx.set_source(pat)
    ctx.fill()
    ctx.restore()

def glyph_control_center(ctx):
    ctx.save()
    draw_round_rect(ctx, 0.20, 0.24, 0.60, 0.21, 0.105)
    ctx.set_source_rgba(*C_CYAN, 0.32)
    ctx.fill_preserve()
    ctx.set_line_width(0.034)
    ctx.set_source_rgba(*C_CYAN, 0.98)
    ctx.stroke()
    ctx.arc(0.68, 0.345, 0.068, 0, 2 * math.pi)
    ctx.set_source_rgba(*C_MINT, 1.0)
    ctx.fill()

    draw_round_rect(ctx, 0.20, 0.55, 0.60, 0.21, 0.105)
    ctx.set_source_rgba(*C_VIOLET, 0.30)
    ctx.fill_preserve()
    ctx.set_line_width(0.034)
    ctx.set_source_rgba(*C_VIOLET, 0.98)
    ctx.stroke()
    ctx.arc(0.32, 0.655, 0.068, 0, 2 * math.pi)
    ctx.set_source_rgba(1.0, 1.0, 1.0, 0.95)
    ctx.fill()
    ctx.restore()

def glyph_wifi(ctx):
    ctx.save()
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    cx, cy = 0.50, 0.72
    ctx.arc(cx, cy, 0.05, 0, 2 * math.pi)
    ctx.set_source_rgba(*C_CYAN, 1.0)
    ctx.fill()

    for r, col in [(0.16, C_CYAN), (0.28, C_VIOLET), (0.40, C_MINT)]:
        ctx.set_line_width(0.05)
        ctx.set_source_rgba(*col, 0.98)
        ctx.arc(cx, cy, r, -math.pi * 0.75, -math.pi * 0.25)
        ctx.stroke()
    ctx.restore()

def glyph_bluetooth(ctx):
    ctx.save()
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    ctx.set_line_width(0.065)
    pat = cairo.LinearGradient(0.28, 0.16, 0.72, 0.84)
    pat.add_color_stop_rgba(0.0, *C_CYAN, 1.0)
    pat.add_color_stop_rgba(1.0, *C_VIOLET, 1.0)
    ctx.set_source(pat)
    ctx.move_to(0.28, 0.67)
    ctx.line_to(0.72, 0.33)
    ctx.line_to(0.50, 0.16)
    ctx.line_to(0.50, 0.84)
    ctx.line_to(0.72, 0.67)
    ctx.line_to(0.28, 0.33)
    ctx.stroke()
    ctx.restore()

def glyph_audio(ctx):
    ctx.save()
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    ctx.move_to(0.22, 0.41)
    ctx.line_to(0.34, 0.41)
    ctx.line_to(0.49, 0.27)
    ctx.line_to(0.49, 0.73)
    ctx.line_to(0.34, 0.59)
    ctx.line_to(0.22, 0.59)
    ctx.close_path()
    ctx.set_source_rgba(*C_CYAN, 0.95)
    ctx.fill()

    ctx.set_line_width(0.045)
    ctx.set_source_rgba(*C_MINT, 0.95)
    ctx.arc(0.48, 0.50, 0.13, -math.pi / 4, math.pi / 4)
    ctx.stroke()
    ctx.set_source_rgba(*C_VIOLET, 0.95)
    ctx.arc(0.48, 0.50, 0.24, -math.pi / 4, math.pi / 4)
    ctx.stroke()
    ctx.restore()

def glyph_audio_mute(ctx):
    glyph_audio(ctx)
    ctx.save()
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_width(0.055)
    ctx.set_source_rgba(*C_CRIMSON, 1.0)
    ctx.move_to(0.24, 0.76)
    ctx.line_to(0.76, 0.24)
    ctx.stroke()
    ctx.restore()

def glyph_battery(ctx):
    ctx.save()
    draw_round_rect(ctx, 0.18, 0.33, 0.56, 0.34, 0.06)
    ctx.set_source_rgba(0.08, 0.07, 0.18, 0.9)
    ctx.fill_preserve()
    ctx.set_line_width(0.04)
    ctx.set_source_rgba(*C_CYAN, 0.95)
    ctx.stroke()

    draw_round_rect(ctx, 0.75, 0.42, 0.06, 0.16, 0.02)
    ctx.set_source_rgba(*C_CYAN, 0.95)
    ctx.fill()

    for bx in [0.24, 0.39, 0.54]:
        draw_round_rect(ctx, bx, 0.40, 0.11, 0.20, 0.02)
        ctx.set_source_rgba(*C_MINT, 1.0)
        ctx.fill()
    ctx.restore()

def glyph_battery_charging(ctx):
    glyph_battery(ctx)
    ctx.save()
    ctx.move_to(0.52, 0.22)
    ctx.line_to(0.41, 0.50)
    ctx.line_to(0.50, 0.50)
    ctx.line_to(0.44, 0.78)
    ctx.line_to(0.59, 0.46)
    ctx.line_to(0.50, 0.46)
    ctx.close_path()
    ctx.set_source_rgba(1.0, 1.0, 1.0, 1.0)
    ctx.fill()
    ctx.restore()

def glyph_nightlight(ctx):
    ctx.save()
    ctx.set_source_rgba(*C_AMBER, 0.98)
    ctx.new_sub_path()
    ctx.arc(0.47, 0.50, 0.27, 0.55, 5.25)
    ctx.arc_negative(0.59, 0.47, 0.21, 5.05, 0.75)
    ctx.close_path()
    ctx.fill()

    ctx.set_source_rgba(*C_CYAN, 0.95)
    for sx, sy, sr in [(0.72, 0.28, 0.032), (0.79, 0.46, 0.025)]:
        ctx.arc(sx, sy, sr, 0, 2 * math.pi)
        ctx.fill()
    ctx.restore()

def glyph_notifications(ctx):
    ctx.save()
    ctx.set_line_width(0.042)
    ctx.set_source_rgba(*C_CYAN, 0.98)
    ctx.arc(0.50, 0.24, 0.048, 0, 2 * math.pi)
    ctx.stroke()

    ctx.new_sub_path()
    ctx.move_to(0.31, 0.62)
    ctx.curve_to(0.31, 0.35, 0.69, 0.35, 0.69, 0.62)
    ctx.line_to(0.75, 0.69)
    ctx.line_to(0.25, 0.69)
    ctx.close_path()
    ctx.set_source_rgba(*C_VIOLET, 0.50)
    ctx.fill_preserve()
    ctx.set_source_rgba(*C_CYAN, 0.98)
    ctx.stroke()

    ctx.arc(0.50, 0.74, 0.06, 0, math.pi)
    ctx.set_source_rgba(*C_MINT, 1.0)
    ctx.fill()

    ctx.arc(0.69, 0.30, 0.068, 0, 2 * math.pi)
    ctx.set_source_rgba(*C_CRIMSON, 1.0)
    ctx.fill()
    ctx.restore()

def glyph_lock(ctx):
    ctx.save()
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_width(0.055)
    ctx.set_source_rgba(*C_CYAN, 0.95)
    ctx.move_to(0.36, 0.44)
    ctx.line_to(0.36, 0.33)
    ctx.arc(0.50, 0.33, 0.14, math.pi, 0)
    ctx.line_to(0.64, 0.44)
    ctx.stroke()

    draw_round_rect(ctx, 0.27, 0.43, 0.46, 0.36, 0.08)
    pat = cairo.LinearGradient(0.27, 0.43, 0.73, 0.79)
    pat.add_color_stop_rgba(0.0, *C_VIOLET, 0.98)
    pat.add_color_stop_rgba(1.0, *C_CYAN, 0.92)
    ctx.set_source(pat)
    ctx.fill()

    ctx.arc(0.50, 0.57, 0.045, 0, 2 * math.pi)
    ctx.set_source_rgba(*C_BASE_DARK, 1.0)
    ctx.fill()
    ctx.rectangle(0.475, 0.57, 0.05, 0.11)
    ctx.fill()
    ctx.restore()

def glyph_power(ctx):
    ctx.save()
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_width(0.058)
    ctx.set_source_rgba(*C_CRIMSON, 1.0)
    ctx.arc(0.50, 0.53, 0.24, -math.pi / 2 + 0.52, 3 * math.pi / 2 - 0.52)
    ctx.stroke()
    ctx.move_to(0.50, 0.22)
    ctx.line_to(0.50, 0.52)
    ctx.stroke()
    ctx.restore()

def glyph_home(ctx):
    ctx.save()
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    ctx.move_to(0.50, 0.22)
    ctx.line_to(0.22, 0.46)
    ctx.line_to(0.29, 0.46)
    ctx.line_to(0.29, 0.75)
    ctx.line_to(0.71, 0.75)
    ctx.line_to(0.71, 0.46)
    ctx.line_to(0.78, 0.46)
    ctx.close_path()
    pat = cairo.LinearGradient(0.50, 0.22, 0.50, 0.75)
    pat.add_color_stop_rgba(0.0, *C_VIOLET, 0.95)
    pat.add_color_stop_rgba(1.0, *C_CYAN, 0.90)
    ctx.set_source(pat)
    ctx.fill_preserve()
    ctx.set_line_width(0.034)
    ctx.set_source_rgba(*C_CYAN, 1.0)
    ctx.stroke()

    draw_round_rect(ctx, 0.43, 0.54, 0.14, 0.21, 0.02)
    ctx.set_source_rgba(*C_BASE_DARK, 0.95)
    ctx.fill()
    ctx.restore()

def glyph_trash(ctx):
    ctx.save()
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_width(0.038)
    ctx.set_source_rgba(*C_CRIMSON, 0.95)
    ctx.move_to(0.29, 0.33); ctx.line_to(0.71, 0.33); ctx.stroke()
    ctx.move_to(0.43, 0.27); ctx.line_to(0.57, 0.27); ctx.stroke()

    ctx.move_to(0.33, 0.36)
    ctx.line_to(0.37, 0.74)
    ctx.line_to(0.63, 0.74)
    ctx.line_to(0.67, 0.36)
    ctx.close_path()
    ctx.set_source_rgba(0.25, 0.10, 0.22, 0.9)
    ctx.fill_preserve()
    ctx.set_source_rgba(*C_CRIMSON, 0.9)
    ctx.stroke()

    ctx.set_line_width(0.024)
    for fx in [0.43, 0.50, 0.57]:
        ctx.move_to(fx, 0.44); ctx.line_to(fx, 0.66); ctx.stroke()
    ctx.restore()

def glyph_screensaver(ctx):
    ctx.save()
    draw_round_rect(ctx, 0.20, 0.24, 0.60, 0.42, 0.06)
    ctx.set_source_rgba(0.07, 0.06, 0.18, 0.94)
    ctx.fill_preserve()
    ctx.set_line_width(0.034)
    ctx.set_source_rgba(*C_CYAN, 0.95)
    ctx.stroke()

    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_width(0.038)
    ctx.move_to(0.50, 0.66); ctx.line_to(0.50, 0.76); ctx.stroke()
    ctx.move_to(0.37, 0.76); ctx.line_to(0.63, 0.76); ctx.stroke()

    ctx.set_line_width(0.032)
    ctx.set_source_rgba(*C_MINT, 0.98)
    pts = [(0.26, 0.45), (0.37, 0.45), (0.43, 0.33), (0.50, 0.57), (0.57, 0.36), (0.63, 0.45), (0.74, 0.45)]
    ctx.move_to(*pts[0])
    for px, py in pts[1:]:
        ctx.line_to(px, py)
    ctx.stroke()
    ctx.restore()

def glyph_cryo_lab(ctx):
    ctx.save()
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    draw_round_rect(ctx, 0.23, 0.18, 0.54, 0.64, 0.14)
    ctx.set_source_rgba(0.06, 0.05, 0.14, 0.92)
    ctx.fill_preserve()
    ctx.set_line_width(0.032)
    ctx.set_source_rgba(*C_CYAN, 0.90)
    ctx.stroke()

    stages = [(0.28, 0.40), (0.38, 0.34), (0.48, 0.28), (0.58, 0.22)]
    ctx.set_line_width(0.028)
    for sy, sw in stages:
        ctx.set_source_rgba(*C_AMBER, 0.95)
        ctx.move_to(0.50 - sw / 2, sy)
        ctx.line_to(0.50 + sw / 2, sy)
        ctx.stroke()

    ctx.set_line_width(0.018)
    ctx.set_source_rgba(*C_VIOLET, 0.85)
    for sx in (0.42, 0.50, 0.58):
        ctx.move_to(sx, 0.26)
        ctx.line_to(sx, 0.64)
        ctx.stroke()

    ctx.arc(0.50, 0.69, 0.065, 0, 2 * math.pi)
    ctx.set_source_rgba(*C_MINT, 1.0)
    ctx.fill()
    ctx.restore()

ICONS_SPEC = [
    ("omniq-logo",       glyph_omniq_logo,      "cyan",    False),
    ("cryo_lab",         glyph_cryo_lab,        "mint",    True),
    ("apps_grid",        glyph_apps_grid,       "cyan",    False),
    ("spotlight",        glyph_spotlight,       "cyan",    True),
    ("terminal",         glyph_terminal,        "violet",  True),
    ("files",            glyph_files,           "cyan",    True),
    ("settings",         glyph_settings,        "violet",  True),
    ("tasks",            glyph_tasks,           "cyan",    True),
    ("cockpit",          glyph_cockpit,         "cyan",    True),
    ("hilbert",          glyph_hilbert,         "cyan",    True),
    ("circuit_maker",    glyph_circuit_maker,   "violet",  True),
    ("qec",              glyph_qec,             "mint",    True),
    ("vqe_chem",         glyph_vqe_chem,        "cyan",    True),
    ("debugger",         glyph_debugger,        "crimson", True),
    ("qpu_3d",           glyph_qpu_3d,          "cyan",    True),
    ("pqc_audit",        glyph_pqc_audit,       "mint",    True),
    ("editor",           glyph_editor,          "violet",  True),
    ("edit",             glyph_editor,          "violet",  True),
    ("browser",          glyph_browser,         "cyan",    True),
    ("ports",            glyph_ports,           "cyan",    True),
    ("disks",            glyph_disks,           "cyan",    True),
    ("installer",        glyph_installer,       "mint",    True),
    ("screenshot",       glyph_screenshot,      "cyan",    True),
    ("wallpaper",        glyph_wallpaper,       "violet",  True),
    ("screensaver",      glyph_screensaver,     "cyan",    True),
    ("control_center",   glyph_control_center,  "cyan",    False),
    ("wifi",             glyph_wifi,            "cyan",    False),
    ("bluetooth",        glyph_bluetooth,       "violet",  False),
    ("audio",            glyph_audio,           "cyan",    False),
    ("audio_mute",       glyph_audio_mute,      "crimson", False),
    ("battery",          glyph_battery,         "mint",    False),
    ("battery_charging", glyph_battery_charging,"cyan",    False),
    ("nightlight",       glyph_nightlight,      "cyan",    False),
    ("notifications",    glyph_notifications,   "cyan",    False),
    ("lock",             glyph_lock,            "violet",  False),
    ("power",            glyph_power,           "crimson", False),
    ("home",             glyph_home,            "violet",  True),
    ("drive",            glyph_drive,           "cyan",    True),
    ("circuits_folder",  glyph_circuits_folder, "mint",    True),
    ("trash",            glyph_trash,           "crimson", True),
]

def render_icon_png(renderer, accent, use_tile, size, out_path):
    scale = 4 if size <= 128 else 2
    hi = size * scale
    hi_surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, hi, hi)
    hi_ctx = cairo.Context(hi_surf)
    hi_ctx.set_antialias(cairo.ANTIALIAS_BEST)
    hi_ctx.scale(hi, hi)

    if use_tile and size >= 32:
        draw_glass_squircle(hi_ctx, size, accent=accent)
        hi_ctx.save()
        hi_ctx.translate(0.08, 0.08)
        hi_ctx.scale(0.84, 0.84)
        renderer(hi_ctx)
        hi_ctx.restore()
    else:
        renderer(hi_ctx)

    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, size, size)
    ctx = cairo.Context(surf)
    ctx.scale(1.0 / scale, 1.0 / scale)
    ctx.set_source_surface(hi_surf, 0, 0)
    ctx.get_source().set_filter(cairo.FILTER_BEST)
    ctx.paint()
    surf.write_to_png(out_path)

def generate_all_icons(icons_dir):
    os.makedirs(icons_dir, exist_ok=True)
    for s in RESOLUTIONS:
        s_x_dir = os.path.join(icons_dir, f"{s}x{s}")
        os.makedirs(s_x_dir, exist_ok=True)
        s_plain = os.path.join(icons_dir, str(s))
        if not os.path.exists(s_plain):
            try:
                os.symlink(f"{s}x{s}", s_plain)
            except Exception:
                os.makedirs(s_plain, exist_ok=True)

    for name, renderer, accent, use_tile in ICONS_SPEC:
        for s in RESOLUTIONS:
            suffixed_main = os.path.join(icons_dir, f"{name}_{s}.png")
            render_icon_png(renderer, accent, use_tile, s, suffixed_main)

            s_x_dir = os.path.join(icons_dir, f"{s}x{s}")
            shutil.copyfile(suffixed_main, os.path.join(s_x_dir, f"{name}.png"))
            shutil.copyfile(suffixed_main, os.path.join(s_x_dir, f"{name}_{s}.png"))

            s_plain = os.path.join(icons_dir, str(s))
            if os.path.isdir(s_plain) and not os.path.islink(s_plain):
                shutil.copyfile(suffixed_main, os.path.join(s_plain, f"{name}.png"))
                shutil.copyfile(suffixed_main, os.path.join(s_plain, f"{name}_{s}.png"))

            if s == 24:
                shutil.copyfile(suffixed_main, os.path.join(icons_dir, f"{name}.png"))

W, H = 1920, 1080

def _draw_base_canvas(ctx, c_inner, c_mid, c_outer, grid_alpha=0.10):
    pat = cairo.RadialGradient(W * 0.5, H * 0.46, 60, W * 0.5, H * 0.5, W * 0.72)
    pat.add_color_stop_rgb(0.0, *c_inner)
    pat.add_color_stop_rgb(0.55, *c_mid)
    pat.add_color_stop_rgb(1.0, *c_outer)
    ctx.set_source(pat)
    ctx.paint()

    ctx.set_line_width(1.0)
    ctx.set_source_rgba(*C_CYAN, grid_alpha)
    step = 80
    for x in range(0, W + 1, step):
        ctx.move_to(x, 0); ctx.line_to(x, H); ctx.stroke()
    for y in range(0, H + 1, step):
        ctx.move_to(0, y); ctx.line_to(W, y); ctx.stroke()

    ctx.set_source_rgba(*C_VIOLET, grid_alpha * 2.2)
    for x in range(step * 2, W, step * 3):
        for y in range(step * 2, H, step * 3):
            ctx.move_to(x - 6, y); ctx.line_to(x + 6, y)
            ctx.move_to(x, y - 6); ctx.line_to(x, y + 6)
            ctx.stroke()

def _stamp_corner_watermark(pil_img, subtitle):
    draw = ImageDraw.Draw(pil_img)
    font_sub = ImageFont.load_default()
    for fp in [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-B.ttf",
    ]:
        if os.path.exists(fp):
            try:
                font_sub = ImageFont.truetype(fp, 15)
                break
            except Exception:
                pass
    stamp = f"OMNIQ-OS 24.04-RT // {subtitle} // TRI-COMPUTE KERNEL"
    draw.text((W - 28, H - 22), stamp, fill=(165, 175, 210, 175), font=font_sub, anchor="rd")

def wp_flagship(ctx):
    _draw_base_canvas(ctx, (0.11, 0.08, 0.22), C_SURF_DARK, C_BASE_DARK, grid_alpha=0.07)
    cx, cy = W * 0.5, H * 0.46

    for rad, col, a in [(440, C_VIOLET, 0.16), (290, C_CYAN, 0.14), (170, C_MINT, 0.10)]:
        g = cairo.RadialGradient(cx, cy, 10, cx, cy, rad)
        g.add_color_stop_rgba(0.0, *col, a)
        g.add_color_stop_rgba(1.0, *col, 0.0)
        ctx.set_source(g)
        ctx.arc(cx, cy, rad, 0, 2 * math.pi)
        ctx.fill()

    for r, col, lw, dash in [
        (340, C_VIOLET, 1.4, [10, 8]),
        (250, C_CYAN, 1.8, []),
        (175, C_MINT, 1.3, [6, 6]),
    ]:
        ctx.save()
        if dash:
            ctx.set_dash(dash, 0)
        ctx.set_line_width(lw)
        ctx.set_source_rgba(*col, 0.45)
        ctx.arc(cx, cy, r, 0, 2 * math.pi)
        ctx.stroke()
        ctx.restore()

    ctx.save()
    ctx.translate(cx - 130, cy - 130)
    ctx.scale(260, 260)
    glyph_omniq_logo(ctx)
    ctx.restore()

def wp_cryo(ctx):
    _draw_base_canvas(ctx, (0.06, 0.13, 0.22), (0.05, 0.07, 0.15), C_BASE_DARK, grid_alpha=0.06)
    cx = W * 0.5

    g = cairo.RadialGradient(cx, H * 0.52, 30, cx, H * 0.52, 520)
    g.add_color_stop_rgba(0.0, *C_CYAN, 0.20)
    g.add_color_stop_rgba(0.55, *C_VIOLET, 0.10)
    g.add_color_stop_rgba(1.0, 0, 0, 0, 0)
    ctx.set_source(g)
    ctx.arc(cx, H * 0.52, 520, 0, 2 * math.pi)
    ctx.fill()

    stages = [
        (160, 760, C_AMBER,  "50K Flange"),
        (310, 620, C_AMBER,  "4K Pulse Tube"),
        (460, 480, C_CYAN,   "800mK Still"),
        (610, 350, C_VIOLET, "100mK Cold Plate"),
        (760, 230, C_MINT,   "14.2mK Mixing Chamber"),
    ]
    ctx.set_line_width(2.2)
    for offset in [-220, -130, -55, 55, 130, 220]:
        ctx.set_source_rgba(*C_CYAN, 0.26)
        ctx.move_to(cx + offset, 160)
        ctx.line_to(cx + offset * 0.32, 760)
        ctx.stroke()

    ctx.set_line_width(1.8)
    for sy, sw, col, _ in stages[:-1]:
        for side in (-1, 1):
            lx = cx + side * (sw * 0.28)
            ctx.set_source_rgba(*col, 0.55)
            ctx.arc(lx, sy + 72, 26, 0, 2 * math.pi)
            ctx.stroke()

    for sy, sw, col, _ in stages:
        draw_round_rect(ctx, cx - sw / 2, sy - 10, sw, 20, 8)
        ctx.set_source_rgba(*col, 0.78)
        ctx.fill_preserve()
        ctx.set_line_width(1.5)
        ctx.set_source_rgba(1, 1, 1, 0.45)
        ctx.stroke()

    draw_round_rect(ctx, cx - 54, 780, 108, 125, 16)
    ctx.set_source_rgba(*C_CYAN, 0.35)
    ctx.fill_preserve()
    ctx.set_line_width(2.5)
    ctx.set_source_rgba(*C_MINT, 0.92)
    ctx.stroke()

def wp_hilbert(ctx):
    _draw_base_canvas(ctx, (0.12, 0.07, 0.24), C_SURF_DARK, C_BASE_DARK, grid_alpha=0.06)
    cx, cy = W * 0.5, H * 0.50

    for phase, col, amp in [(0.0, C_CYAN, 95), (1.3, C_VIOLET, 75), (2.6, C_MINT, 55)]:
        ctx.set_line_width(2.0)
        ctx.set_source_rgba(*col, 0.35)
        for x in range(0, W + 8, 8):
            env = math.exp(-((x - cx) / 560.0) ** 2)
            y = cy + math.sin(x * 0.014 + phase) * amp * env
            if x == 0:
                ctx.move_to(x, y)
            else:
                ctx.line_to(x, y)
        ctx.stroke()

    R = 290
    ctx.arc(cx, cy, R, 0, 2 * math.pi)
    ctx.set_source_rgba(0.06, 0.09, 0.20, 0.55)
    ctx.fill_preserve()
    ctx.set_line_width(2.4)
    ctx.set_source_rgba(*C_CYAN, 0.80)
    ctx.stroke()

    for sx, sy, col in [(1.0, 0.30, C_VIOLET), (1.0, 0.62, C_CYAN), (0.34, 1.0, C_VIOLET), (0.68, 1.0, C_CYAN)]:
        ctx.save()
        ctx.translate(cx, cy)
        ctx.scale(sx, sy)
        ctx.arc(0, 0, R, 0, 2 * math.pi)
        ctx.restore()
        ctx.set_line_width(1.3)
        ctx.set_source_rgba(*col, 0.42)
        ctx.stroke()

    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_width(4.5)
    ctx.set_source_rgba(*C_MINT, 0.95)
    vx, vy = cx + 165, cy - 185
    ctx.move_to(cx, cy); ctx.line_to(vx, vy); ctx.stroke()
    ctx.arc(vx, vy, 9, 0, 2 * math.pi)
    ctx.set_source_rgba(*C_CRIMSON, 1.0)
    ctx.fill()

def wp_heavyhex(ctx):
    _draw_base_canvas(ctx, (0.07, 0.11, 0.21), C_SURF_DARK, C_BASE_DARK, grid_alpha=0.04)
    hex_r = 78
    dx = hex_r * math.sqrt(3)
    dy = hex_r * 1.5

    for row in range(-1, int(H / dy) + 2):
        for col_idx in range(-1, int(W / dx) + 2):
            hx = col_idx * dx + (dx * 0.5 if row % 2 else 0)
            hy = row * dy
            dist = math.hypot(hx - W * 0.5, hy - H * 0.5)
            in_part_a = dist < 240
            in_guard = 240 <= dist < 350

            verts = [
                (hx + hex_r * math.cos(math.pi / 6 + k * math.pi / 3),
                 hy + hex_r * math.sin(math.pi / 6 + k * math.pi / 3))
                for k in range(6)
            ]
            ctx.move_to(*verts[0])
            for vx, vy in verts[1:]:
                ctx.line_to(vx, vy)
            ctx.close_path()

            edge_col = C_MINT if in_part_a else (C_VIOLET if in_guard else C_CYAN)
            alpha = 0.58 if in_part_a else (0.45 if in_guard else 0.18)
            ctx.set_line_width(2.0 if (in_part_a or in_guard) else 1.2)
            ctx.set_source_rgba(*edge_col, alpha)
            ctx.stroke()

            for vx, vy in verts[:3]:
                ctx.arc(vx, vy, 5.0 if in_part_a else 3.5, 0, 2 * math.pi)
                ctx.set_source_rgba(*edge_col, min(1.0, alpha + 0.25))
                ctx.fill()

def wp_surfacecode(ctx):
    _draw_base_canvas(ctx, (0.10, 0.08, 0.23), C_SURF_DARK, C_BASE_DARK, grid_alpha=0.05)
    cx, cy = W * 0.5, H * 0.5
    cell = 74
    n = 4
    for r in range(-n, n):
        for c in range(-n, n):
            px = cx + (c - r) * cell
            py = cy + (c + r) * cell * 0.62
            is_x = (r + c) % 2 == 0
            col = C_CYAN if is_x else C_VIOLET
            ctx.move_to(px, py - cell * 0.62)
            ctx.line_to(px + cell, py)
            ctx.line_to(px, py + cell * 0.62)
            ctx.line_to(px - cell, py)
            ctx.close_path()
            ctx.set_source_rgba(*col, 0.16)
            ctx.fill_preserve()
            ctx.set_line_width(1.6)
            ctx.set_source_rgba(*col, 0.55)
            ctx.stroke()

            ctx.arc(px, py, 4.5, 0, 2 * math.pi)
            ctx.set_source_rgba(*col, 0.85)
            ctx.fill()

    chain = [(cx - 2 * cell, cy), (cx - cell, cy - cell * 0.62), (cx, cy), (cx + cell, cy + cell * 0.62), (cx + 2 * cell, cy)]
    ctx.set_line_width(3.5)
    ctx.set_source_rgba(*C_MINT, 0.92)
    ctx.move_to(*chain[0])
    for pt in chain[1:]:
        ctx.line_to(*pt)
    ctx.stroke()
    for pt in (chain[0], chain[-1]):
        ctx.arc(pt[0], pt[1], 8.0, 0, 2 * math.pi)
        ctx.set_source_rgba(*C_CRIMSON, 1.0)
        ctx.fill()

def wp_obsidian(ctx):
    _draw_base_canvas(ctx, (0.09, 0.07, 0.18), (0.06, 0.05, 0.12), C_BASE_DARK, grid_alpha=0.06)
    cx, cy = W * 0.5, H * 0.5
    for r, a in [(330, 0.10), (240, 0.16), (150, 0.22), (75, 0.30)]:
        ctx.set_line_width(1.4)
        ctx.set_source_rgba(*C_CYAN, a)
        ctx.arc(cx, cy, r, 0, 2 * math.pi)
        ctx.stroke()
    ctx.set_source_rgba(*C_VIOLET, 0.45)
    ctx.move_to(cx - 28, cy); ctx.line_to(cx + 28, cy)
    ctx.move_to(cx, cy - 28); ctx.line_to(cx, cy + 28)
    ctx.stroke()

def generate_all_wallpapers(omniq_etc_dir):
    os.makedirs(omniq_etc_dir, exist_ok=True)
    specs = [
        ("wallpaper.png",             "thumb_wallpaper.png",   wp_flagship,    "QUANTUM HORIZON"),
        ("wallpaper_cryo.png",        "thumb_cryo.png",        wp_cryo,        "CRYO DILUTION 14.2mK"),
        ("wallpaper_hilbert.png",     "thumb_hilbert.png",     wp_hilbert,     "HILBERT BLOCH MANIFOLD"),
        ("wallpaper_heavyhex.png",    "thumb_heavyhex.png",    wp_heavyhex,    "128Q HEAVY-HEX QMMU"),
        ("wallpaper_surfacecode.png", "thumb_surfacecode.png", wp_surfacecode, "SURFACE CODE QEC"),
        ("wallpaper_obsidian.png",    "thumb_obsidian.png",    wp_obsidian,    "QUANTUM OBSIDIAN"),
    ]

    for fname, thumb_name, fn, subtitle in specs:
        surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, W, H)
        ctx = cairo.Context(surf)
        ctx.set_antialias(cairo.ANTIALIAS_BEST)
        fn(ctx)
        out_path = os.path.join(omniq_etc_dir, fname)
        surf.write_to_png(out_path)

        with Image.open(out_path) as im:
            rgba = im.convert("RGBA")
            _stamp_corner_watermark(rgba, subtitle)
            rgba.convert("RGB").save(out_path, "PNG")

            thumb = rgba.convert("RGB").resize((320, 180), Image.Resampling.LANCZOS)
            thumb.save(os.path.join(omniq_etc_dir, thumb_name), "PNG")

            if fname == "wallpaper.png":
                blur_lock = rgba.convert("RGB").filter(ImageFilter.GaussianBlur(radius=24))
                blur_lock.save(os.path.join(omniq_etc_dir, "wallpaper_lock_blur.png"), "PNG")

    wp_conf_path = os.path.join(omniq_etc_dir, "wallpaper.conf")
    wp_conf = {
        "mode": "image",
        "path": "wallpaper.png",
        "color": "#0D0B18",
        "updated_at": time.time(),
    }
    with open(wp_conf_path, "w", encoding="utf-8") as f:
        json.dump(wp_conf, f, indent=2)

def main():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if "--test" in sys.argv:
        icons_dir = os.path.join(repo_root, "etc/omniq/icons")
        if os.path.isdir(icons_dir) and os.path.isfile(os.path.join(repo_root, "etc/omniq/wallpaper.conf")):
            print("[+] generate_omniq_assets.py verified existing assets OK.")
            return 0

    omniq_etc = os.path.join(repo_root, "etc/omniq")
    icons_dir = os.path.join(omniq_etc, "icons")
    print("[*] Generating High-DPI Anti-Aliased RGBA Icons across 10 resolutions...")
    generate_all_icons(icons_dir)
    print(f"[+] Generated {len(ICONS_SPEC)} icon families across {RESOLUTIONS} in {icons_dir}")

    print("[*] Generating 6 1920x1080 Quantum Wallpapers + Thumbnails + wallpaper.conf...")
    generate_all_wallpapers(omniq_etc)
    print(f"[+] Generated 6 Quantum Wallpapers + thumbs + wallpaper.conf in {omniq_etc}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
