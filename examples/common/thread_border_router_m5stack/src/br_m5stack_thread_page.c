/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 */

#include "br_m5stack_thread_page.h"
#include "br_m5stack_common.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_ot_wifi_cmd.h"
#include "lvgl.h"
#include "nvs.h"
#include "bsp/esp-bsp.h"
#include "misc/lv_area.h"
#include "openthread/border_agent.h"
#include "openthread/border_routing.h"
#include "openthread/dataset.h"
#include "openthread/dataset_ftd.h"
#include "openthread/error.h"
#include "openthread/thread.h"
#include "openthread/verhoeff_checksum.h"

static lv_obj_t *s_thread_page = NULL;
static lv_obj_t *s_thread_role_label = NULL;
static lv_obj_t *s_br_state_label = NULL;
otBorderRoutingState s_br_state = OT_BORDER_ROUTING_STATE_UNINITIALIZED;
static char s_thread_role_str[5][16] = {"disabled", "detached", "child", "router", "leader"};
static char s_br_state_str[4][16] = {"(Uninitialized)", "(Disabled)", "(Stopped)", "(Running)"};

static void thread_update_thread_role(void *user_data)
{
    otDeviceRole role = (otDeviceRole)user_data;
    br_m5stack_modify_label(s_thread_role_label, s_thread_role_str[role]);
}

static void thread_update_br_state(void *user_data)
{
    otBorderRoutingState state = (otBorderRoutingState)user_data;
    br_m5stack_modify_label(s_br_state_label, s_br_state_str[state]);
}

static void thread_update_msg(otChangedFlags aFlags, void *aContext)
{
    if (aFlags & OT_CHANGED_THREAD_ROLE) {
        otDeviceRole role = otThreadGetDeviceRole(esp_openthread_get_instance());
        lv_async_call(thread_update_thread_role, (void *)role);
    }
    otBorderRoutingState state = otBorderRoutingGetState(esp_openthread_get_instance());
    if (state != s_br_state) {
        s_br_state = state;
        lv_async_call(thread_update_br_state, (void *)s_br_state);
    }
}

static esp_err_t handle_channel_set(const char *input, otOperationalDataset dataset)
{
    uint16_t channel = atoi(input);
    if (strlen(input) != 2 || channel < 11 || channel > 26) {
        return ESP_ERR_INVALID_ARG;
    }

    dataset.mChannel = channel;
    if (otDatasetSetActive(esp_openthread_get_instance(), &dataset) != OT_ERROR_NONE) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t handle_networkkey_set(const char *input, otOperationalDataset dataset)
{
    if (strlen(input) != OT_NETWORK_KEY_SIZE * 2) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < OT_NETWORK_KEY_SIZE; i++) {
        uint8_t value = 0;
        if (isxdigit((uint8_t)input[2 * i]) && isxdigit((uint8_t)input[2 * i + 1])) {
            char str[3] = {input[2 * i], input[2 * i + 1], '\0'};
            char *endptr;
            value = strtol(str, &endptr, 16);
        } else {
            return ESP_ERR_INVALID_ARG;
        }
        dataset.mNetworkKey.m8[i] = value;
    }

    if (otDatasetSetActive(esp_openthread_get_instance(), &dataset) != OT_ERROR_NONE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t handle_panid_set(const char *input, otOperationalDataset dataset)
{
    char *endptr;

    if (strlen(input) == 0 || strlen(input) > 4) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < strlen(input); i++) {
        if (!isxdigit((uint8_t)input[i])) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    dataset.mPanId = strtol(input, &endptr, 16);
    if (otDatasetSetActive(esp_openthread_get_instance(), &dataset) != OT_ERROR_NONE) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void br_m5stack_thread_set_dataset_callback(const char *input, void *user_data)
{
    esp_err_t error = ESP_OK;
    br_m5stack_err_msg_t err_msg = "Success to set";
    otOperationalDataset dataset;
    br_m5stack_dataset_flag_t flag = (br_m5stack_dataset_flag_t)user_data;

    esp_openthread_lock_acquire(portMAX_DELAY);
    BR_M5STACK_GOTO_ON_FALSE(otDatasetGetActive(esp_openthread_get_instance(), &dataset) == OT_ERROR_NONE, exit,
                             "None active dataset\nclick 'random dataset'\nto generate a new one");

    switch (flag) {
    case BR_M5STACK_DATASET_CHANNEL:
        error = handle_channel_set(input, dataset);
        BR_M5STACK_GOTO_ON_FALSE(error == ESP_OK, exit,
                                 (error == ESP_ERR_INVALID_ARG) ? "Invalid args for channel" : "Fail to set channel");
        break;
    case BR_M5STACK_DATASET_NETWORKKEY:
        error = handle_networkkey_set(input, dataset);
        BR_M5STACK_GOTO_ON_FALSE(error == ESP_OK, exit,
                                 (error == ESP_ERR_INVALID_ARG) ? "Invalid args for networkkey"
                                                                : "Fail to set networkkey");
        break;
    case BR_M5STACK_DATASET_PANID:
        error = handle_panid_set(input, dataset);
        BR_M5STACK_GOTO_ON_FALSE(error == ESP_OK, exit,
                                 (error == ESP_ERR_INVALID_ARG) ? "Invalid args for panid" : "Fail to set panid");
        break;
    default:
        break;
    }

exit:
    esp_openthread_lock_release();
    BR_M5STACK_CREATE_WARNING_IF_EXIST(1000);
}

static void br_m5stack_thread_set_dataset(lv_event_t *e)
{
    br_m5stack_err_msg_t err_msg = "";
    otOperationalDataset dataset;
    otDeviceRole role = OT_DEVICE_ROLE_DISABLED;
    otError err = OT_ERROR_NONE;
    otInstance *instance = NULL;

    esp_openthread_lock_acquire(portMAX_DELAY);
    instance = esp_openthread_get_instance();
    role = otThreadGetDeviceRole(instance);
    err = otDatasetGetActive(instance, &dataset);
    esp_openthread_lock_release();

    BR_M5STACK_GOTO_ON_FALSE(role == OT_DEVICE_ROLE_DISABLED, exit, "Stop Thread before setting!");
    BR_M5STACK_GOTO_ON_FALSE(err == OT_ERROR_NONE, exit,
                             "None active dataset\nclick 'random dataset'\nto generate a new one");

    br_m5stack_create_keyboard(lv_layer_top(), br_m5stack_thread_set_dataset_callback, lv_event_get_user_data(e),
                               false);

exit:
    BR_M5STACK_CREATE_WARNING_IF_EXIST(1000);
}

// When using the nano libc, both printf and snprintf do not support the %llu format specifier.
// So use this function to convert uint64_t to string.
static void uint64_to_str(uint64_t value, char *str)
{
    char buf[21];
    int i = 0;

    do {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    } while (value);

    int j = 0;
    while (i > 0) {
        str[j++] = buf[--i];
    }
    str[j] = '\0';
}

static void show_dataset_on_page(lv_obj_t *page, otOperationalDataset dataset)
{
    char txt[128] = "";

    char activetimestamp_str[21] = "None";
    if (dataset.mComponents.mIsActiveTimestampPresent) {
        uint64_to_str(dataset.mActiveTimestamp.mSeconds, activetimestamp_str);
    }
    snprintf(txt, sizeof(txt), "Active Timestamp: %s", activetimestamp_str);
    br_m5stack_create_label(page, txt, &lv_font_montserrat_12, lv_color_black(), LV_ALIGN_TOP_LEFT, 0, 20);

    char pendingtimestamp_str[21] = "None";
    if (dataset.mComponents.mIsPendingTimestampPresent) {
        uint64_to_str(dataset.mPendingTimestamp.mSeconds, pendingtimestamp_str);
    }
    snprintf(txt, sizeof(txt), "Pending Timestamp: %s", pendingtimestamp_str);
    br_m5stack_create_label(page, txt, &lv_font_montserrat_12, lv_color_black(), LV_ALIGN_TOP_LEFT, 0, 35);

    char networkkey_str[OT_NETWORK_KEY_SIZE * 2 + 1] = "None";
    if (dataset.mComponents.mIsNetworkKeyPresent) {
        char *p = networkkey_str;
        for (int i = 0; i < OT_NETWORK_KEY_SIZE; i++) {
            p += snprintf(p, 3, "%02x", dataset.mNetworkKey.m8[i]);
        }
    }
    snprintf(txt, sizeof(txt), "NetworkKey:%s", networkkey_str);
    br_m5stack_create_label(page, txt, &lv_font_montserrat_12, lv_color_black(), LV_ALIGN_TOP_LEFT, 0, 50);

    snprintf(txt, sizeof(txt), "Network Name: %s",
             dataset.mComponents.mIsNetworkNamePresent ? dataset.mNetworkName.m8 : "None");
    br_m5stack_create_label(page, txt, &lv_font_montserrat_12, lv_color_black(), LV_ALIGN_TOP_LEFT, 0, 65);

    char extpanid_str[OT_EXT_PAN_ID_SIZE * 2 + 1] = "None";
    if (dataset.mComponents.mIsExtendedPanIdPresent) {
        char *p = extpanid_str;
        for (int i = 0; i < OT_EXT_PAN_ID_SIZE; i++) {
            p += snprintf(p, 3, "%02x", dataset.mExtendedPanId.m8[i]);
        }
    }
    snprintf(txt, sizeof(txt), "Ext PAN ID: %s", extpanid_str);
    br_m5stack_create_label(page, txt, &lv_font_montserrat_12, lv_color_black(), LV_ALIGN_TOP_LEFT, 0, 80);

    char meshlocal_prefix_str[OT_IP6_PREFIX_STRING_SIZE] = "None";
    if (dataset.mComponents.mIsMeshLocalPrefixPresent) {
        snprintf(meshlocal_prefix_str, OT_IP6_PREFIX_STRING_SIZE + 1, "%x:%x:%x:%x::/64",
                 (dataset.mMeshLocalPrefix.m8[0] << 8) | dataset.mMeshLocalPrefix.m8[1],
                 (dataset.mMeshLocalPrefix.m8[2] << 8) | dataset.mMeshLocalPrefix.m8[3],
                 (dataset.mMeshLocalPrefix.m8[4] << 8) | dataset.mMeshLocalPrefix.m8[5],
                 (dataset.mMeshLocalPrefix.m8[6] << 8) | dataset.mMeshLocalPrefix.m8[7]);
    }
    snprintf(txt, sizeof(txt), "Mesh Local Prefix: %s", meshlocal_prefix_str);
    br_m5stack_create_label(page, txt, &lv_font_montserrat_12, lv_color_black(), LV_ALIGN_TOP_LEFT, 0, 95);

    if (dataset.mComponents.mIsDelayPresent) {
        snprintf(txt, sizeof(txt), "Delay: %lu", dataset.mDelay);
    } else {
        snprintf(txt, sizeof(txt), "Delay: %s", "None");
    }
    br_m5stack_create_label(page, txt, &lv_font_montserrat_12, lv_color_black(), LV_ALIGN_TOP_LEFT, 0, 110);

    if (dataset.mComponents.mIsPanIdPresent) {
        snprintf(txt, sizeof(txt), "PAN ID: 0x%04x", dataset.mPanId);
    } else {
        snprintf(txt, sizeof(txt), "PAN ID: %s", "None");
    }
    br_m5stack_create_label(page, txt, &lv_font_montserrat_12, lv_color_black(), LV_ALIGN_TOP_LEFT, 0, 125);

    if (dataset.mComponents.mIsChannelPresent) {
        snprintf(txt, sizeof(txt), "Channel: %u", dataset.mChannel);
    } else {
        snprintf(txt, sizeof(txt), "Channel: %s", "None");
    }
    br_m5stack_create_label(page, txt, &lv_font_montserrat_12, lv_color_black(), LV_ALIGN_TOP_LEFT, 0, 140);

    if (dataset.mComponents.mIsWakeupChannelPresent) {
        snprintf(txt, sizeof(txt), "Wake-up Channel: %u", dataset.mWakeupChannel);
    } else {
        snprintf(txt, sizeof(txt), "Wake-up Channel: %s", "None");
    }
    br_m5stack_create_label(page, txt, &lv_font_montserrat_12, lv_color_black(), LV_ALIGN_TOP_LEFT, 0, 155);

    if (dataset.mComponents.mIsChannelMaskPresent) {
        snprintf(txt, sizeof(txt), "Channel Mask: 0x%08lx", dataset.mChannelMask);
    } else {
        snprintf(txt, sizeof(txt), "Channel Mask: %s", "None");
    }
    br_m5stack_create_label(page, txt, &lv_font_montserrat_12, lv_color_black(), LV_ALIGN_TOP_LEFT, 0, 170);

    char pskc_str[OT_PSKC_MAX_SIZE * 2 + 1] = "None";
    if (dataset.mComponents.mIsPskcPresent) {
        char *p = pskc_str;
        for (int i = 0; i < OT_PSKC_MAX_SIZE; i++) {
            p += snprintf(p, 3, "%02x", dataset.mPskc.m8[i]);
        }
    }
    snprintf(txt, sizeof(txt), "PSKc: %s", pskc_str);
    br_m5stack_create_label(page, txt, &lv_font_montserrat_12, lv_color_black(), LV_ALIGN_TOP_LEFT, 0, 185);

    char security_policy_str[24] = "None";
    if (dataset.mComponents.mIsSecurityPolicyPresent) {
        char option[9] = "";
        int idx = 0;
        if (dataset.mSecurityPolicy.mObtainNetworkKeyEnabled) {
            option[idx++] = 'o';
        }
        if (dataset.mSecurityPolicy.mNativeCommissioningEnabled) {
            option[idx++] = 'n';
        }
        if (dataset.mSecurityPolicy.mRoutersEnabled) {
            option[idx++] = 'r';
        }
        if (dataset.mSecurityPolicy.mExternalCommissioningEnabled) {
            option[idx++] = 'c';
        }
        if (dataset.mSecurityPolicy.mCommercialCommissioningEnabled) {
            option[idx++] = 'C';
        }
        if (dataset.mSecurityPolicy.mAutonomousEnrollmentEnabled) {
            option[idx++] = 'e';
        }
        if (dataset.mSecurityPolicy.mNetworkKeyProvisioningEnabled) {
            option[idx++] = 'p';
        }
        if (dataset.mSecurityPolicy.mNonCcmRoutersEnabled) {
            option[idx++] = 'R';
        }
        option[idx] = '\0';
        snprintf(security_policy_str, 24, "%u %s %u", dataset.mSecurityPolicy.mRotationTime, option,
                 dataset.mSecurityPolicy.mVersionThresholdForRouting);
    }
    snprintf(txt, sizeof(txt), "Security Policy: %s", security_policy_str);
    br_m5stack_create_label(page, txt, &lv_font_montserrat_12, lv_color_black(), LV_ALIGN_TOP_LEFT, 0, 200);
}

static void br_m5stack_thread_check_dataset(lv_event_t *e)
{
    esp_err_t ret = ESP_OK;
    otError err = OT_ERROR_NONE;
    br_m5stack_err_msg_t err_msg = "";
    lv_obj_t *dataset_page = NULL;
    lv_obj_t *exit_btn = NULL;
    otOperationalDataset dataset;

    esp_openthread_lock_acquire(portMAX_DELAY);
    err = otDatasetGetActive(esp_openthread_get_instance(), &dataset);
    esp_openthread_lock_release();

    BR_M5STACK_GOTO_ON_FALSE(err == OT_ERROR_NONE, exit,
                             "None active dataset\nclick 'random dataset'\nto generate a new one");
    dataset_page = br_m5stack_create_blank_page(s_thread_page);
    ESP_GOTO_ON_FALSE(dataset_page, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the dataset page");
    exit_btn = br_m5stack_create_button(40, 20, br_m5stack_delete_page_from_button, LV_EVENT_CLICKED, "exit", NULL);
    ESP_GOTO_ON_FALSE(exit_btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the exit button for dataset page");
    br_m5stack_add_btn_to_page(dataset_page, exit_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    show_dataset_on_page(dataset_page, dataset);

exit:
    if (ret == ESP_OK) {
        BR_M5STACK_CREATE_WARNING_IF_EXIST(1000);
    } else {
        BR_M5STACK_DELETE_OBJ_IF_EXIST(dataset_page);
    }
}

static void br_m5stack_thread_random_dataset(lv_event_t *e)
{
    br_m5stack_err_msg_t err_msg = "";
    otOperationalDataset dataset;

    esp_openthread_lock_acquire(portMAX_DELAY);
    BR_M5STACK_GOTO_ON_FALSE(otThreadGetDeviceRole(esp_openthread_get_instance()) == OT_DEVICE_ROLE_DISABLED, exit,
                             "Stop Thread before setting!");
    strcpy(err_msg, "Generated random dataset");
    BR_M5STACK_GOTO_ON_FALSE(otDatasetCreateNewNetwork(esp_openthread_get_instance(), &dataset) == OT_ERROR_NONE, exit,
                             "Failed to create new dataset");
    BR_M5STACK_GOTO_ON_FALSE(otDatasetSetActive(esp_openthread_get_instance(), &dataset) == OT_ERROR_NONE, exit,
                             "Failed to set dataset");

exit:
    esp_openthread_lock_release();
    BR_M5STACK_CREATE_WARNING_IF_EXIST(1000);
}

static void br_m5stack_thread_connect(lv_event_t *e)
{
    br_m5stack_err_msg_t err_msg = "";
    otOperationalDatasetTlvs dataset;
    otInstance *instance = NULL;
    otError error = OT_ERROR_NONE;
    otDeviceRole role = OT_DEVICE_ROLE_DISABLED;

    esp_openthread_lock_acquire(portMAX_DELAY);
    instance = esp_openthread_get_instance();
    role = otThreadGetDeviceRole(instance);
    BR_M5STACK_GOTO_ON_FALSE(role == OT_DEVICE_ROLE_DISABLED, exit,
                             (role == OT_DEVICE_ROLE_DETACHED) ? "Thread is connecting.." : "Thread is connected");
    error = otDatasetGetActiveTlvs(instance, &dataset);
    ESP_ERROR_CHECK(esp_openthread_auto_start((error == OT_ERROR_NONE) ? &dataset : NULL));

exit:
    esp_openthread_lock_release();
    BR_M5STACK_CREATE_WARNING_IF_EXIST(1000);
}

static void br_m5stack_thread_disconnect(lv_event_t *e)
{
    br_m5stack_err_msg_t err_msg = "";
    otInstance *instance = NULL;

    esp_openthread_lock_acquire(portMAX_DELAY);
    instance = esp_openthread_get_instance();
    BR_M5STACK_GOTO_ON_FALSE(otThreadSetEnabled(instance, false) == OT_ERROR_NONE, exit, "Failed to stop Thread");

exit:
    esp_openthread_lock_release();
    BR_M5STACK_CREATE_WARNING_IF_EXIST(1000);
}

lv_obj_t *br_m5stack_thread_ui_init(lv_obj_t *page)
{
    esp_err_t ret = ESP_OK;
    lv_obj_t *thread_page_btn = NULL;
    lv_obj_t *con_btn = NULL;
    lv_obj_t *discon_btn = NULL;
    lv_obj_t *set_channel_btn = NULL;
    lv_obj_t *set_networkkey_btn = NULL;
    lv_obj_t *set_panid_btn = NULL;
    lv_obj_t *random_dataset_btn = NULL;
    lv_obj_t *check_dataset_btn = NULL;
    lv_obj_t *exit_btn = NULL;
    otDeviceRole role = OT_DEVICE_ROLE_DISABLED;
    const char *role_str = NULL;
    otInstance *instance = NULL;

    s_thread_page = br_m5stack_create_blank_page(page);
    ESP_GOTO_ON_FALSE(s_thread_page, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the Thread page");

    esp_openthread_lock_acquire(portMAX_DELAY);
    instance = esp_openthread_get_instance();
    s_br_state = otBorderRoutingGetState(instance);
    role = otThreadGetDeviceRole(instance);
    role_str = otThreadDeviceRoleToString(role);
    esp_openthread_lock_release();

    s_br_state_label = br_m5stack_create_label(page, s_br_state_str[s_br_state], &lv_font_montserrat_16,
                                               lv_color_make(0xff, 0x0, 0x00), LV_ALIGN_TOP_MID, 0, 60);
    ESP_GOTO_ON_FALSE(s_br_state_label, ESP_FAIL, exit, BR_M5STACK_TAG,
                      "Failed to create the Thread Border Router state label");

    s_thread_role_label = br_m5stack_create_label(s_thread_page, role_str, &lv_font_montserrat_32, lv_color_black(),
                                                  LV_ALIGN_TOP_MID, 0, 30);
    ESP_GOTO_ON_FALSE(s_thread_role_label, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the Thread role label");

    thread_page_btn = br_m5stack_create_button(100, 60, br_m5stack_display_page_from_button, LV_EVENT_CLICKED, "Thread",
                                               (void *)s_thread_page);
    ESP_GOTO_ON_FALSE(thread_page_btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the Thread button");

    con_btn = br_m5stack_create_button(100, 50, br_m5stack_thread_connect, LV_EVENT_CLICKED, "connect", NULL);
    ESP_GOTO_ON_FALSE(con_btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the Thread connection button");
    br_m5stack_add_btn_to_page(s_thread_page, con_btn, LV_ALIGN_BOTTOM_LEFT, 0, -58);

    discon_btn = br_m5stack_create_button(100, 50, br_m5stack_thread_disconnect, LV_EVENT_CLICKED, "disconnect", NULL);
    ESP_GOTO_ON_FALSE(discon_btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the Thread disconnection button");
    br_m5stack_add_btn_to_page(s_thread_page, discon_btn, LV_ALIGN_BOTTOM_RIGHT, 0, -58);

    set_channel_btn = br_m5stack_create_button(100, 50, br_m5stack_thread_set_dataset, LV_EVENT_CLICKED, "set channel",
                                               (void *)BR_M5STACK_DATASET_CHANNEL);
    ESP_GOTO_ON_FALSE(set_channel_btn, ESP_FAIL, exit, BR_M5STACK_TAG,
                      "Failed to create the Thread Channel setting button");
    br_m5stack_add_btn_to_page(s_thread_page, set_channel_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    set_networkkey_btn = br_m5stack_create_button(100, 50, br_m5stack_thread_set_dataset, LV_EVENT_CLICKED,
                                                  "set networkkey", (void *)BR_M5STACK_DATASET_NETWORKKEY);
    ESP_GOTO_ON_FALSE(set_networkkey_btn, ESP_FAIL, exit, BR_M5STACK_TAG,
                      "Failed to create the Thread Network Key setting button");
    br_m5stack_add_btn_to_page(s_thread_page, set_networkkey_btn, LV_ALIGN_BOTTOM_MID, 0, 0);

    set_panid_btn = br_m5stack_create_button(100, 50, br_m5stack_thread_set_dataset, LV_EVENT_CLICKED, "set panid",
                                             (void *)BR_M5STACK_DATASET_PANID);
    ESP_GOTO_ON_FALSE(set_panid_btn, ESP_FAIL, exit, BR_M5STACK_TAG,
                      "Failed to create the Thread Pan ID setting button");
    br_m5stack_add_btn_to_page(s_thread_page, set_panid_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    random_dataset_btn =
        br_m5stack_create_button(100, 50, br_m5stack_thread_random_dataset, LV_EVENT_CLICKED, "random dataset", NULL);
    ESP_GOTO_ON_FALSE(random_dataset_btn, ESP_FAIL, exit, BR_M5STACK_TAG,
                      "Failed to create the Thread dataset generation button");
    br_m5stack_add_btn_to_page(s_thread_page, random_dataset_btn, LV_ALIGN_BOTTOM_MID, 0, -58);

    check_dataset_btn =
        br_m5stack_create_button(100, 50, br_m5stack_thread_check_dataset, LV_EVENT_CLICKED, "dataset", NULL);
    ESP_GOTO_ON_FALSE(check_dataset_btn, ESP_FAIL, exit, BR_M5STACK_TAG,
                      "Failed to create the Thread dataset check button");
    br_m5stack_add_btn_to_page(s_thread_page, check_dataset_btn, LV_ALIGN_BOTTOM_MID, 0, -116);

    exit_btn = br_m5stack_create_button(40, 20, br_m5stack_hidden_page_from_button, LV_EVENT_CLICKED, "exit", NULL);
    ESP_GOTO_ON_FALSE(exit_btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the exit button for Thread page");
    br_m5stack_add_btn_to_page(s_thread_page, exit_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    br_m5stack_hidden_page(s_thread_page);

    esp_openthread_lock_acquire(portMAX_DELAY);
    otSetStateChangedCallback(instance, thread_update_msg, NULL);
    esp_openthread_lock_release();

exit:
    if (ret != ESP_OK) {
        BR_M5STACK_DELETE_OBJ_IF_EXIST(thread_page_btn);
        BR_M5STACK_DELETE_OBJ_IF_EXIST(s_thread_page);
    }
    return thread_page_btn;
}

static void br_m5stack_thread_do_factoryreset(lv_event_t *e)
{
    esp_err_t ret = ESP_OK;
    (void)ret;
    br_m5stack_err_msg_t err_msg = "Failed to do factoryreset";
    nvs_handle_t ot_nvs_handle;
    esp_err_t err = ESP_OK;

    err = nvs_open("openthread", NVS_READWRITE, &ot_nvs_handle);
    ESP_GOTO_ON_ERROR(err, exit, BR_M5STACK_TAG, "Failed to open NVS namespace (0x%x)", err);
    err = nvs_erase_all(ot_nvs_handle);
    ESP_GOTO_ON_ERROR(err, exit, BR_M5STACK_TAG, "Failed to erase all OpenThread settings (0x%x)", err);
    nvs_close(ot_nvs_handle);
    esp_restart();

exit:
    BR_M5STACK_CREATE_WARNING_IF_EXIST(1000);
}

static void br_m5stack_thread_factoryreset_confirm(lv_event_t *e)
{
    esp_err_t ret = ESP_OK;
    lv_obj_t *factoryreset_page = NULL;
    lv_obj_t *label = NULL;
    lv_obj_t *yes_btn = NULL;
    lv_obj_t *no_btn = NULL;

    factoryreset_page = br_m5stack_create_blank_page(lv_layer_top());
    ESP_GOTO_ON_FALSE(factoryreset_page, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the factoryreset page");

    label = br_m5stack_create_label(factoryreset_page, "Are you sure to execute factory reset?", &lv_font_montserrat_16,
                                    lv_color_black(), LV_ALIGN_CENTER, 0, -20);
    ESP_GOTO_ON_FALSE(label, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the label for factoryreset page");

    yes_btn = br_m5stack_create_button(80, 40, br_m5stack_thread_do_factoryreset, LV_EVENT_CLICKED, "Yes", NULL);
    ESP_GOTO_ON_FALSE(yes_btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the yes button for factoryreset page");
    br_m5stack_add_btn_to_page(factoryreset_page, yes_btn, LV_ALIGN_LEFT_MID, 40, 60);

    no_btn = br_m5stack_create_button(80, 40, br_m5stack_delete_page_from_button, LV_EVENT_CLICKED, "No", NULL);
    ESP_GOTO_ON_FALSE(no_btn, ESP_FAIL, exit, BR_M5STACK_TAG, "Failed to create the no button for factoryreset page");
    br_m5stack_add_btn_to_page(factoryreset_page, no_btn, LV_ALIGN_RIGHT_MID, -40, 60);

exit:
    if (ret != ESP_OK) {
        BR_M5STACK_DELETE_OBJ_IF_EXIST(factoryreset_page);
    }
}

lv_obj_t *br_m5stack_create_factoryreset_button(lv_obj_t *page)
{
    lv_obj_t *btn = br_m5stack_create_button(100, 20, br_m5stack_thread_factoryreset_confirm, LV_EVENT_CLICKED,
                                             "factoryreset", NULL);
    ESP_RETURN_ON_FALSE(btn, NULL, BR_M5STACK_TAG, "Failed to create factoryreset button");
    return btn;
}