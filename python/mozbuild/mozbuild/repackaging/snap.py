# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import subprocess

from mozbuild.repackaging.application_ini import get_application_ini_values

SNAP_MISC_DIR = "taskcluster/docker/firefox-snap"
SNAP_YAML_IN = os.path.join(SNAP_MISC_DIR, "firefox.snapcraft.yaml.in")

def repackage_snap(srcdir, snapdir, snapcraft):
    pkgsrc = os.path.join(snapdir, "source")
    distrib = os.path.join(pkgsrc, "distribution")

    snap_appname = "firefox-devel"
    snap_version, snap_buildno = get_application_ini_values(
        pkgsrc,
        dict(section="App", value="Version"),
        dict(section="App", value="BuildID"),
    )

    with open(os.path.join(snapdir, "snapcraft.yaml"), "w") as snap_yaml:
        subprocess.check_call(
            ["sed",
             "-e", "s/@VERSION@/%s/" % snap_version,
             "-e", "s/@BUILD_NUMBER@/%s/" % snap_buildno,
             "-e", "s/@APP_NAME@/%s/" % snap_appname,
             "-e", "/^summary: /s/$/ (development build)/",
             os.path.join(srcdir, SNAP_YAML_IN)],
            stdout = snap_yaml,
        )

    subprocess.check_call(["mkdir", "-p", distrib])

    for (src, dst) in [
            ("tmpdir", pkgsrc),
            ("firefox.desktop", distrib),
            ("policies.json", distrib),
    ]:
        subprocess.check_call(
            ["cp", os.path.join(srcdir, SNAP_MISC_DIR, src), dst + "/"],
        )

    subprocess.check_call(
        [snapcraft],
        cwd = snapdir,
    )
