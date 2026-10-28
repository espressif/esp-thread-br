/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include "host_platform.h"
#define ESP_OT_REST_API_VERSION "2.0.0"
#include "rest_api.inc"
#include "rest_web.inc"

static httpd_req_t request(httpd_method_t method, const char *uri, const char *body, const char *accept,
                           const char *type)
{
    httpd_req_t req = {
        .method = method, .uri = uri, .input = body ? body : "", .content_len = body ? strlen(body) : 0, .fragment = 3};
    req.headers = cJSON_CreateObject();
    if (accept) {
        cJSON_AddStringToObject(req.headers, "aCcEpT", accept);
    }
    if (type) {
        cJSON_AddStringToObject(req.headers, "content-type", type);
    }
    return req;
}
static cJSON *run_request(httpd_req_t *req, int status)
{
    esp_otbr_thread_rest_handler(req);
    assert(req->status && atoi(req->status) == status);
    assert(!strcmp(cJSON_GetStringValue(rest_get(req->sent_headers, "Access-Control-Allow-Origin")), "*"));
    return cJSON_Parse(req->body);
}
static void release(httpd_req_t *req, cJSON *json)
{
    cJSON_Delete(req->headers);
    cJSON_Delete(req->sent_headers);
    cJSON_Delete(json);
    free(req->body);
    free(req->status);
    free(req->type);
}
static void test_node(void)
{
    httpd_req_t req = request(HTTP_GET, "/node", NULL, NULL, NULL);
    cJSON *json = run_request(&req, 200);
    assert(!strcmp(rest_string(json, "state"), "disabled"));
    assert(rest_get(json, "leaderData") && !rest_get(json, "LeaderData"));
    assert(!strcmp(rest_string(json, "rloc16"), "0x0000"));
    assert(!strcmp(rest_string(json, "rlocAddress"), "::"));
    assert(rest_get(json, "routerCount")->valueint == 1);
    release(&req, json);
    req = request(HTTP_GET, "/node/coprocessor/version", NULL, NULL, NULL);
    json = run_request(&req, 200);
    assert(!strcmp(cJSON_GetStringValue(json), "test-rcp"));
    release(&req, json);
    req = request(HTTP_GET, "/.well-known/thread/br-rest", NULL, NULL, NULL);
    json = run_request(&req, 200);
    assert(!strcmp(rest_string(rest_get(json, "api"), "version"), ESP_OT_REST_API_VERSION));
    release(&req, json);
}
static void test_routing(void)
{
    httpd_req_t req = request(HTTP_OPTIONS, "/node/state", NULL, NULL, NULL);
    cJSON *json = run_request(&req, 200);
    assert(!strcmp(rest_string(req.sent_headers, "Allow"), "GET, PUT, OPTIONS"));
    release(&req, json);
    req = request(HTTP_PATCH, "/node/state", NULL, NULL, NULL);
    json = run_request(&req, 405);
    assert(rest_get(json, "status")->valueint == 405);
    release(&req, json);
    req = request(HTTP_GET, "/node/unknown", NULL, NULL, NULL);
    json = run_request(&req, 404);
    release(&req, json);
    req = request(HTTP_GET, "/node/%00", NULL, NULL, NULL);
    json = run_request(&req, 400);
    release(&req, json);
    req = request(HTTP_GET, "/api/actions?fields%5BupdateDeviceCollectionTask%5D=%GG", NULL, "*/*", NULL);
    json = run_request(&req, 400);
    release(&req, json);
    req = request(HTTP_POST, "/api/actions", NULL, NULL, "application/vnd.api+json");
    req.content_len = 16385;
    json = run_request(&req, 413);
    assert(!strcmp(rest_string(req.sent_headers, "Connection"), "close"));
    release(&req, json);
    req = request(HTTP_PUT, "/node/state", "\"enable\"", NULL, "application/json");
    req.content_len += 3;
    json = run_request(&req, 400);
    release(&req, json);
}
static void test_route_matrix(void)
{
    unsigned operations = 0;
    for (unsigned i = 0; i < sizeof(s_thread_rest_routes) / sizeof(s_thread_rest_routes[0]); i++) {
        const thread_rest_route_t *route = &s_thread_rest_routes[i];
        operations += __builtin_popcount(route->methods);
        char uri[128];
        snprintf(uri, sizeof(uri), "%s%s", route->path, route->item ? "item" : "");
        httpd_req_t req = request(HTTP_OPTIONS, uri, NULL, NULL, NULL);
        cJSON *json = run_request(&req, 200);
        assert(strstr(rest_string(req.sent_headers, "Allow"), "OPTIONS"));
        release(&req, json);
    }
    assert(operations == 42);
}

static void test_commissioner_and_energy(void)
{
    httpd_req_t req = request(HTTP_PUT, "/node/commissioner/state", "\"enable\"", NULL, "application/json");
    cJSON *json = run_request(&req, 200);
    release(&req, json);
    req = request(HTTP_POST, "/node/commissioner/joiner", "{\"pskd\":\"ABC123\",\"joinerId\":\"*\"}", NULL,
                  "application/json");
    json = run_request(&req, 200);
    release(&req, json);
    const char *body =
        "{\"data\":[{\"type\":\"addThreadDeviceTask\",\"attributes\":{\"pskd\":\"ABC123\",\"joinerId\":\"*\"}}]}";
    req = request(HTTP_POST, "/api/actions", body, NULL, THREAD_REST_JSON_API);
    json = run_request(&req, 200);
    assert(rest_get(rest_get(cJSON_GetArrayItem(rest_get(json, "data"), 0), "attributes"), "timeout")->valueint == 60);
    release(&req, json);
    rest_poll(esp_openthread_get_instance());
    thread_rest_item_t *action = s_rest_actions;
    assert(action->joiner_registered);
    rest_joiner_callback(OT_COMMISSIONER_JOINER_START, &action->joiner, NULL, NULL);
    assert(!strcmp(rest_string(action->attributes, "status"), "attempted"));
    rest_joiner_callback(OT_COMMISSIONER_JOINER_END, &action->joiner, NULL, NULL);
    rest_poll(esp_openthread_get_instance());
    assert(action->complete && !action->joiner_registered);
    rest_collection_delete(&s_rest_actions, NULL, esp_openthread_get_instance());
    body = "{\"data\":[{\"type\":\"getEnergyScanTask\",\"attributes\":{\"destination\":\"0x0000\",\"channelMask\":[11,"
           "12],\"count\":1,\"period\":100,\"scanDuration\":10,\"timeout\":60}}]}";
    req = request(HTTP_POST, "/api/actions", body, NULL, THREAD_REST_JSON_API);
    json = run_request(&req, 200);
    release(&req, json);
    rest_poll(esp_openthread_get_instance());
    action = s_rest_energy_action;
    assert(action);
    const uint8_t energy[] = {156, 166, 176, 186};
    rest_energy_callback((1u << 11) | (1u << 12), energy, 4, (void *)(uintptr_t)action->token);
    assert(action->complete && !s_rest_energy_action);
    cJSON *report = rest_get(s_rest_diagnostics->attributes, "report");
    assert(cJSON_GetArraySize(report) == 2);
    cJSON *rssi = rest_get(cJSON_GetArrayItem(report, 0), "maxRssi");
    assert(cJSON_GetArrayItem(rssi, 0)->valueint == -100);
    rest_collection_delete(&s_rest_actions, NULL, esp_openthread_get_instance());
    body = "{\"data\":[{\"type\":\"resetNetworkDiagCounterTask\",\"attributes\":{\"types\":[\"macCounters\"],"
           "\"timeout\":5}}]}";
    req = request(HTTP_POST, "/api/actions", body, NULL, THREAD_REST_JSON_API);
    json = run_request(&req, 200);
    release(&req, json);
    rest_poll(esp_openthread_get_instance());
    assert(s_rest_actions->complete);
    rest_collection_delete(&s_rest_actions, NULL, esp_openthread_get_instance());
}

static void test_dataset(void)
{
    cJSON *json = cJSON_Parse("{\"networkName\":\"mesh\",\"networkKey\":\"00112233445566778899aabbccddeeff\","
                              "\"channel\":15,\"activeTimestamp\":{\"seconds\":42}}");
    otOperationalDataset dataset = {0};
    assert(rest_parse_dataset(json, &dataset, false));
    assert(!strcmp(dataset.mNetworkName.m8, "mesh") && dataset.mChannel == 15 &&
           dataset.mActiveTimestamp.mSeconds == 42);
    cJSON_Delete(json);
    json = cJSON_Parse("{\"networkKey\":\"abc\"}");
    assert(!rest_parse_dataset(json, &dataset, false));
    cJSON_Delete(json);
    json = cJSON_Parse("{\"activeDataset\":{},\"delay\":30000}");
    assert(rest_parse_dataset(json, &dataset, true));
    cJSON_Delete(json);
    json = cJSON_Parse("{\"delay\":30000}");
    assert(!rest_parse_dataset(json, &dataset, true));
    cJSON_Delete(json);
    httpd_req_t req = request(HTTP_GET, "/node/dataset/active", NULL, NULL, NULL);
    json = run_request(&req, 200);
    assert(!strcmp(rest_string(json, "networkName"), "test-mesh"));
    release(&req, json);
    req = request(HTTP_GET, "/node/dataset/active", NULL, "text/plain", NULL);
    json = run_request(&req, 200);
    assert(!strcmp(req.body, "ABCD") && !strcmp(req.type, "text/plain"));
    release(&req, json);
    req = request(HTTP_PUT, "/node/dataset/active", "{}", NULL, "application/json");
    cJSON_AddStringToObject(req.headers, "If-None-Match", " * ");
    json = run_request(&req, 412);
    release(&req, json);
    mock_role = OT_DEVICE_ROLE_CHILD;
    req =
        request(HTTP_PUT, "/node/dataset/pending", "{\"activeDataset\":{},\"delay\":30000}", NULL, "application/json");
    json = run_request(&req, 201);
    release(&req, json);
    mock_pending_result = OT_ERROR_REJECTED;
    req =
        request(HTTP_PUT, "/node/dataset/pending", "{\"activeDataset\":{},\"delay\":30000}", NULL, "application/json");
    json = run_request(&req, 409);
    assert(!strcmp(rest_string(json, "detail"), "rejected by leader"));
    release(&req, json);
    mock_pending_result = OT_ERROR_RESPONSE_TIMEOUT;
    req =
        request(HTTP_PUT, "/node/dataset/pending", "{\"activeDataset\":{},\"delay\":30000}", NULL, "application/json");
    json = run_request(&req, 504);
    release(&req, json);
    mock_role = OT_DEVICE_ROLE_DISABLED;
}
static void test_actions(void)
{
    httpd_req_t req = request(HTTP_GET, "/api/actions", NULL, NULL, NULL);
    cJSON *json = run_request(&req, 406);
    release(&req, json);
    req = request(HTTP_POST, "/api/actions", "{\"data\":[]}", NULL, "application/json");
    json = run_request(&req, 415);
    release(&req, json);
    req = request(HTTP_POST, "/api/actions", "{\"data\":[{\"type\":\"unknown\",\"attributes\":{}}]}", NULL,
                  THREAD_REST_JSON_API);
    json = run_request(&req, 422);
    release(&req, json);
    const char *body = "{\"data\":[{\"type\":\"updateDeviceCollectionTask\",\"attributes\":{\"maxAge\":1.5,"
                       "\"maxRetries\":3,\"deviceCount\":5,\"timeout\":5}}]}";
    req = request(HTTP_POST, "/api/actions", body, NULL, THREAD_REST_JSON_API);
    json = run_request(&req, 200);
    cJSON *item = cJSON_GetArrayItem(rest_get(json, "data"), 0);
    char id[37];
    strlcpy(id, rest_string(item, "id"), sizeof(id));
    assert(strlen(id) == 36);
    release(&req, json);
    rest_poll(esp_openthread_get_instance());
    assert(s_rest_mesh_action && s_rest_mesh_action->active);
    char uri[160];
    snprintf(uri, sizeof(uri), "/api/actions/%s?fields%%5BupdateDeviceCollectionTask%%5D=status", id);
    req = request(HTTP_GET, uri, NULL, THREAD_REST_JSON_API, NULL);
    json = run_request(&req, 200);
    item = rest_get(rest_get(json, "data"), "attributes");
    assert(rest_get(item, "status") && !rest_get(item, "maxAge"));
    release(&req, json);
    snprintf(uri, sizeof(uri), "/api/actions/%s", id);
    req = request(HTTP_DELETE, uri, NULL, NULL, NULL);
    json = run_request(&req, 204);
    release(&req, json);
    assert(!s_rest_mesh_action);
    body = "{\"data\":[{\"type\":\"getNetworkDiagnosticTask\",\"attributes\":{\"destination\":\"0x0000\",\"types\":["
           "\"children\"],\"timeout\":60}}]}";
    req = request(HTTP_POST, "/api/actions", body, NULL, THREAD_REST_JSON_API);
    json = run_request(&req, 200);
    release(&req, json);
    rest_poll(esp_openthread_get_instance());
    rest_poll(esp_openthread_get_instance());
    assert(s_rest_query_action);
    otMeshDiagChildEntry child = {.mRloc16 = 1, .mFrameErrorRate = 65535, .mSupportsErrRate = true};
    rest_children_callback(OT_ERROR_NONE, &child, (void *)(uintptr_t)s_rest_query_action->token);
    rest_poll(esp_openthread_get_instance());
    assert(s_rest_diagnostics && s_rest_actions->complete);
    cJSON *children = rest_get(s_rest_diagnostics->attributes, "children");
    assert(cJSON_GetArraySize(children) == 1);
    assert(rest_get(cJSON_GetArrayItem(children, 0), "frameErrorRate")->valuedouble == 1);
    rest_collection_delete(&s_rest_actions, NULL, esp_openthread_get_instance());
    body = "{\"data\":[{\"type\":\"getNetworkDiagnosticTask\",\"attributes\":{\"destination\":\"0x0000\",\"types\":["
           "\"extAddress\"],\"timeout\":60}}]}";
    req = request(HTTP_POST, "/api/actions", body, NULL, THREAD_REST_JSON_API);
    json = run_request(&req, 200);
    release(&req, json);
    rest_poll(esp_openthread_get_instance());
    uint32_t old_token = s_rest_actions->token;
    rest_collection_delete(&s_rest_actions, NULL, esp_openthread_get_instance());
    req = request(HTTP_POST, "/api/actions", body, NULL, THREAD_REST_JSON_API);
    json = run_request(&req, 200);
    release(&req, json);
    rest_poll(esp_openthread_get_instance());
    mock_deliver_diagnostic(old_token);
    assert(!s_rest_actions->complete);
    mock_deliver_diagnostic(0);
    assert(s_rest_actions->complete);
}
int main(void)
{
    mock_reset();
    assert(esp_otbr_thread_rest_register(NULL) == ESP_OK && mock_registered == 28);
    test_node();
    test_route_matrix();
    test_routing();
    test_dataset();
    test_actions();
    test_commissioner_and_energy();
    rest_collection_delete(&s_rest_actions, NULL, esp_openthread_get_instance());
    rest_collection_delete(&s_rest_devices, NULL, esp_openthread_get_instance());
    rest_collection_delete(&s_rest_diagnostics, NULL, esp_openthread_get_instance());
    puts("ESP-native REST routes, datasets, collections, callbacks, and HTTP transport passed");
    return 0;
}
