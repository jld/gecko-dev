# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import shutil
import subprocess

from mozbuild.repackaging.application_ini import get_application_ini_values

SNAP_MISC_DIR = "taskcluster/docker/firefox-snap"
SNAP_YAML_IN = os.path.join(SNAP_MISC_DIR, "firefox.snapcraft.yaml.in")

def repackage_snap(srcdir, snapdir, snapcraft, arch='amd64'):
    pkgsrc = os.path.join(snapdir, "source")
    distrib = os.path.join(pkgsrc, "distribution")

    # Obtain the build's version info
    appname = "firefox-devel"
    version, buildno = get_application_ini_values(
        pkgsrc,
        dict(section="App", value="Version"),
        dict(section="App", value="BuildID"),
    )

    # Generate the snapcraft.yaml
    with open(os.path.join(snapdir, "snapcraft.yaml"), "w") as snap_yaml:
        subprocess.check_call(
            ["sed",
             "-e", "s/@VERSION@/%s/" % version,
             "-e", "s/@BUILD_NUMBER@/%s/" % buildno,
             "-e", "s/@APP_NAME@/%s/" % appname,
             "-e", "/^summary: /s/$/ (development build)/",
             os.path.join(srcdir, SNAP_YAML_IN)],
            stdout = snap_yaml,
        )

    # Copy in some miscellaneous extra files
    os.makedirs(distrib, exist_ok = True)

    for (srcfile, dstdir) in [
            ("tmpdir", pkgsrc),
            ("firefox.desktop", distrib),
            ("policies.json", distrib),
    ]:
        shutil.copy(os.path.join(srcdir, SNAP_MISC_DIR, srcfile),
                    os.path.join(dstdir, srcfile))

    # At last, build the snap.
    env = dict(os.environ)
    env["SNAP_ARCH"] = arch
    subprocess.check_call(
        [snapcraft],
        env = env,
        cwd = snapdir,
    )

    snapfile = f"{appname}_{version}-{buildno}_{arch}.snap"
    snappath = os.path.join(snapdir, snapfile)

    if not os.path.exists(snappath):
        raise AssertionError(f"Snap file {snapfile} doesn't exist?")

    # Create a symlink to the file for later use.
    latest_snap = os.path.join(snapdir, "latest.snap")
    try:
        os.unlink(latest_snap)
    except FileNotFoundError:
        pass
    os.symlink(snapfile, latest_snap)

    return snappath

def unpack_tarball(package, destdir):
    os.makedirs(destdir, exist_ok = True)
    subprocess.check_call([
        "tar",
        "-C",
        destdir,
        "-xvf",
        package,
        "--strip-components=1",
    ])

def missing_connections(app_name):
    rv = []
    with subprocess.Popen(
            ["snap", "connections", app_name],
            stdout = subprocess.PIPE,
            encoding = "utf-8",
    ) as proc:
        header = next(proc.stdout)
        for line in proc.stdout:
            iface, plug, slot, notes = line.split(maxsplit=3)
            if plug != "-" and slot == "-":
                rv.append(plug)
    return rv
