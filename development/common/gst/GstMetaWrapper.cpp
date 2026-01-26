#include "PerceptionContextMeta.h"

#include <glib.h>
#include <memory>

namespace amp {

template <typename T, typename Traits>
gboolean
GstMetaWrapper<T, Traits>::init(GstMeta *meta, gpointer /*params*/, GstBuffer * /*buffer*/) {
    auto *m = reinterpret_cast<MetaType *>(meta);
    m->payload = nullptr;
    m->lock = nullptr;
    return TRUE;
}

template <typename T, typename Traits>
void GstMetaWrapper<T, Traits>::free(GstMeta *meta, GstBuffer * /*buffer*/) {
    auto *m = reinterpret_cast<MetaType *>(meta);

    if (m->payload) {
        delete m->payload;
        m->payload = nullptr;
    }

    if (m->lock) {
        g_mutex_free(m->lock);
        m->lock = nullptr;
    }
}

template <typename T, typename Traits>
gboolean GstMetaWrapper<T, Traits>::transform(
    GstBuffer *dest, GstMeta *meta, GstBuffer * /*src*/, GQuark /*type*/, gpointer /*data*/) {
    auto *sourceMeta = reinterpret_cast<MetaType *>(meta);
    auto *destMeta = reinterpret_cast<MetaType *>(
        gst_buffer_add_meta(dest, GstMetaWrapper<T, Traits>::get_info(), nullptr));

    if (!destMeta)
        return FALSE;

    if (sourceMeta->lock)
        g_mutex_lock(sourceMeta->lock);

    if (sourceMeta->payload) {
        destMeta->payload = new T(*sourceMeta->payload);
    }

    if (sourceMeta->lock)
        g_mutex_unlock(sourceMeta->lock);

    destMeta->lock = g_mutex_new();

    return TRUE;
}

template <typename T, typename Traits>
typename GstMetaWrapper<T, Traits>::MetaType *GstMetaWrapper<T, Traits>::attach_meta(GstBuffer *buf,
                                                                                     T *context) {
    g_return_val_if_fail(GST_IS_BUFFER(buf), nullptr);
    g_return_val_if_fail(context != nullptr, nullptr);

    auto *meta = reinterpret_cast<MetaType *>(
        gst_buffer_add_meta(buf, GstMetaWrapper<T, Traits>::get_info(), nullptr));

    if (!meta)
        return nullptr;

    meta->payload = context;
    meta->lock = g_mutex_new();

    if (!meta->lock) {
        GST_ERROR("Failed to create GMutex for meta");
        return nullptr;
    }

    return meta;
}

template <typename T, typename Traits>
typename GstMetaWrapper<T, Traits>::MetaType *GstMetaWrapper<T, Traits>::get_meta(GstBuffer *buf) {
    return reinterpret_cast<MetaType *>(
        gst_buffer_get_meta(buf, GstMetaWrapper<T, Traits>::get_type()));
}

template <typename T, typename Traits> void GstMetaWrapper<T, Traits>::lock(MetaType *meta) {
    if (meta && meta->lock) {
        g_mutex_lock(meta->lock);
    }
}

template <typename T, typename Traits> void GstMetaWrapper<T, Traits>::unlock(MetaType *meta) {
    if (meta && meta->lock) {
        g_mutex_unlock(meta->lock);
    }
}

template <typename T, typename Traits> GType GstMetaWrapper<T, Traits>::get_type() {
    static GType type = 0;
    if (g_once_init_enter(&type)) {
        const char *api_name = Traits::api_name();
        GType t = g_type_from_name(api_name);
        if (!t) {
            t = gst_meta_api_type_register(api_name, Traits::tags());
        }
        g_once_init_leave(&type, t);
    }
    return type;
}

template <typename T, typename Traits> const GstMetaInfo *GstMetaWrapper<T, Traits>::get_info() {
    static const GstMetaInfo *mi = NULL;
    if (g_once_init_enter(&mi)) {
        const GstMetaInfo *info = gst_meta_register(GstMetaWrapper<T, Traits>::get_type(),
                                                    Traits::meta_name(),
                                                    sizeof(MetaType),
                                                    GstMetaWrapper<T, Traits>::init,
                                                    GstMetaWrapper<T, Traits>::free,
                                                    GstMetaWrapper<T, Traits>::transform);
        g_once_init_leave(&mi, info);
    }
    return mi;
}

template <typename T, typename Traits> GstMetaWrapper<T, Traits>::~GstMetaWrapper() {
    if (gst_meta) {
        GstMetaWrapper<T, Traits>::unlock(gst_meta);
    }
}

template <typename T, typename Traits> T *GstMetaWrapper<T, Traits>::get_payload() const {
    if (gst_meta) {
        GstMetaWrapper<T, Traits>::lock(gst_meta);
        return gst_meta->payload;
    }
    return nullptr;
}

template <typename T, typename Traits>
const T *GstMetaWrapper<T, Traits>::get_const_payload() const {
    if (gst_meta) {
        GstMetaWrapper<T, Traits>::lock(gst_meta);
        return gst_meta->payload;
    }
    return nullptr;
}

template <typename T, typename Traits>
std::shared_ptr<GstMetaWrapper<T, Traits>> GstMetaWrapper<T, Traits>::attach(GstBuffer *buf,
                                                                             T *context) {

    MetaType *meta = GstMetaWrapper<T, Traits>::attach_meta(buf, context);
    if (!meta) {
        GST_ERROR("Failed to attach meta to buffer");
        delete context;
        return nullptr;
    }

    return std::shared_ptr<GstMetaWrapper<T, Traits>>(new GstMetaWrapper(meta));
}

template <typename T, typename Traits>
std::shared_ptr<GstMetaWrapper<T, Traits>> GstMetaWrapper<T, Traits>::get(GstBuffer *buf) {
    if (!buf) {
        return nullptr;
    }

    MetaType *meta = GstMetaWrapper<T, Traits>::get_meta(buf);
    if (!meta) {
        return nullptr;
    }

    return std::shared_ptr<GstMetaWrapper<T, Traits>>(new GstMetaWrapper(meta));
}

} // namespace amp