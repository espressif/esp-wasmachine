/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include "bh_common.h"
#include "bh_log.h"
#include "bh_platform.h"
#include "app_manager_export.h"
#include "module_wasm_app.h"
#include "bi-inc/attr_container.h"
#include "wasm_export.h"
#include "wasm_native.h"
#include "wasm_runtime_common.h"

#include "wm_ext_wasm_native_macro.h"
#include "wm_ext_wasm_native_export.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include "esp_memory_utils.h"
#include <esp_log.h>
#include <esp_event.h>
#include <mqtt_client.h>

static const char *TAG = "mqtt_wrapper";

#define MQTT_HOST                   "host"                      /*!< MQTT server domain (ipv4 as string) */
#define MQTT_URI                    "uri"                       /*!< MQTT broker URI (string) */
#define MQTT_PORT                   "port"                      /*!< MQTT server port ()*/
#define MQTT_CLIENT_ID              "client_id"                 /*!< MQTT client id (string) */
#define MQTT_USERNAME               "username"                  /*!< MQTT username (string) */
#define MQTT_PASSWORD               "password"                  /*!< MQTT password (string) */
#define MQTT_LWT_TOPIC              "lwt_topic"                 /*!< MQTT LWT (Last Will and Testament) message topic (string) */
#define MQTT_LWT_MSG                "lwt_msg"                   /*!< MQTT LWT message (string, NULL by default) */
#define MQTT_LWT_QOS                "lwt_qos"                   /*!< MQTT LWT message qos () */
#define MQTT_LWT_RETAIN             "lwt_retain"                /*!< MQTT LWT retained message flag () */
#define MQTT_LWT_MSG_LEN            "lwt_msg_len"               /*!< MQTT LWT message length () */
#define MQTT_DISABLE_CLEAN_SESSION  "disable_clean_session"     /*!< MQTT clean session (default clean_session is true) */
#define MQTT_KEEPALIVE              "keepalive"                 /*!< MQTT keepalive (default is 120 seconds) */
#define MQTT_DISABLE_AUTO_RECONNECT "disable_auto_reconnect"    /*!< MQTT client will reconnect to server (when errors/disconnect). Set disable_auto_reconnect=true to disable */
#define MQTT_CERT_PEM               "cert_pem"                  /*!< MQTT certificate data in PEM or DER format for server verify (with SSL, default is NULL), not required to verify the server. PEM-format must have a terminating NULL-character. DER-format requires the length to be passed in cert_len. */
#define MQTT_CERT_LEN               "cert_len"                  /*!< Length of the cert_pem. May be 0 for null-terminated pem () */
#define MQTT_CLIENT_CERT_PEM        "client_cert_pem"           /*!< MQTT certificate data in PEM or DER format for SSL mutual authentication, default is NULL, not required if mutual authentication is not needed. If it is not NULL, also `client_key_pem` has to be provided. PEM-format must have a terminating NULL-character. DER-format requires the length to be passed in client_cert_len. */
#define MQTT_CLIENT_CERT_LEN        "client_cert_len"           /*!< Length of the client_cert_pem. May be 0 for null-terminated pem () */
#define MQTT_CLIENT_KEY_PEM         "client_key_pem"            /*!< MQTT private key data in PEM or DER format for SSL mutual authentication, default is NULL, not required if mutual authentication is not needed. If it is not NULL, also `client_cert_pem` has to be provided. PEM-format must have a terminating NULL-character. DER-format requires the length to be passed in client_key_len */
#define MQTT_CLIENT_KEY_LEN         "client_key_len"            /*!< Length of the client_key_pem. May be 0 for null-terminated pem () */
#define MQTT_CLIENTKEY_PASSWORD     "clientkey_password"       /*!< MQTT Client key decryption password string (string) */
#define MQTT_CLIENTKEY_PASSWORD_LEN "clientkey_password_len"   /*!< Length of the clientkey_password () */
#define MQTT_URI_PATH               "path"                      /*!< MQTT Path in the URI (string) */
#define MQTT_EVENT_ATTR_ID          "event_id"                  /*!< MQTT event type */
#define MQTT_EVENT_ATTR_DATA        "data"                      /*!< MQTT Data associated with this event */
#define MQTT_EVENT_ATTR_DATA_LEN    "data_len"                  /*!< Length of the data for this event */
#define MQTT_EVENT_ATTR_TOTAL_LEN   "total_len"                 /*!< Total Length of the data for this event */
#define MQTT_EVENT_ATTR_DATA_OFFSET "offset"                    /*!< Actual offset for the data associated with this event */
#define MQTT_EVENT_ATTR_TOPIC       "topic"                     /*!< MQTT Topic associated with this event */
#define MQTT_EVENT_ATTR_TOPIC_LEN   "topic_len"                 /*!< Length of the topic for this event associated with this event */
#define MQTT_EVENT_ATTR_MSG_ID      "msg_id"                    /*!< MQTT messaged id of message */
#define MQTT_EVENT_ATTR_SESSION     "session"                   /*!< MQTT session_present flag for connection event */
#define MQTT_EVENT_ATTR_ERROR_CODE  "error_code"                /*!< MQTT error handle including esp-tls errors as well as internal mqtt errors */
#define MQTT_EVENT_ATTR_RETAIN      "retain"                    /*!< Retained flag of the message associated with this event */
#define MQTT_EVENT_ATTR_QOS         "qos"                       /*!< QoS of the messages associated with this event */
#define MQTT_EVENT_ATTR_DUP         "dup"                       /*!< dup flag of the message associated with this event */

typedef struct __mqtt_wrapper_ctx_t {
    /* Handle to interact with wasm app */
    uint32_t                            handle;

    /* Underlying mqtt context ID, may be socket fd */
    esp_mqtt_client_config_t            config;
    esp_mqtt_client_handle_t            client;

    /* Module id that the mqtt context belongs to */
    uint32_t                            module_id;
} mqtt_wrapper_ctx_t __attribute__((aligned(4)));

#define MQTT_EVENT_WASM  WASM_Msg_Start + 5

/* --- connection manager reference implementation ---*/

typedef struct mqtt_wrapper_event {
    uint32_t handle;                    /*!< MQTT handle for this event */
    char *data;                         /*!< Data associated with this event */
    uint32_t data_len;                  /*!< Length of the data for this event */
} mqtt_wrapper_event_t;

static void mqtt_wrapper_event_cleaner(mqtt_wrapper_event_t *conn_event)
{
    if (conn_event->data != NULL) {
        wasm_runtime_free(conn_event->data);
    }
    wasm_runtime_free(conn_event);
}

static void mqtt_wrapper_event_to_module(mqtt_wrapper_ctx_t *wrapper_mqtt_ctx, char *data, uint32_t len)
{
    module_data *module = module_data_list_lookup_id(wrapper_mqtt_ctx->module_id);
    char *data_copy = NULL;
    mqtt_wrapper_event_t *mqtt_wrapper_event;
    bh_message_t msg;

    if (module == NULL) {
        return;
    }

    mqtt_wrapper_event = (mqtt_wrapper_event_t *)wasm_runtime_malloc(sizeof(*mqtt_wrapper_event));
    if (mqtt_wrapper_event == NULL) {
        return;
    }

    if (len > 0) {
        data_copy = (char *)wasm_runtime_malloc(len);
        if (data_copy == NULL) {
            wasm_runtime_free(mqtt_wrapper_event);
            return;
        }
        bh_memcpy_s(data_copy, len, data, len);
    }

    memset(mqtt_wrapper_event, 0, sizeof(*mqtt_wrapper_event));
    mqtt_wrapper_event->handle = wrapper_mqtt_ctx->handle;
    mqtt_wrapper_event->data = data_copy;
    mqtt_wrapper_event->data_len = len;

    msg = bh_new_msg(MQTT_EVENT_WASM, mqtt_wrapper_event, sizeof(*mqtt_wrapper_event), mqtt_wrapper_event_cleaner);
    if (!msg) {
        mqtt_wrapper_event_cleaner(mqtt_wrapper_event);
        return;
    }

    bh_post_msg2(module->queue, msg);
}

static void mqtt_wrapper_event_callback(module_data *m_data, bh_message_t msg)
{
    uint32_t argv[3];
    wasm_function_inst_t func_on_conn_data;
    bh_assert(MQTT_EVENT_WASM == bh_message_type(msg));
    wasm_data *wasm_app_data = (wasm_data *)m_data->internal_data;
    wasm_module_inst_t inst = wasm_app_data->wasm_module_inst;
    mqtt_wrapper_event_t *conn_event = (mqtt_wrapper_event_t *)bh_message_payload(msg);
    int32 data_offset;

    if (conn_event == NULL) {
        return;
    }

    func_on_conn_data = wasm_runtime_lookup_function(inst, "on_mqtt_dispatch_event", "(i32i32i32)");
    if (!func_on_conn_data) {
        ESP_LOGE(TAG, "Cannot find function on_mqtt_dispatch_event");
        return;
    }

    /* 0 len means connection closed */
    if (conn_event->data_len == 0) {
        argv[0] = conn_event->handle;
        argv[1] = 0;
        argv[2] = 0;
        if (!wasm_runtime_call_wasm(wasm_app_data->exec_env, func_on_conn_data, 3, argv)) {
            const char *exception = wasm_runtime_get_exception(inst);
            bh_assert(exception);
            ESP_LOGE(TAG, ":Got exception running wasm code: %s", exception);
            wasm_runtime_clear_exception(inst);
            return;
        }
    } else {
        data_offset = wasm_runtime_module_dup_data(inst, conn_event->data, conn_event->data_len);
        if (data_offset == 0) {
            const char *exception = wasm_runtime_get_exception(inst);
            if (exception) {
                ESP_LOGE(TAG, "Got exception running wasm code: %s", exception);
                wasm_runtime_clear_exception(inst);
            }
            return;
        }

        argv[0] = conn_event->handle;
        argv[1] = (uint32_t)data_offset;
        argv[2] = conn_event->data_len;
        if (!wasm_runtime_call_wasm(wasm_app_data->exec_env, func_on_conn_data, 3, argv)) {
            const char *exception = wasm_runtime_get_exception(inst);
            bh_assert(exception);
            ESP_LOGE(TAG, ":Got exception running wasm code: %s", exception);
            wasm_runtime_clear_exception(inst);
            wasm_runtime_module_free(inst, data_offset);
            return;
        }
        wasm_runtime_module_free(inst, data_offset);
    }
}

static esp_err_t mqtt_event_handler_callback(void *handler_args, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    mqtt_wrapper_ctx_t *wrapper_mqtt_ctx = handler_args;
    if (!wrapper_mqtt_ctx) {
        return ESP_FAIL;
    }

    attr_container_t *args = NULL;
    args = attr_container_create("mqtt_attr");
    if (!args) {
        ESP_LOGE(TAG, "Failed to allocate memory for wrapper mqtt attribute container");
        return ESP_OK;
    }

    if (!attr_container_set_int(&args, MQTT_EVENT_ATTR_ID, event->event_id)) {
        goto fail;
    }

    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGD(TAG, "MQTT_EVENT_CONNECTED");
        ATTR_CONTAINER_SET_INT(MQTT_EVENT_ATTR_SESSION, event->session_present)
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
    case MQTT_EVENT_UNSUBSCRIBED:
    case MQTT_EVENT_PUBLISHED:
        ATTR_CONTAINER_SET_INT(MQTT_EVENT_ATTR_MSG_ID, event->msg_id)
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGD(TAG, "MQTT_EVENT_DATA");
        ATTR_CONTAINER_SET_INT(MQTT_EVENT_ATTR_MSG_ID, event->msg_id)
        ATTR_CONTAINER_SET_BOOL(MQTT_EVENT_ATTR_RETAIN, event->retain)
        ATTR_CONTAINER_SET_INT(MQTT_EVENT_ATTR_TOTAL_LEN, event->total_data_len)
        ATTR_CONTAINER_SET_INT(MQTT_EVENT_ATTR_DATA_OFFSET, event->current_data_offset)
        ATTR_CONTAINER_SET_INT(MQTT_EVENT_ATTR_QOS, event->qos)
        ATTR_CONTAINER_SET_BOOL(MQTT_EVENT_ATTR_DUP, event->dup)

        /* Topic can be NULL, for data longer than the MQTT buffer */
        if (event->topic) {
            ESP_LOGD(TAG, "TOPIC=%.*s\r\n", event->topic_len, event->topic);
            ATTR_CONTAINER_SET_STRING(MQTT_EVENT_ATTR_TOPIC, event->topic)
            ATTR_CONTAINER_SET_INT(MQTT_EVENT_ATTR_TOPIC_LEN, event->topic_len)
        }
        ESP_LOGD(TAG, "DATA=%.*s\r\n", event->data_len, event->data);
        ATTR_CONTAINER_SET_STRING(MQTT_EVENT_ATTR_DATA, event->data)
        ATTR_CONTAINER_SET_INT(MQTT_EVENT_ATTR_DATA_LEN, event->data_len)
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGD(TAG, "MQTT_EVENT_ERROR");
        if (!attr_container_set_bytearray(&args, MQTT_EVENT_ATTR_ERROR_CODE, (const int8_t *)event->error_handle, sizeof(esp_mqtt_error_codes_t))) {
            goto fail;
        }

        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGD(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGD(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
            ESP_LOGD(TAG, "Last captured errno : %d (%s)",  event->error_handle->esp_transport_sock_errno,
                     strerror(event->error_handle->esp_transport_sock_errno));
        } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGD(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
        } else {
            ESP_LOGD(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
        }
        break;
    default:
        ESP_LOGD(TAG, "Other event id:%d", event->event_id);
        break;
    }

    mqtt_wrapper_event_to_module(wrapper_mqtt_ctx, (char *)args, attr_container_get_serialize_length(args));

fail:
    attr_container_destroy(args);

    return ESP_OK;
}

static void wrapper_mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    /* The argument passed to esp_mqtt_client_register_event can de accessed as handler_args*/
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%"PRIu32"", base, event_id);
    mqtt_event_handler_callback(handler_args, event_data);
}

static bool wasm_mqtt_unpack(esp_mqtt_client_config_t *config, attr_container_t *args)
{
    bool container_set_flag = false;

    ATTR_CONTAINER_GET_STRING(MQTT_HOST, config->broker.address.hostname)
    ATTR_CONTAINER_GET_STRING(MQTT_URI, config->broker.address.uri)
    ATTR_CONTAINER_GET_UINT16(MQTT_PORT, config->broker.address.port)
    ATTR_CONTAINER_GET_STRING(MQTT_CLIENT_ID, config->credentials.client_id)
    ATTR_CONTAINER_GET_STRING(MQTT_USERNAME, config->credentials.username)
    ATTR_CONTAINER_GET_STRING(MQTT_PASSWORD, config->credentials.authentication.password)
    ATTR_CONTAINER_GET_STRING(MQTT_LWT_TOPIC, config->session.last_will.topic)
    ATTR_CONTAINER_GET_STRING(MQTT_LWT_MSG, config->session.last_will.msg)
    ATTR_CONTAINER_GET_INT(MQTT_LWT_QOS, config->session.last_will.qos)
    ATTR_CONTAINER_GET_INT(MQTT_LWT_RETAIN, config->session.last_will.retain)
    ATTR_CONTAINER_GET_INT(MQTT_LWT_MSG_LEN, config->session.last_will.msg_len)
    ATTR_CONTAINER_GET_BOOL(MQTT_DISABLE_CLEAN_SESSION, config->session.disable_clean_session)
    ATTR_CONTAINER_GET_INT(MQTT_KEEPALIVE, config->session.keepalive)
    ATTR_CONTAINER_GET_BOOL(MQTT_DISABLE_AUTO_RECONNECT, config->network.disable_auto_reconnect)
    ATTR_CONTAINER_GET_STRING(MQTT_CERT_PEM, config->broker.verification.certificate)
    ATTR_CONTAINER_GET_UINT16(MQTT_CERT_LEN, config->broker.verification.certificate_len)
    ATTR_CONTAINER_GET_STRING(MQTT_CLIENT_CERT_PEM, config->credentials.authentication.certificate)
    ATTR_CONTAINER_GET_UINT16(MQTT_CLIENT_CERT_LEN, config->credentials.authentication.certificate_len)
    ATTR_CONTAINER_GET_STRING(MQTT_CLIENT_KEY_PEM, config->credentials.authentication.key)
    ATTR_CONTAINER_GET_UINT16(MQTT_CLIENT_KEY_LEN, config->credentials.authentication.key_len)
    ATTR_CONTAINER_GET_STRING(MQTT_CLIENTKEY_PASSWORD, config->credentials.authentication.key_password)
    ATTR_CONTAINER_GET_UINT16(MQTT_CLIENTKEY_PASSWORD_LEN, config->credentials.authentication.key_password_len)
    ATTR_CONTAINER_GET_STRING(MQTT_URI_PATH, config->broker.address.path)
    container_set_flag = true;

fail:
    return container_set_flag;
}

static int wasm_mqtt_init_wrapper(wasm_exec_env_t exec_env, uint32_t *mqtt_handle, attr_container_t *args)
{
    mqtt_wrapper_ctx_t *wrapper_mqtt_ctx = NULL;
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    uint32_t module_id = app_manager_get_module_id(Module_WASM_App, module_inst);

    if (!args) {
        return ESP_FAIL;
    }

    bh_assert(module_id != ID_NONE);

    if (!validate_native_addr((void *)mqtt_handle, sizeof(uint32))
            || !validate_native_addr((void *)args, sizeof(attr_container_t))) {
        ESP_LOGE(TAG, "Failed to check for mqtt_wrapper_ctx_t or args by runtime");
        goto fail;
    }

    wrapper_mqtt_ctx = wasm_runtime_malloc(sizeof(mqtt_wrapper_ctx_t));
    if (!wrapper_mqtt_ctx) {
        ESP_LOGE(TAG, "Failed to allocate memory for mqtt_wrapper_ctx_t");
        goto fail;
    }

    memset(wrapper_mqtt_ctx, 0, sizeof(mqtt_wrapper_ctx_t));
    wrapper_mqtt_ctx->module_id = module_id;
    if (!wasm_mqtt_unpack(&wrapper_mqtt_ctx->config, args)) {
        ESP_LOGE(TAG, "Failed to unpack the attribute container to init mqtt configuration");
        goto fail;
    }

    wrapper_mqtt_ctx->client = esp_mqtt_client_init(&wrapper_mqtt_ctx->config);
    if (!wrapper_mqtt_ctx->client) {
        ESP_LOGE(TAG, "Failed to allocate memory for mqtt client");
        goto fail;
    }

    esp_mqtt_client_register_event(wrapper_mqtt_ctx->client, MQTT_EVENT_ANY, wrapper_mqtt_event_handler, wrapper_mqtt_ctx);
    if (!wasm_register_msg_callback(MQTT_EVENT_WASM, mqtt_wrapper_event_callback)) {
        goto fail;
    }

    wrapper_mqtt_ctx->handle = (uint32_t)wrapper_mqtt_ctx;
    *mqtt_handle = (uint32_t)wrapper_mqtt_ctx;

    return ESP_OK;

fail:
    if (wrapper_mqtt_ctx) {
        if (wrapper_mqtt_ctx->client) {
            esp_mqtt_client_destroy(wrapper_mqtt_ctx->client);
        }

        wasm_runtime_free(wrapper_mqtt_ctx);
    }

    return ESP_FAIL;
}

static int wasm_mqtt_destory_wrapper(wasm_exec_env_t exec_env, uint32_t mqtt_handle)
{
    mqtt_wrapper_ctx_t *wrapper_mqtt_ctx = (mqtt_wrapper_ctx_t *)mqtt_handle;
    if (!wrapper_mqtt_ctx) {
        return ESP_FAIL;
    }

    esp_mqtt_client_destroy(wrapper_mqtt_ctx->client);

    wasm_runtime_free(wrapper_mqtt_ctx);

    return ESP_OK;
}

static int wasm_mqtt_start_wrapper(wasm_exec_env_t exec_env, uint32_t mqtt_handle)
{
    mqtt_wrapper_ctx_t *wrapper_mqtt_ctx = (mqtt_wrapper_ctx_t *)mqtt_handle;
    if (!wrapper_mqtt_ctx) {
        return ESP_FAIL;
    }

    return esp_mqtt_client_start(wrapper_mqtt_ctx->client);
}

static int wasm_mqtt_stop_wrapper(wasm_exec_env_t exec_env, uint32_t mqtt_handle)
{
    mqtt_wrapper_ctx_t *wrapper_mqtt_ctx = (mqtt_wrapper_ctx_t *)mqtt_handle;
    if (!wrapper_mqtt_ctx) {
        return ESP_FAIL;
    }

    return esp_mqtt_client_stop(wrapper_mqtt_ctx->client);
}

static int wasm_mqtt_reconnect_wrapper(wasm_exec_env_t exec_env, uint32_t mqtt_handle)
{
    mqtt_wrapper_ctx_t *wrapper_mqtt_ctx = (mqtt_wrapper_ctx_t *)mqtt_handle;
    if (!wrapper_mqtt_ctx) {
        return ESP_FAIL;
    }

    return esp_mqtt_client_reconnect(wrapper_mqtt_ctx->client);
}

static int wasm_mqtt_disconnect_wrapper(wasm_exec_env_t exec_env, uint32_t mqtt_handle)
{
    mqtt_wrapper_ctx_t *wrapper_mqtt_ctx = (mqtt_wrapper_ctx_t *)mqtt_handle;
    if (!wrapper_mqtt_ctx) {
        return ESP_FAIL;
    }

    return esp_mqtt_client_disconnect(wrapper_mqtt_ctx->client);
}

static int wasm_mqtt_publish_wrapper(wasm_exec_env_t exec_env, uint32_t mqtt_handle, const char *topic, void *data, size_t data_len, uint8_t qos)
{
    mqtt_wrapper_ctx_t *wrapper_mqtt_ctx = (mqtt_wrapper_ctx_t *)mqtt_handle;
    if (!wrapper_mqtt_ctx) {
        return ESP_FAIL;
    }

    /* topic have been checked by runtime */
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    if (!validate_native_addr((void *)data, data_len)) {
        ESP_LOGE(TAG, "Failed to check for publish data by runtime");
        return ESP_FAIL;
    }

    return esp_mqtt_client_publish(wrapper_mqtt_ctx->client, topic, data, data_len, qos, 0);
}

static int wasm_mqtt_subscribe_wrapper(wasm_exec_env_t exec_env, uint32_t mqtt_handle, char *topic, uint8_t qos)
{
    mqtt_wrapper_ctx_t *wrapper_mqtt_ctx = (mqtt_wrapper_ctx_t *)mqtt_handle;
    if (!wrapper_mqtt_ctx) {
        return ESP_FAIL;
    }

    return esp_mqtt_client_subscribe(wrapper_mqtt_ctx->client, topic, qos);
}

static int wasm_mqtt_unsubscribe_wrapper(wasm_exec_env_t exec_env, uint32_t mqtt_handle, const char *topic)
{
    mqtt_wrapper_ctx_t *wrapper_mqtt_ctx = (mqtt_wrapper_ctx_t *)mqtt_handle;
    if (!wrapper_mqtt_ctx) {
        return ESP_FAIL;
    }

    return esp_mqtt_client_unsubscribe(wrapper_mqtt_ctx->client, topic);
}

static int wasm_mqtt_enqueue_wrapper(wasm_exec_env_t exec_env, uint32_t mqtt_handle, const char *topic, const void *data, int len, int qos, int retain, bool store)
{
    mqtt_wrapper_ctx_t *wrapper_mqtt_ctx = (mqtt_wrapper_ctx_t *)mqtt_handle;
    if (!wrapper_mqtt_ctx) {
        return ESP_FAIL;
    }

    /* topic have been checked by runtime */
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    if (!validate_native_addr((void *)data, len)) {
        ESP_LOGE(TAG, "Failed to check for enqueue data by runtime");
        return ESP_FAIL;
    }

    return esp_mqtt_client_enqueue(wrapper_mqtt_ctx->client, topic, data, len, qos, retain, store);
}

static int wasm_mqtt_set_uri_wrapper(wasm_exec_env_t exec_env, uint32_t mqtt_handle, const char *uri)
{
    mqtt_wrapper_ctx_t *wrapper_mqtt_ctx = (mqtt_wrapper_ctx_t *)mqtt_handle;
    if (!wrapper_mqtt_ctx) {
        return ESP_FAIL;
    }

    return esp_mqtt_client_set_uri(wrapper_mqtt_ctx->client, uri);
}

static int wasm_mqtt_config_wrapper(wasm_exec_env_t exec_env, uint32_t mqtt_handle, attr_container_t *args)
{
    mqtt_wrapper_ctx_t *wrapper_mqtt_ctx = (mqtt_wrapper_ctx_t *)mqtt_handle;
    if (!wrapper_mqtt_ctx || !args) {
        return ESP_FAIL;
    }

    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    if (!validate_native_addr((void *)args, sizeof(attr_container_t))) {
        ESP_LOGE(TAG, "Failed to check for args by runtime");
        return ESP_FAIL;
    }

    if (!wasm_mqtt_unpack(&wrapper_mqtt_ctx->config, args)) {
        ESP_LOGE(TAG, "Failed to unpack the attribute container to update mqtt configuration");
        return ESP_FAIL;
    }

    return esp_mqtt_set_config(wrapper_mqtt_ctx->client, &wrapper_mqtt_ctx->config);
}

static int wasm_mqtt_get_outbox_size_wrapper(wasm_exec_env_t exec_env, uint32_t mqtt_handle)
{
    mqtt_wrapper_ctx_t *wrapper_mqtt_ctx = (mqtt_wrapper_ctx_t *)mqtt_handle;
    if (!wrapper_mqtt_ctx) {
        return ESP_FAIL;
    }

    return esp_mqtt_client_get_outbox_size(wrapper_mqtt_ctx->client);
}

static NativeSymbol wm_mqtt_wrapper_native_symbol[] = {
    REG_NATIVE_FUNC(wasm_mqtt_init, "(**)i"),
    REG_NATIVE_FUNC(wasm_mqtt_destory, "(i)i"),
    REG_NATIVE_FUNC(wasm_mqtt_start, "(i)i"),
    REG_NATIVE_FUNC(wasm_mqtt_stop, "(i)i"),
    REG_NATIVE_FUNC(wasm_mqtt_reconnect, "(i)i"),
    REG_NATIVE_FUNC(wasm_mqtt_disconnect, "(i)i"),
    REG_NATIVE_FUNC(wasm_mqtt_publish, "(i$*ii)i"),
    REG_NATIVE_FUNC(wasm_mqtt_subscribe, "(i$i)i"),
    REG_NATIVE_FUNC(wasm_mqtt_unsubscribe, "(i$)i"),
    REG_NATIVE_FUNC(wasm_mqtt_enqueue, "(i$*iiii)i"),
    REG_NATIVE_FUNC(wasm_mqtt_set_uri, "(i$)i"),
    REG_NATIVE_FUNC(wasm_mqtt_config, "(i*)i"),
    REG_NATIVE_FUNC(wasm_mqtt_get_outbox_size, "(i)i")
};

int wm_ext_wasm_native_mqtt_export(void)
{
    NativeSymbol *sym = (NativeSymbol *)wm_mqtt_wrapper_native_symbol;
    int num = sizeof(wm_mqtt_wrapper_native_symbol) / sizeof(wm_mqtt_wrapper_native_symbol[0]);

    if (!wasm_native_register_natives("env", sym,  num)) {
        return -1;
    }

    return 0;
}
