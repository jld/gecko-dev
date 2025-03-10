# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the beetmover task into an actual task description.
"""


from copy import deepcopy

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.dependencies import get_primary_dependency
from taskgraph.util.schema import Schema, optionally_keyed_by, resolve_keyed_by
from voluptuous import Optional, Required

from gecko_taskgraph.transforms.beetmover import (
    craft_release_properties as beetmover_craft_release_properties,
)
from gecko_taskgraph.transforms.task import task_description_schema
from gecko_taskgraph.util.attributes import (
    copy_attributes_from_dependent_job,
    release_level,
)
from gecko_taskgraph.util.declarative_artifacts import (
    get_geckoview_artifact_id,
    get_geckoview_artifact_map,
    get_geckoview_upstream_artifacts,
)

beetmover_description_schema = Schema(
    {
        Required("label"): str,
        Required("dependencies"): task_description_schema["dependencies"],
        Optional("treeherder"): task_description_schema["treeherder"],
        Required("run-on-projects"): task_description_schema["run-on-projects"],
        Required("run-on-hg-branches"): task_description_schema["run-on-hg-branches"],
        Optional("bucket-scope"): optionally_keyed_by("release-level", str),
        Optional("shipping-phase"): optionally_keyed_by(
            "project", task_description_schema["shipping-phase"]
        ),
        Optional("shipping-product"): task_description_schema["shipping-product"],
        Optional("attributes"): task_description_schema["attributes"],
        Optional("task-from"): task_description_schema["task-from"],
    }
)

transforms = TransformSequence()


@transforms.add
def remove_name(config, jobs):
    for job in jobs:
        if "name" in job:
            del job["name"]
        yield job


transforms.add_validate(beetmover_description_schema)


@transforms.add
def resolve_keys(config, jobs):
    for job in jobs:
        resolve_keyed_by(
            job,
            "run-on-hg-branches",
            item_name=job["label"],
            project=config.params["project"],
        )
        resolve_keyed_by(
            job,
            "shipping-phase",
            item_name=job["label"],
            project=config.params["project"],
        )
        resolve_keyed_by(
            job,
            "bucket-scope",
            item_name=job["label"],
            **{"release-level": release_level(config.params["project"])},
        )
        yield job


@transforms.add
def split_maven_packages(config, jobs):
    for job in jobs:
        dep_job = get_primary_dependency(config, job)
        assert dep_job

        attributes = copy_attributes_from_dependent_job(dep_job)
        for package in attributes["maven_packages"]:
            package_job = deepcopy(job)
            package_job["maven-package"] = package
            yield package_job


@transforms.add
def make_task_description(config, jobs):
    for job in jobs:
        dep_job = get_primary_dependency(config, job)
        assert dep_job

        attributes = copy_attributes_from_dependent_job(dep_job)
        attributes.update(job.get("attributes", {}))

        treeherder = job.get("treeherder", {})
        dep_th_platform = (
            dep_job.task.get("extra", {})
            .get("treeherder", {})
            .get("machine", {})
            .get("platform", "")
        )
        dep_symbol = dep_job.task.get("extra", {}).get("treeherder", {}).get("symbol")
        assert not dep_symbol or (
            dep_symbol.startswith("B") and dep_symbol.endswith("s")
        )
        symbol_suffix = f"-{dep_symbol[1:-1]}" if dep_symbol[1:-1] else ""
        treeherder.setdefault("platform", f"{dep_th_platform}/opt")
        treeherder.setdefault("tier", 2)
        treeherder.setdefault("kind", "build")
        package = job["maven-package"]
        treeherder.setdefault("symbol", f"BM-{package}{symbol_suffix}")
        label = job["label"]
        description = (
            "Beetmover submission for geckoview"
            "{build_platform}/{build_type}'".format(
                build_platform=attributes.get("build_platform"),
                build_type=attributes.get("build_type"),
            )
        )

        job["dependencies"].update(deepcopy(dep_job.dependencies))

        if job.get("locale"):
            attributes["locale"] = job["locale"]

        attributes["run_on_hg_branches"] = job["run-on-hg-branches"]

        task = {
            "label": f"{package}-{label}",
            "description": description,
            "worker-type": "beetmover",
            "scopes": [
                job["bucket-scope"],
                "project:releng:beetmover:action:push-to-maven",
            ],
            "dependencies": job["dependencies"],
            "attributes": attributes,
            "run-on-projects": job["run-on-projects"],
            "treeherder": treeherder,
            "shipping-phase": job["shipping-phase"],
            "maven-package": package,
        }

        yield task


@transforms.add
def make_task_worker(config, jobs):
    for job in jobs:
        job["worker"] = {
            "artifact-map": get_geckoview_artifact_map(config, job),
            "implementation": "beetmover-maven",
            "release-properties": craft_release_properties(config, job),
            "upstream-artifacts": get_geckoview_upstream_artifacts(
                config, job, job["maven-package"]
            ),
        }
        del job["maven-package"]

        yield job


def craft_release_properties(config, job):
    release_properties = beetmover_craft_release_properties(config, job)

    release_properties["artifact-id"] = get_geckoview_artifact_id(
        config,
        job["attributes"]["build_platform"],
        job["maven-package"],
        job["attributes"].get("update-channel"),
    )
    release_properties["app-name"] = "geckoview"

    return release_properties
