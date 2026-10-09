#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WEB = ROOT / "web"
OUT_C = ROOT / "web_assets.c"
OUT_H = ROOT / "web_assets.h"
ASSETS = [
    ("index_html", WEB / "index.html", "text/html; charset=utf-8", "/"),
    ("app_css", WEB / "app.css", "text/css; charset=utf-8", "/app.css"),
    ("favicon_png", WEB / "favicon.png", "image/png", "/favicon.png"),
]

OUT_H.write_text(
    "#ifndef PS5_MONITOR_WEB_ASSETS_H\n"
    "#define PS5_MONITOR_WEB_ASSETS_H\n"
    "#include <stddef.h>\n"
    "typedef struct ps5_monitor_web_asset { const char *path; const char *content_type; const unsigned char *data; size_t size; } ps5_monitor_web_asset_t;\n"
    "const ps5_monitor_web_asset_t *ps5_monitor_web_asset_find(const char *path);\n"
    "#endif\n"
)

parts = ['#include "web_assets.h"', '#include <string.h>', '']
entries = []
for symbol, path, ctype, route in ASSETS:
    data = path.read_bytes()
    parts.append(f"static const unsigned char {symbol}[] = {{")
    for i in range(0, len(data), 16):
        parts.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i+16]) + ",")
    parts.append("};\n")
    entries.append((route, ctype, symbol, len(data)))

parts.append("static const ps5_monitor_web_asset_t assets[] = {")
for route, ctype, symbol, size in entries:
    parts.append(f'    {{ "{route}", "{ctype}", {symbol}, {size} }},')
parts.append("};\n")
parts.append(
    "const ps5_monitor_web_asset_t *ps5_monitor_web_asset_find(const char *path) {\n"
    "    if (!path) return 0;\n"
    "    for (size_t i = 0; i < sizeof(assets)/sizeof(assets[0]); ++i)\n"
    "        if (strcmp(path, assets[i].path) == 0) return &assets[i];\n"
    "    return 0;\n"
    "}\n"
)
OUT_C.write_text("\n".join(parts))
