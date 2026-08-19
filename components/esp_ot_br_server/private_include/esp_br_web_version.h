/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief REST API semantic version, the single source of truth for this component's REST API version.
 *
 * This mirrors ot-br-posix's `OTBR_REST_API_VERSION` (src/rest/version.hpp): both implementations expose
 * the same REST resources, so they are kept at the same version whenever their API surface is at parity.
 * Bump this value, and openapi.yaml's `info.version`, together whenever a REST endpoint is added, removed,
 * or changed, following the same semver rules ot-br-posix documents for its REST API:
 *   - MAJOR: an incompatible API change.
 *   - MINOR: backward-compatible functionality added (or a breaking change while the major version is 0).
 *   - PATCH: a backward-compatible bug fix.
 *
 * Current value matches ot-br-posix's ePSKc endpoints
 * (https://github.com/openthread/ot-br-posix/pull/3480, version bump proposed in
 * https://github.com/openthread/ot-br-posix/pull/3534), which this component also implements.
 */
#define ESP_OT_REST_API_VERSION "0.5.0"

#ifdef __cplusplus
}
#endif
