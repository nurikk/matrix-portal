# PlatformIO pre-build script: embed web/index.html into src/web_ui.h.
#
# The control panel is authored as a normal HTML file (web/index.html) so it can
# be edited with proper tooling. This script converts it into a PROGMEM byte
# array (src/web_ui.h, git-ignored, regenerated each build) that web_portal.cpp
# serves via gServer.send_P(..., kIndexHtml). A byte array is used (rather than a
# raw string literal) so the HTML can contain any bytes without delimiter issues.
Import("env")  # noqa: F821  (provided by PlatformIO)
import os

project = env["PROJECT_DIR"]
src = os.path.join(project, "web", "index.html")
dst = os.path.join(project, "src", "web_ui.h")

if not os.path.isfile(src):
    raise SystemExit("embed_web_ui: missing source %s" % src)

with open(src, "rb") as f:
    data = f.read()

rows = ["  " + "".join("0x%02x," % b for b in data[i:i + 16])
        for i in range(0, len(data), 16)]

header = (
    "// AUTO-GENERATED from web/index.html by scripts/embed_web_ui.py - do not edit.\n"
    "#pragma once\n"
    "#include <pgmspace.h>\n"
    "static const char kIndexHtml[] PROGMEM = {\n"
    + "\n".join(rows) + "\n"
    "  0x00\n"
    "};\n"
)

old = open(dst).read() if os.path.isfile(dst) else None
if old != header:
    with open(dst, "w") as f:
        f.write(header)
    print("embed_web_ui: generated src/web_ui.h from web/index.html (%d bytes)" % len(data))
else:
    print("embed_web_ui: src/web_ui.h already up to date (%d bytes)" % len(data))
