# Comprime con gzip los archivos del dashboard (web/) y los embebe en el firmware
# generando include/web_files.h. PlatformIO lo ejecuta antes de cada compilación
# (extra_scripts = pre:tools/embed_web.py en platformio.ini).
# No edites web_files.h: edita los archivos de web/ y vuelve a compilar.
import gzip
import os

Import("env")  # lo define PlatformIO (SCons) al cargar el script

PROJECT_DIR = env.subst("$PROJECT_DIR")
WEB_DIR = os.path.join(PROJECT_DIR, "web")
OUT_FILE = os.path.join(PROJECT_DIR, "include", "web_files.h")

# archivo de web/ -> nombre del arreglo en C++ (ver handleRoot() y setupWebServer())
FILES = [
    ("dashboard.html", "WEB_DASHBOARD_HTML_GZ"),
    ("style.css", "WEB_STYLE_CSS_GZ"),
    ("chart.js", "WEB_CHART_JS_GZ"),
]


def c_array(name, data):
    rows = ["  " + ", ".join("0x%02x" % b for b in data[i:i + 16]) + "," for i in range(0, len(data), 16)]
    return "const uint8_t %s[] PROGMEM = {\n%s\n};\nconst size_t %s_LEN = %d;\n" % (
        name, "\n".join(rows), name, len(data))


parts = [
    "// GENERADO AUTOMÁTICAMENTE por tools/embed_web.py en cada compilación.",
    "// No lo edites: edita los archivos de web/ y vuelve a compilar.",
    "#pragma once",
    "#include <Arduino.h>",
    "",
]
for filename, name in FILES:
    path = os.path.join(WEB_DIR, filename)
    if not os.path.isfile(path):
        print("embed_web.py: ERROR, falta web/%s" % filename)
        env.Exit(1)
    with open(path, "rb") as f:
        original = f.read()
    # mtime=0: el mismo archivo siempre da los mismos bytes (no recompila si no cambió)
    compressed = gzip.compress(original, compresslevel=9, mtime=0)
    if gzip.decompress(compressed) != original:
        print("embed_web.py: ERROR, la compresión de web/%s no se pudo verificar" % filename)
        env.Exit(1)
    print("embed_web.py: web/%s: %d -> %d bytes (gzip)" % (filename, len(original), len(compressed)))
    parts.append("// web/%s (%d bytes sin comprimir)" % (filename, len(original)))
    parts.append(c_array(name, compressed))

content = "\n".join(parts)
old = None
if os.path.exists(OUT_FILE):
    with open(OUT_FILE, encoding="utf-8") as f:
        old = f.read()
if content != old:
    with open(OUT_FILE, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)
    print("embed_web.py: include/web_files.h actualizado")
