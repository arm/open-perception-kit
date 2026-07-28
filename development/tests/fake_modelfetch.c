/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

/*
 * Backend test double below modelfetch's real header-only C++ API.
 * Keep this file and the owning tests aligned with model-loading changes, and
 * keep its exported C ABI surface exactly equal to the production imports.
 */

#include <modelfetch.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>

struct modelfetch_service {
    int unused;
};

struct modelfetch_config {
    const uint8_t *collection_slug;
    size_t collection_slug_length;
    modelfetch_token_mode_t token_mode;
    const uint8_t *explicit_token;
    size_t explicit_token_length;
};

struct modelfetch_request {
    char *asset_id;
    size_t asset_id_length;
    char *destination;
    size_t destination_length;
};

struct modelfetch_request_list {
    char *asset_id;
    size_t asset_id_length;
    char *destination;
    size_t destination_length;
};

struct modelfetch_integrity {
    const char *token;
    size_t token_length;
};

struct modelfetch_outcome {
    char *asset_id;
    size_t asset_id_length;
    char *path;
    size_t path_length;
    modelfetch_outcome_kind_t kind;
    modelfetch_success_status_t success_status;
    modelfetch_failure_reason_t failure_reason;
    struct modelfetch_integrity integrity;
};

struct modelfetch_outcome_list {
    struct modelfetch_outcome outcome;
    size_t count;
};

struct modelfetch_error {
    char *message;
    size_t message_length;
};

struct modelfetch_big_uint {
    const char *decimal;
    size_t decimal_length;
};

struct modelfetch_progress_event {
    const char *artifact_id;
    size_t artifact_id_length;
    modelfetch_progress_state_t state;
    struct modelfetch_big_uint transferred_bytes;
    struct modelfetch_big_uint total_bytes;
    uint8_t bytes_present;
    double percentage;
    uint8_t percentage_present;
};

static const char VALID_INTEGRITY[] =
    "sha256:0000000000000000000000000000000000000000000000000000000000000000";
static const char MISMATCHED_ASSET[] =
    "hf:Arm/mismatched@0000000000000000000000000000000000000000#file=model.onnx";

static char *copy_bytes(const uint8_t *value, size_t length) {
    if (length == SIZE_MAX)
        return NULL;

    char *copy = malloc(length + 1U);
    if (copy == NULL)
        return NULL;
    for (size_t index = 0U; index < length; ++index)
        copy[index] = (char)value[index];
    copy[length] = '\0';
    return copy;
}

static size_t bounded_string_length(const char *value, size_t maximum_length) {
    for (size_t length = 0U; length <= maximum_length; ++length) {
        if (value[length] == '\0')
            return length;
    }
    return SIZE_MAX;
}

static char *join_path(const char *destination,
                       size_t destination_length,
                       const char *relative,
                       size_t relative_length,
                       size_t *path_length) {
    if (path_length == NULL)
        return NULL;
    if (relative_length > SIZE_MAX - 2U || destination_length > SIZE_MAX - relative_length - 2U)
        return NULL;

    const size_t length = destination_length + relative_length + 1U;
    char *path = malloc(length + 1U);
    if (path == NULL)
        return NULL;

    for (size_t index = 0U; index < destination_length; ++index)
        path[index] = destination[index];
    path[destination_length] = '/';
    for (size_t index = 0U; index < relative_length; ++index)
        path[destination_length + 1U + index] = relative[index];
    path[length] = '\0';
    *path_length = length;
    return path;
}

static modelfetch_status_t
fail_with(modelfetch_error_t **error_out, const char *message, size_t message_length) {
    if (error_out != NULL) {
        *error_out = calloc(1U, sizeof(**error_out));
        if (*error_out != NULL) {
            (*error_out)->message = copy_bytes((const uint8_t *)message, message_length);
            if ((*error_out)->message == NULL) {
                free(*error_out);
                *error_out = NULL;
                return MODELFETCH_STATUS_INTERNAL_PANIC;
            }
            (*error_out)->message_length = message_length;
        }
    }
    return MODELFETCH_STATUS_ACCESS_ERROR;
}

static const char *fake_mode(void) {
    const char *mode = getenv("PEK_MODELFETCH_FAKE_MODE");
    return mode == NULL ? "downloaded" : mode;
}

static void sleep_for_progress_poll(void) {
    const struct timespec interval = {
        0,
        10L * 1000L * 1000L,
    };
    thrd_sleep(&interval, NULL);
}

static modelfetch_status_t write_marker(const char *environment_name, const char *contents) {
    const char *marker_path = getenv(environment_name);
    if (marker_path == NULL)
        return MODELFETCH_STATUS_OK;

    FILE *marker = fopen(marker_path, "w");
    if (marker == NULL)
        return MODELFETCH_STATUS_INTERNAL_PANIC;
    fputs(contents, marker);
    fclose(marker);
    return MODELFETCH_STATUS_OK;
}

static bool explicit_token_matches(const modelfetch_config_t *config, const char *expected_token) {
    if (config->token_mode != MODELFETCH_TOKEN_EXPLICIT)
        return false;

    size_t index = 0U;
    while (index < config->explicit_token_length && expected_token[index] != '\0') {
        if ((uint8_t)expected_token[index] != config->explicit_token[index])
            return false;
        ++index;
    }
    return index == config->explicit_token_length && expected_token[index] == '\0';
}

static modelfetch_status_t record_download_call(void) {
    const char *calls_path = getenv("PEK_MODELFETCH_FAKE_CALLS");
    if (calls_path == NULL)
        return MODELFETCH_STATUS_OK;

    FILE *calls = fopen(calls_path, "a");
    if (calls == NULL)
        return MODELFETCH_STATUS_INTERNAL_PANIC;
    fputs("call\n", calls);
    fclose(calls);
    return MODELFETCH_STATUS_OK;
}

static modelfetch_status_t emit_progress(modelfetch_progress_callback_t callback,
                                         void *user_data,
                                         const modelfetch_progress_event_t *event) {
    if (callback != NULL && callback(event, user_data) == MODELFETCH_CALLBACK_ABORT)
        return MODELFETCH_STATUS_CALLBACK_ABORTED;
    return MODELFETCH_STATUS_OK;
}

static modelfetch_status_t emit_download_progress(const char *mode,
                                                  modelfetch_progress_callback_t callback,
                                                  void *user_data,
                                                  const modelfetch_progress_event_t *event,
                                                  modelfetch_error_t **error_out) {
    if (strcmp(mode, "blocking") != 0)
        return emit_progress(callback, user_data, event);

    if (callback == NULL) {
        static const char message[] = "blocking fake requires a progress callback";
        return fail_with(error_out, message, sizeof(message) - 1U);
    }

    for (;;) {
        sleep_for_progress_poll();
        const modelfetch_status_t status = emit_progress(callback, user_data, event);
        if (status != MODELFETCH_STATUS_OK)
            return status;
    }
}

static modelfetch_status_t copy_outcome_asset(const modelfetch_request_list_t *requests,
                                              const char *mode,
                                              modelfetch_outcome_t *outcome) {
    const char *asset_id = requests->asset_id;
    size_t asset_id_length = requests->asset_id_length;
    if (strcmp(mode, "mismatched-asset") == 0) {
        asset_id = MISMATCHED_ASSET;
        asset_id_length = sizeof(MISMATCHED_ASSET) - 1U;
    }

    outcome->asset_id = copy_bytes((const uint8_t *)asset_id, asset_id_length);
    outcome->asset_id_length = asset_id_length;
    return outcome->asset_id == NULL ? MODELFETCH_STATUS_INTERNAL_PANIC : MODELFETCH_STATUS_OK;
}

static modelfetch_status_t copy_outcome_path(const modelfetch_request_list_t *requests,
                                             const char *mode,
                                             modelfetch_outcome_t *outcome) {
    const char *escape_path = getenv("PEK_MODELFETCH_FAKE_ESCAPE_PATH");
    if (strcmp(mode, "escape") == 0 && escape_path != NULL) {
        const size_t escape_path_length = bounded_string_length(escape_path, FILENAME_MAX);
        if (escape_path_length == SIZE_MAX)
            return MODELFETCH_STATUS_INVALID_ARGUMENT;
        outcome->path = copy_bytes((const uint8_t *)escape_path, escape_path_length);
        outcome->path_length = escape_path_length;
        return outcome->path == NULL ? MODELFETCH_STATUS_INTERNAL_PANIC : MODELFETCH_STATUS_OK;
    }

    static const char file_fragment[] = "#file=";
    static const char fallback_file[] = "model.onnx";
    const char *relative = strstr(requests->asset_id, file_fragment);
    size_t relative_length = sizeof(fallback_file) - 1U;
    if (relative == NULL) {
        relative = fallback_file;
    } else {
        relative += sizeof(file_fragment) - 1U;
        relative_length = requests->asset_id_length - (size_t)(relative - requests->asset_id);
    }

    outcome->path = join_path(requests->destination,
                              requests->destination_length,
                              relative,
                              relative_length,
                              &outcome->path_length);
    return outcome->path == NULL ? MODELFETCH_STATUS_INTERNAL_PANIC : MODELFETCH_STATUS_OK;
}

static modelfetch_status_t populate_outcome(const modelfetch_request_list_t *requests,
                                            const char *mode,
                                            modelfetch_outcome_t *outcome) {
    outcome->kind = strcmp(mode, "failure-outcome") == 0 ? MODELFETCH_OUTCOME_FAILURE
                                                         : MODELFETCH_OUTCOME_SUCCESS;
    outcome->success_status = MODELFETCH_SUCCESS_DOWNLOADED;
    outcome->failure_reason = MODELFETCH_FAILURE_INTEGRITY_MISMATCH;
    if (strcmp(mode, "invalid-integrity") == 0) {
        static const char invalid_integrity[] = "sha256:invalid";
        outcome->integrity.token = invalid_integrity;
        outcome->integrity.token_length = sizeof(invalid_integrity) - 1U;
    } else {
        outcome->integrity.token = VALID_INTEGRITY;
        outcome->integrity.token_length = sizeof(VALID_INTEGRITY) - 1U;
    }

    const modelfetch_status_t asset_status = copy_outcome_asset(requests, mode, outcome);
    if (asset_status != MODELFETCH_STATUS_OK)
        return asset_status;
    return copy_outcome_path(requests, mode, outcome);
}

modelfetch_status_t modelfetch_config_new(const uint8_t *collection_ptr,
                                          size_t collection_len,
                                          modelfetch_token_mode_t token_mode,
                                          const uint8_t *token_ptr,
                                          size_t token_len,
                                          modelfetch_config_t **out,
                                          modelfetch_error_t **error_out) {
    if (out == NULL || collection_ptr == NULL || collection_len == 0U)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    if (token_mode != MODELFETCH_TOKEN_CONFIGURED && token_mode != MODELFETCH_TOKEN_ANONYMOUS &&
        token_mode != MODELFETCH_TOKEN_EXPLICIT)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    if ((token_mode == MODELFETCH_TOKEN_EXPLICIT && (token_ptr == NULL || token_len == 0U)) ||
        (token_mode != MODELFETCH_TOKEN_EXPLICIT && token_len != 0U))
        return MODELFETCH_STATUS_INVALID_ARGUMENT;

    *out = calloc(1U, sizeof(**out));
    if (*out == NULL)
        return MODELFETCH_STATUS_INTERNAL_PANIC;
    (*out)->collection_slug = collection_ptr;
    (*out)->collection_slug_length = collection_len;
    (*out)->token_mode = token_mode;
    (*out)->explicit_token = token_ptr;
    (*out)->explicit_token_length = token_len;
    if (error_out != NULL)
        *error_out = NULL;
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_config_collection_slug(const modelfetch_config_t *value,
                                                      modelfetch_text_view_t *out) {
    if (value == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    out->ptr = value->collection_slug;
    out->len = value->collection_slug_length;
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_config_token_mode(const modelfetch_config_t *value,
                                                 modelfetch_token_mode_t *out) {
    if (value == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    *out = value->token_mode;
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_config_explicit_token(const modelfetch_config_t *value,
                                                     uint8_t *present_out,
                                                     modelfetch_text_view_t *out) {
    if (value == NULL || present_out == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    *present_out = value->token_mode == MODELFETCH_TOKEN_EXPLICIT ? 1U : 0U;
    out->ptr = value->explicit_token;
    out->len = value->explicit_token_length;
    return MODELFETCH_STATUS_OK;
}

void modelfetch_config_free(modelfetch_config_t *value) {
    free(value);
}

modelfetch_status_t modelfetch_service_new_with_config(const modelfetch_config_t *config,
                                                       modelfetch_service_t **out,
                                                       modelfetch_error_t **error_out) {
    if (config == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    const char *mode =
        config->token_mode == MODELFETCH_TOKEN_ANONYMOUS ? "anonymous\n" : "explicit\n";
    const modelfetch_status_t marker_status = write_marker("PEK_MODELFETCH_FAKE_TOKEN_MODE", mode);
    if (marker_status != MODELFETCH_STATUS_OK)
        return marker_status;

    const char *expected_token = getenv("PEK_MODELFETCH_FAKE_EXPECTED_TOKEN");
    if (expected_token != NULL && !explicit_token_matches(config, expected_token))
        return fail_with(error_out, "unexpected explicit token", 25U);

    *out = calloc(1U, sizeof(**out));
    if (*out == NULL)
        return MODELFETCH_STATUS_INTERNAL_PANIC;
    if (error_out != NULL)
        *error_out = NULL;
    return MODELFETCH_STATUS_OK;
}

void modelfetch_service_free(modelfetch_service_t *value) {
    free(value);
}

modelfetch_status_t modelfetch_request_new(const uint8_t *asset_id_ptr,
                                           size_t asset_id_len,
                                           const uint8_t *destination_ptr,
                                           size_t destination_len,
                                           modelfetch_request_t **out,
                                           modelfetch_error_t **error_out) {
    if (out == NULL || asset_id_ptr == NULL || destination_ptr == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    *out = calloc(1U, sizeof(**out));
    if (*out == NULL)
        return MODELFETCH_STATUS_INTERNAL_PANIC;
    (*out)->asset_id = copy_bytes(asset_id_ptr, asset_id_len);
    (*out)->asset_id_length = asset_id_len;
    (*out)->destination = copy_bytes(destination_ptr, destination_len);
    (*out)->destination_length = destination_len;
    if ((*out)->asset_id == NULL || (*out)->destination == NULL) {
        modelfetch_request_free(*out);
        *out = NULL;
        return MODELFETCH_STATUS_INTERNAL_PANIC;
    }
    if (error_out != NULL)
        *error_out = NULL;
    return MODELFETCH_STATUS_OK;
}

void modelfetch_request_free(modelfetch_request_t *value) {
    if (value == NULL)
        return;
    free(value->asset_id);
    free(value->destination);
    free(value);
}

modelfetch_status_t modelfetch_request_asset_id(const modelfetch_request_t *value,
                                                modelfetch_text_view_t *out) {
    if (value == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    out->ptr = (const uint8_t *)value->asset_id;
    out->len = value->asset_id_length;
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_request_destination(const modelfetch_request_t *value,
                                                   modelfetch_text_view_t *out) {
    if (value == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    out->ptr = (const uint8_t *)value->destination;
    out->len = value->destination_length;
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_request_list_new(modelfetch_request_list_t **out,
                                                modelfetch_error_t **error_out) {
    if (out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    *out = calloc(1U, sizeof(**out));
    if (error_out != NULL)
        *error_out = NULL;
    return *out == NULL ? MODELFETCH_STATUS_INTERNAL_PANIC : MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_request_list_append(modelfetch_request_list_t *list,
                                                   const modelfetch_request_t *request,
                                                   modelfetch_error_t **error_out) {
    if (list == NULL || request == NULL || list->asset_id != NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    list->asset_id = copy_bytes((const uint8_t *)request->asset_id, request->asset_id_length);
    list->asset_id_length = request->asset_id_length;
    list->destination =
        copy_bytes((const uint8_t *)request->destination, request->destination_length);
    list->destination_length = request->destination_length;
    if (list->asset_id == NULL || list->destination == NULL)
        return MODELFETCH_STATUS_INTERNAL_PANIC;
    if (error_out != NULL)
        *error_out = NULL;
    return MODELFETCH_STATUS_OK;
}

void modelfetch_request_list_free(modelfetch_request_list_t *value) {
    if (value == NULL)
        return;
    free(value->asset_id);
    free(value->destination);
    free(value);
}

modelfetch_status_t
modelfetch_service_download_asset_requests(const modelfetch_service_t *service,
                                           const modelfetch_request_list_t *requests,
                                           modelfetch_progress_callback_t callback,
                                           void *user_data,
                                           modelfetch_outcome_list_t **out,
                                           modelfetch_error_t **error_out) {
    if (service == NULL || requests == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    *out = NULL;
    if (error_out != NULL)
        *error_out = NULL;
    const modelfetch_status_t entered_status =
        write_marker("PEK_MODELFETCH_FAKE_ENTERED", "entered\n");
    if (entered_status != MODELFETCH_STATUS_OK)
        return entered_status;
    const modelfetch_status_t record_status = record_download_call();
    if (record_status != MODELFETCH_STATUS_OK)
        return record_status;
    const char *mode = fake_mode();
    if (strcmp(mode, "api-failure") == 0) {
        static const char message[] = "fake access failure";
        return fail_with(error_out, message, sizeof(message) - 1U);
    }

    struct modelfetch_progress_event progress = {
        .artifact_id = requests->asset_id,
        .artifact_id_length = requests->asset_id_length,
        .state = MODELFETCH_PROGRESS_IN_PROGRESS,
        .transferred_bytes = {"0", 1U},
        .total_bytes = {"0", 1U},
        .bytes_present = 0U,
        .percentage = 0.0,
        .percentage_present = 0U,
    };

    const modelfetch_status_t progress_status =
        emit_download_progress(mode, callback, user_data, &progress, error_out);
    if (progress_status != MODELFETCH_STATUS_OK)
        return progress_status;

    *out = calloc(1U, sizeof(**out));
    if (*out == NULL)
        return MODELFETCH_STATUS_INTERNAL_PANIC;
    (*out)->count = strcmp(mode, "multiple-results") == 0 ? 2U : 1U;
    const modelfetch_status_t outcome_status = populate_outcome(requests, mode, &(*out)->outcome);
    if (outcome_status != MODELFETCH_STATUS_OK) {
        modelfetch_outcome_list_free(*out);
        *out = NULL;
        return outcome_status;
    }

    return MODELFETCH_STATUS_OK;
}

void modelfetch_outcome_list_free(modelfetch_outcome_list_t *value) {
    if (value == NULL)
        return;
    free(value->outcome.asset_id);
    free(value->outcome.path);
    free(value);
}

modelfetch_status_t modelfetch_outcome_list_count(const modelfetch_outcome_list_t *value,
                                                  size_t *out) {
    if (value == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    *out = value->count;
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_outcome_list_get(const modelfetch_outcome_list_t *value,
                                                size_t index,
                                                const modelfetch_outcome_t **out) {
    if (value == NULL || out == NULL || index >= value->count)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    *out = &value->outcome;
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_outcome_asset_id(const modelfetch_outcome_t *value,
                                                modelfetch_text_view_t *out) {
    if (value == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    out->ptr = (const uint8_t *)value->asset_id;
    out->len = value->asset_id_length;
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_outcome_kind(const modelfetch_outcome_t *value,
                                            modelfetch_outcome_kind_t *out) {
    if (value == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    *out = value->kind;
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_outcome_success_status(const modelfetch_outcome_t *value,
                                                      modelfetch_success_status_t *out) {
    if (value == NULL || out == NULL || value->kind != MODELFETCH_OUTCOME_SUCCESS)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    *out = value->success_status;
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_outcome_success_path_count(const modelfetch_outcome_t *value,
                                                          size_t *out) {
    if (value == NULL || out == NULL || value->kind != MODELFETCH_OUTCOME_SUCCESS)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    *out = strcmp(fake_mode(), "multiple-paths") == 0 ? 2U : 1U;
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_outcome_success_path(const modelfetch_outcome_t *value,
                                                    size_t index,
                                                    modelfetch_text_view_t *out) {
    const size_t count = strcmp(fake_mode(), "multiple-paths") == 0 ? 2U : 1U;
    if (value == NULL || out == NULL || index >= count || value->kind != MODELFETCH_OUTCOME_SUCCESS)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    out->ptr = (const uint8_t *)value->path;
    out->len = value->path_length;
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_outcome_success_integrity(const modelfetch_outcome_t *value,
                                                         size_t index,
                                                         const modelfetch_integrity_t **out) {
    const size_t count = strcmp(fake_mode(), "multiple-paths") == 0 ? 2U : 1U;
    if (value == NULL || out == NULL || index >= count || value->kind != MODELFETCH_OUTCOME_SUCCESS)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    *out = &value->integrity;
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_outcome_failure_reason(const modelfetch_outcome_t *value,
                                                      modelfetch_failure_reason_t *out) {
    if (value == NULL || out == NULL || value->kind != MODELFETCH_OUTCOME_FAILURE)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    *out = value->failure_reason;
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_integrity_token(const modelfetch_integrity_t *value,
                                               modelfetch_text_view_t *out) {
    if (value == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    out->ptr = (const uint8_t *)value->token;
    out->len = value->token_length;
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_error_message(const modelfetch_error_t *value,
                                             modelfetch_text_view_t *out) {
    if (value == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    out->ptr = (const uint8_t *)value->message;
    out->len = value->message_length;
    return MODELFETCH_STATUS_OK;
}

void modelfetch_error_free(modelfetch_error_t *value) {
    if (value == NULL)
        return;
    free(value->message);
    free(value);
}

modelfetch_status_t modelfetch_progress_event_artifact_id(const modelfetch_progress_event_t *value,
                                                          modelfetch_text_view_t *out) {
    if (value == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    out->ptr = (const uint8_t *)value->artifact_id;
    out->len = value->artifact_id_length;
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_progress_event_state(const modelfetch_progress_event_t *value,
                                                    modelfetch_progress_state_t *out) {
    if (value == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    *out = value->state;
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t
modelfetch_progress_event_transferred_bytes(const modelfetch_progress_event_t *value,
                                            uint8_t *present_out,
                                            const modelfetch_big_uint_t **out) {
    if (value == NULL || present_out == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    *present_out = value->bytes_present;
    *out = value->bytes_present != 0U ? &value->transferred_bytes : NULL;
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_progress_event_total_bytes(const modelfetch_progress_event_t *value,
                                                          uint8_t *present_out,
                                                          const modelfetch_big_uint_t **out) {
    if (value == NULL || present_out == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    *present_out = value->bytes_present;
    *out = value->bytes_present != 0U ? &value->total_bytes : NULL;
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_progress_event_percentage(const modelfetch_progress_event_t *value,
                                                         uint8_t *present_out,
                                                         double *out) {
    if (value == NULL || present_out == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    *present_out = value->percentage_present;
    *out = value->percentage;
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_big_uint_decimal(const modelfetch_big_uint_t *value,
                                                modelfetch_text_view_t *out) {
    if (value == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    out->ptr = (const uint8_t *)value->decimal;
    out->len = value->decimal_length;
    return MODELFETCH_STATUS_OK;
}
