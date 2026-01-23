#include "PerceptionContextMeta.h"

#include <glib.h>
#include <memory>

// ============= Internal C API (Not exposed in header) =============

/**
 * Called when meta is initialized (buffer creation)
 */
static gboolean GstMetaPerceptionContext_init(GstMeta *meta, gpointer params, GstBuffer *buffer) {
    GstMetaPerceptionContext *m = (GstMetaPerceptionContext *)meta;
    m->perceptionContext = nullptr;
    m->lock = nullptr;
    return TRUE;
}

/**
 * Called when buffer is destroyed
 * Cleans up the PerceptionContext and mutex
 */
static void GstMetaPerceptionContext_free(GstMeta *meta, GstBuffer *buffer) {
    GstMetaPerceptionContext *m = (GstMetaPerceptionContext *)meta;

    if (m->perceptionContext) {
        delete m->perceptionContext;
        m->perceptionContext = nullptr;
    }

    if (m->lock) {
        g_mutex_free(m->lock);
        m->lock = nullptr;
    }
}

/**
 * Called when buffer is copied/transformed (e.g., through queues)
 * Deep copies the PerceptionContext to the destination buffer
 */
static gboolean GstMetaPerceptionContext_transform(
    GstBuffer *dest, GstMeta *meta, GstBuffer *src, GQuark type, gpointer data) {
    GstMetaPerceptionContext *m = (GstMetaPerceptionContext *)meta;
    GstMetaPerceptionContext *d = (GstMetaPerceptionContext *)gst_buffer_add_meta(
        dest, GST_META_PERCEPTION_CONTEXT_INFO, NULL);

    if (!d)
        return FALSE;

    // Lock source for reading
    if (m->lock)
        g_mutex_lock(m->lock);

    // Deep copy the PerceptionContext if it exists
    if (m->perceptionContext) {
        d->perceptionContext = new amp::PerceptionContext(*m->perceptionContext);
    }

    // Unlock source
    if (m->lock)
        g_mutex_unlock(m->lock);

    // Create new mutex for destination
    d->lock = g_mutex_new();

    return TRUE;
}

/**
 * Register the API type (thread-safe, one-time)
 */
GType GstMetaPerceptionContext_get_type(void) {
    static GType type = 0;
    if (g_once_init_enter(&type)) {
        const char *api_name = "GstMetaPerceptionContextAPI";
        GType t = g_type_from_name(api_name);
        if (!t) {
            static const gchar *tags[] = {"perception", "inference", "detections", NULL};
            t = gst_meta_api_type_register(api_name, tags);
        }
        g_once_init_leave(&type, t);
    }
    return type;
}

/**
 * Register the meta info (thread-safe, one-time)
 */
const GstMetaInfo *GstMetaPerceptionContext_get_info(void) {
    static const GstMetaInfo *mi = NULL;
    if (g_once_init_enter(&mi)) {
        const GstMetaInfo *info = gst_meta_register(GST_META_PERCEPTION_CONTEXT_TYPE,
                                                    "GstMetaPerceptionContext",
                                                    sizeof(GstMetaPerceptionContext),
                                                    GstMetaPerceptionContext_init,
                                                    GstMetaPerceptionContext_free,
                                                    GstMetaPerceptionContext_transform);
        g_once_init_leave(&mi, info);
    }
    return mi;
}

/**
 * Attach PerceptionContext to a buffer (internal)
 */
static GstMetaPerceptionContext *GstMetaPerceptionContext_attach(GstBuffer *buf,
                                                                 amp::PerceptionContext *context) {
    g_return_val_if_fail(GST_IS_BUFFER(buf), nullptr);
    g_return_val_if_fail(context != nullptr, nullptr);

    // Allocate meta structure
    GstMetaPerceptionContext *meta = (GstMetaPerceptionContext *)gst_buffer_add_meta(
        buf, GST_META_PERCEPTION_CONTEXT_INFO, nullptr);

    if (!meta)
        return nullptr;

    // Set context and create mutex
    meta->perceptionContext = context;
    meta->lock = g_mutex_new();

    if (!meta->lock) {
        GST_ERROR("Failed to create GMutex for PerceptionContext");
        return nullptr;
    }

    return meta;
}

/**
 * Retrieve PerceptionContext meta from a buffer (internal)
 */
static GstMetaPerceptionContext *GstMetaPerceptionContext_get(GstBuffer *buf) {
    return (GstMetaPerceptionContext *)gst_buffer_get_meta(buf, GST_META_PERCEPTION_CONTEXT_TYPE);
}

/**
 * Lock the context for exclusive access (internal)
 */
static void GstMetaPerceptionContext_lock(GstMetaPerceptionContext *meta) {
    if (meta && meta->lock) {
        g_mutex_lock(meta->lock);
    }
}

/**
 * Unlock the context (internal)
 */
static void GstMetaPerceptionContext_unlock(GstMetaPerceptionContext *meta) {
    if (meta && meta->lock) {
        g_mutex_unlock(meta->lock);
    }
}

// ============= Public C++ API =============

namespace amp {

PerceptionContextMeta::~PerceptionContextMeta() {
    if (gst_meta) {
        GstMetaPerceptionContext_unlock(gst_meta);
    }
}

amp::PerceptionContext *PerceptionContextMeta::context() const {
    if (gst_meta) {
        GstMetaPerceptionContext_lock(gst_meta);
        return gst_meta->perceptionContext;
    }
    return nullptr;
}

std::shared_ptr<PerceptionContextMeta>
PerceptionContextMeta::attach(GstBuffer *buf, amp::PerceptionContext *context) {

    GstMetaPerceptionContext *meta = GstMetaPerceptionContext_attach(buf, context);
    if (!meta) {
        GST_ERROR("Failed to attach PerceptionContext meta to buffer");
        delete context;
        return nullptr;
    }

    return std::shared_ptr<PerceptionContextMeta>(new PerceptionContextMeta(meta));
}

std::shared_ptr<PerceptionContextMeta> PerceptionContextMeta::get(GstBuffer *buf) {
    if (!buf) {
        return nullptr;
    }

    GstMetaPerceptionContext *meta = GstMetaPerceptionContext_get(buf);
    if (!meta) {
        return nullptr;
    }

    return std::shared_ptr<PerceptionContextMeta>(new PerceptionContextMeta(meta));
}

} // namespace amp
