/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <modelfetch.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>

struct modelfetch_service {
    int unused;
};

struct modelfetch_request {
    char *asset_id;
    char *destination;
};

struct modelfetch_request_list {
    char *asset_id;
    char *destination;
};

struct modelfetch_integrity {
    const char *token;
};

struct modelfetch_outcome {
    char *asset_id;
    char *path;
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
};

struct modelfetch_big_uint {
    const char *decimal;
};

struct modelfetch_progress_event {
    const char *artifact_id;
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
    char *copy = malloc(length + 1U);
    if (copy == NULL)
        return NULL;
    if (length != 0U)
        memcpy(copy, value, length);
    copy[length] = '\0';
    return copy;
}

static char *copy_string(const char *value) {
    return copy_bytes((const uint8_t *)value, strlen(value));
}

static modelfetch_status_t fail_with(modelfetch_error_t **error_out, const char *message) {
    if (error_out != NULL) {
        *error_out = calloc(1U, sizeof(**error_out));
        if (*error_out != NULL)
            (*error_out)->message = copy_string(message);
    }
    return MODELFETCH_STATUS_ACCESS_ERROR;
}

static const char *fake_mode(void) {
    const char *mode = getenv("PEK_MODELFETCH_FAKE_MODE");
    return mode == NULL ? "downloaded" : mode;
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

modelfetch_status_t modelfetch_service_new(modelfetch_service_t **out,
                                           modelfetch_error_t **error_out) {
    if (out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    *out = calloc(1U, sizeof(**out));
    if (error_out != NULL)
        *error_out = NULL;
    return *out == NULL ? MODELFETCH_STATUS_INTERNAL_PANIC : MODELFETCH_STATUS_OK;
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
    (*out)->destination = copy_bytes(destination_ptr, destination_len);
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
    out->len = strlen(value->asset_id);
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_request_destination(const modelfetch_request_t *value,
                                                   modelfetch_text_view_t *out) {
    if (value == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    out->ptr = (const uint8_t *)value->destination;
    out->len = strlen(value->destination);
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
    list->asset_id = copy_string(request->asset_id);
    list->destination = copy_string(request->destination);
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
    const modelfetch_status_t record_status = record_download_call();
    if (record_status != MODELFETCH_STATUS_OK)
        return record_status;
    if (strcmp(fake_mode(), "api-failure") == 0)
        return fail_with(error_out, "fake access failure");

    struct modelfetch_progress_event progress = {
        requests->asset_id,
        MODELFETCH_PROGRESS_STARTED,
        {"0"},
        {"10"},
        1U,
        0.0,
        1U,
    };
    modelfetch_status_t progress_status = emit_progress(callback, user_data, &progress);
    if (progress_status != MODELFETCH_STATUS_OK)
        return progress_status;

    progress.state = MODELFETCH_PROGRESS_IN_PROGRESS;
    progress.transferred_bytes.decimal = "5";
    progress.percentage = 50.0;

    if (strcmp(fake_mode(), "blocking") == 0) {
        const char *started_path = getenv("PEK_MODELFETCH_FAKE_STARTED");
        if (started_path != NULL) {
            FILE *started = fopen(started_path, "w");
            if (started == NULL)
                return MODELFETCH_STATUS_INTERNAL_PANIC;
            fputs("started\n", started);
            fclose(started);
        }
        if (callback == NULL)
            return fail_with(error_out, "blocking fake requires a progress callback");

        const struct timespec interval = {0, 10000000L};
        for (;;) {
            thrd_sleep(&interval, NULL);
            progress_status = emit_progress(callback, user_data, &progress);
            if (progress_status != MODELFETCH_STATUS_OK)
                return progress_status;
        }
    }

    progress_status = emit_progress(callback, user_data, &progress);
    if (progress_status != MODELFETCH_STATUS_OK)
        return progress_status;

    *out = calloc(1U, sizeof(**out));
    if (*out == NULL)
        return MODELFETCH_STATUS_INTERNAL_PANIC;
    (*out)->count = strcmp(fake_mode(), "multiple-results") == 0 ? 2U : 1U;
    (*out)->outcome.kind = strcmp(fake_mode(), "failure-outcome") == 0 ? MODELFETCH_OUTCOME_FAILURE
                                                                       : MODELFETCH_OUTCOME_SUCCESS;
    (*out)->outcome.success_status = strcmp(fake_mode(), "existing") == 0
                                         ? MODELFETCH_SUCCESS_EXISTING
                                         : MODELFETCH_SUCCESS_DOWNLOADED;
    (*out)->outcome.failure_reason = MODELFETCH_FAILURE_INTEGRITY_MISMATCH;
    (*out)->outcome.integrity.token =
        strcmp(fake_mode(), "invalid-integrity") == 0 ? "sha256:invalid" : VALID_INTEGRITY;
    (*out)->outcome.asset_id = copy_string(requests->asset_id);
    if (strcmp(fake_mode(), "invalid-utf8") == 0 && (*out)->outcome.asset_id != NULL) {
        (*out)->outcome.asset_id[0] = (char)0xff;
    } else if (strcmp(fake_mode(), "mismatched-asset") == 0) {
        free((*out)->outcome.asset_id);
        (*out)->outcome.asset_id = copy_string(MISMATCHED_ASSET);
    }

    const char *escape_path = getenv("PEK_MODELFETCH_FAKE_ESCAPE_PATH");
    if (strcmp(fake_mode(), "escape") == 0 && escape_path != NULL) {
        (*out)->outcome.path = copy_string(escape_path);
    } else {
        const char *relative = strstr(requests->asset_id, "#file=");
        relative = relative == NULL ? "model.onnx" : relative + strlen("#file=");
        const size_t length = strlen(requests->destination) + 1U + strlen(relative);
        (*out)->outcome.path = malloc(length + 1U);
        if ((*out)->outcome.path != NULL)
            snprintf((*out)->outcome.path, length + 1U, "%s/%s", requests->destination, relative);
    }

    if ((*out)->outcome.asset_id == NULL || (*out)->outcome.path == NULL) {
        modelfetch_outcome_list_free(*out);
        *out = NULL;
        return MODELFETCH_STATUS_INTERNAL_PANIC;
    }

    progress.state = MODELFETCH_PROGRESS_COMPLETED;
    progress.transferred_bytes.decimal = "10";
    progress.percentage = 100.0;
    progress_status = emit_progress(callback, user_data, &progress);
    if (progress_status != MODELFETCH_STATUS_OK) {
        modelfetch_outcome_list_free(*out);
        *out = NULL;
        return progress_status;
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
    out->len = strlen(value->asset_id);
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
    if (value == NULL || out == NULL || index != 0U || value->kind != MODELFETCH_OUTCOME_SUCCESS)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    out->ptr = (const uint8_t *)value->path;
    out->len = strlen(value->path);
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_outcome_success_integrity(const modelfetch_outcome_t *value,
                                                         size_t index,
                                                         const modelfetch_integrity_t **out) {
    if (value == NULL || out == NULL || index != 0U || value->kind != MODELFETCH_OUTCOME_SUCCESS)
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
    out->len = strlen(value->token);
    return MODELFETCH_STATUS_OK;
}

modelfetch_status_t modelfetch_error_message(const modelfetch_error_t *value,
                                             modelfetch_text_view_t *out) {
    if (value == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    out->ptr = (const uint8_t *)value->message;
    out->len = strlen(value->message);
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
    out->len = strlen(value->artifact_id);
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
    out->len = strlen(value->decimal);
    return MODELFETCH_STATUS_OK;
}
