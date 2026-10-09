# Thread REST host tests

Run from the repository root with ESP-IDF available:

```sh
python3 components/esp_ot_br_server/tests/run_host_tests.py
python3 tools/ci/check_rest_api_version_sync.py
idf.py -C examples/basic_thread_border_router build
```

The host runner requires `IDF_PATH`, GCC, and Clang. It compiles the actual C
REST sections from `esp_br_web.c` and `esp_br_web_api.c`, together with the
existing dataset JSON encoders, using deterministic HTTP, FreeRTOS, and
OpenThread stubs. AddressSanitizer and UndefinedBehaviorSanitizer check the
handlers and callback lifetimes. Build artifacts are created in a temporary
directory.

Tests cover node schemas, dataset parsing and media types, conditional writes,
leader rejection and timeout responses, discovery, OPTIONS/Allow, unknown
routes, method errors, collection media negotiation, sparse fields, action
creation and deletion, mesh diagnostic queries, late callbacks, fragmented
request bodies, URL decoding, response header lifetimes, CORS, and body limits.

On hardware, verify commissioner joiner events, energy reports, discovery of
routers and end devices, diagnostic TLVs and mesh queries, and pending dataset
registration with a leader. The host stubs do not simulate radio communication
or FreeRTOS scheduling.
