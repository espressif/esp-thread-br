# REST API Changelog

Version history of the esp-thread-br REST API, as reported by `ESP_OT_REST_API_VERSION`
(`private_include/esp_br_web_api.h`) and `openapi.yaml`'s `info.version`. Follows
[Semantic Versioning](https://semver.org/): MAJOR for incompatible API changes, MINOR for
backward-compatible additions, PATCH for backward-compatible fixes.

## [2.0.0]

### Changed
- Align Thread REST methods, schemas, media types, errors, OPTIONS/Allow, and
  discovery with ot-br-posix REST 0.6.0.
- Structured node, leader, and dataset JSON now uses lower camel case; `/node`
  returns a role string and POSIX's router count/hex RLOC16 fields.
- Pending dataset writes register with the leader and support conditional
  `If-None-Match: *` writes, matching the compatibility baseline.

### Added
- Commissioner state/joiner APIs and `/node/coprocessor/version`.
- `/api/node`, action/device/diagnostic collections and item APIs, including all
  five upstream asynchronous action types and sparse field queries.
- `/.well-known/thread/br-rest` discovery.
- ESP HTTP/OpenThread handlers for Thread REST operations;
  request bodies are limited to 16 KiB and collections are held in RAM.

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
