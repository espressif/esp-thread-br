#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0
"""
Check that ESP_OT_REST_API_VERSION (the runtime source of truth returned by the
REST API discovery endpoint) and openapi.yaml's `info.version` stay in sync, so the
two do not silently drift apart when one is bumped without the other.

See https://github.com/espressif/esp-thread-br/pull/216#discussion_r3819911931.
"""

import re
import sys
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parents[2]
HEADER_PATH = REPO_ROOT / "components/esp_ot_br_server/private_include/esp_br_web_api.h"
OPENAPI_PATH = REPO_ROOT / "components/esp_ot_br_server/src/openapi.yaml"

VERSION_DEFINE_RE = re.compile(r'#define\s+ESP_OT_REST_API_VERSION\s+"([^"]+)"')


def get_header_version(path: Path) -> str:
    match = VERSION_DEFINE_RE.search(path.read_text())
    if not match:
        print(f"Failed to find ESP_OT_REST_API_VERSION in {path}")
        sys.exit(1)
    return match.group(1)


def get_openapi_version(path: Path) -> str:
    with open(path) as f:
        spec = yaml.safe_load(f)
    try:
        return spec["info"]["version"]
    except (KeyError, TypeError):
        print(f"Failed to find info.version in {path}")
        sys.exit(1)


def main() -> None:
    header_version = get_header_version(HEADER_PATH)
    openapi_version = get_openapi_version(OPENAPI_PATH)

    if header_version != openapi_version:
        print(
            f"REST API version mismatch: ESP_OT_REST_API_VERSION in {HEADER_PATH} is "
            f"'{header_version}', but info.version in {OPENAPI_PATH} is '{openapi_version}'. "
            "Bump both together."
        )
        sys.exit(1)

    print(f"REST API version check passed: {header_version}")


if __name__ == "__main__":
    main()
