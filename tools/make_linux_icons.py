# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later

r"""Render the hicolor PNG icons of the Linux package from the icon SVGs.

    python tools\make_linux_icons.py [--out linux\data\icons]

Writes <out>\hicolor\<N>x<N>\apps\hourcalc.png for N = 16, 24, 32, 48, 64, 128, 256. The
data\meson.build installs them as <app id>.png, so the committed names do not change when
the application id does. The scalable icon is assets\icon\app.svg itself (installed
straight from there, nothing is copied).

The drawings are the ones tools\make_ico.py uses for the Windows .ico:

    16 px   assets\icon\app-16.svg     (drawn on a 16 px grid)
    24 px   assets\icon\app-24.svg     (drawn on a 24 px grid)
    32 px   assets\icon\app-16.svg     rendered at 2x, as the .ico does
    48-256  assets\icon\app.svg

Rendering and PNG writing are make_ico's (resvg_py without system fonts, the smaller of
Pillow's optimized PNG and the unfiltered one), so the output is deterministic. Needs
Python with resvg_py and Pillow, like make_ico.py.
"""

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import make_ico  # noqa: E402  (needs the path above)

ICON_DIR = os.path.join(ROOT, "assets", "icon")

# size -> drawing, exactly the choice make_ico.py makes for the same frame sizes
SOURCES = {
    16: "app-16.svg",
    24: "app-24.svg",
    32: "app-16.svg",
    48: "app.svg",
    64: "app.svg",
    128: "app.svg",
    256: "app.svg",
}


def main(argv):
    ap = argparse.ArgumentParser(description="Render the hicolor PNG icons of the Linux package.")
    ap.add_argument("--out", default=os.path.join(ROOT, "linux", "data", "icons"),
                    help="icon theme root (default: linux\\data\\icons)")
    args = ap.parse_args(argv)

    for size, name in sorted(SOURCES.items()):
        img = make_ico.render(make_ico.read_svg(os.path.join(ICON_DIR, name)), size)
        folder = os.path.join(args.out, "hicolor", "%dx%d" % (size, size), "apps")
        os.makedirs(folder, exist_ok=True)
        data = make_ico.png_frame(img)
        path = os.path.join(folder, "hourcalc.png")
        with open(path, "wb") as f:
            f.write(data)
        print("wrote %s (%d bytes, from %s)" % (os.path.relpath(path, ROOT), len(data), name))


if __name__ == "__main__":
    main(sys.argv[1:])
