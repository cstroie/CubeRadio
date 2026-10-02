"""
PlatformIO pre-script: build the SPIFFS image from a gzip-compressed copy of data/.

The ESP32 WebServer serves "<file>.gz" with "Content-Encoding: gzip" when
"<file>" itself does not exist. Web assets (.html, .css, .js, .svg) are
therefore stored compressed only, which cuts flash use and transfer time by
about 75%. Files the firmware reads itself (JSON, JSON Lines) are copied
unchanged. Edit the files in data/ as usual; the staging directory is
rebuilt on every buildfs/uploadfs.
"""

import gzip
import os
import shutil

from SCons.Script import COMMAND_LINE_TARGETS

Import("env")  # noqa: F821 (provided by PlatformIO)

COMPRESS = (".html", ".css", ".js", ".svg")
SKIP = (".example", ".bak", ".tmp")


def stage_data():
    src = env.subst("$PROJECT_DATA_DIR")
    dst = os.path.join(env.subst("$BUILD_DIR"), "data")
    shutil.rmtree(dst, ignore_errors=True)
    os.makedirs(dst)
    for root, _, files in os.walk(src):
        outdir = os.path.normpath(os.path.join(dst, os.path.relpath(root, src)))
        os.makedirs(outdir, exist_ok=True)
        for name in sorted(files):
            if name.endswith(SKIP):
                continue
            path = os.path.join(root, name)
            if name.lower().endswith(COMPRESS):
                with open(path, "rb") as f:
                    data = f.read()
                # mtime=0 keeps the image reproducible
                with open(os.path.join(outdir, name + ".gz"), "wb") as out:
                    with gzip.GzipFile(filename=name, mode="wb", fileobj=out,
                                       compresslevel=9, mtime=0) as gz:
                        gz.write(data)
            else:
                shutil.copy2(path, os.path.join(outdir, name))
    print("Staged gzip-compressed SPIFFS data in %s" % dst)
    env.Replace(PROJECT_DATA_DIR=dst)


if {"buildfs", "uploadfs", "uploadfsota"} & set(COMMAND_LINE_TARGETS):
    stage_data()
