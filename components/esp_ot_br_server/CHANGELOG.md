# REST API Changelog

Version history of the esp-thread-br REST API, as reported by `ESP_OT_REST_API_VERSION`
(`private_include/esp_br_web_api.h`) and `openapi.yaml`'s `info.version`. Follows
[Semantic Versioning](https://semver.org/): MAJOR for incompatible API changes, MINOR for
backward-compatible additions, PATCH for backward-compatible fixes.

esp-thread-br versions its REST API independently from ot-br-posix's `OTBR_REST_API_VERSION`,
since the two implementations do not expose the same REST API contract (see
https://github.com/espressif/esp-thread-br/pull/216#discussion_r3819911931).

## [1.1.0]

### Added
- Border Agent ephemeral key (ePSKc) endpoints: `GET`/`PUT /node/ba-epskc/state` and
  `GET`/`POST`/`DELETE /node/ba-epskc/key`.
- REST API discovery endpoint: `GET /.well-known/thread/esp-br-rest`, returning the running
  REST API version and RFC 8288 links to its entry points
  (see https://github.com/espressif/esp-thread-br/pull/216).

## [1.0.0]

### Added
- Initial versioned REST API surface: `/node`, `/node/rloc`, `/node/rloc16`, `/node/state`,
  `/node/ext-address`, `/node/network-name`, `/node/leader-data`, `/node/num-of-router`,
  `/node/ext-panid`, `/node/ba-id`, `/node/dataset/active`, `/node/dataset/pending`, and
  `/diagnostics`.
