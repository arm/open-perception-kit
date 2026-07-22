/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <modelfetch.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    (void)callback;
    (void)user_data;
    if (service == NULL || requests == NULL || out == NULL)
        return MODELFETCH_STATUS_INVALID_ARGUMENT;
    *out = NULL;
    if (error_out != NULL)
        *error_out = NULL;
    if (strcmp(fake_mode(), "api-failure") == 0)
        return fail_with(error_out, "fake access failure");

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
