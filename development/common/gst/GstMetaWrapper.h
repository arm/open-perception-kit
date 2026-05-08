/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <functional>
#include <gst/gst.h>
#include <memory>
#include <utility>
#include <variant>

namespace pek {

// Traits contract:
// struct MyTraits {
//   using Payload = ...;
//   static const char* api_name();   // unique
//   static const char* meta_name();  // unique
//   static const char* const* tags();// optional tags array, null-terminated
//   static Payload clone(const Payload&); // or std::unique_ptr clone
// };

template <class Traits> struct MetaContainer {
    GstMeta meta;
    std::shared_ptr<typename Traits::Payload> payload;
};

enum class MetaError {
    OK,
    NO_METADATA,
};

template <class Traits> class Meta {
  public:
    using Payload = typename Traits::Payload;
    using MetaType = MetaContainer<Traits>;

    // ---- Registration ----
    static GType api_type() {
        static gsize once = 0;
        static GType type = 0;

        if (g_once_init_enter(&once)) {
            const char *name = Traits::api_name().data();

            // 1) If it already exists, use it.
            if (GType existing = g_type_from_name(name); existing != 0) {
                type = existing;
                g_once_init_leave(&once, 1);
                return type;
            }

            // 2) Otherwise register it.
            GType t = gst_meta_api_type_register(name, Traits::tags().data());

            // 3) If registration failed for some reason, try lookup again.
            if (t == 0)
                t = g_type_from_name(name);

            type = t; // may still be 0 if totally broken, but usually won’t be.
            g_once_init_leave(&once, 1);
        }

        return type;
    }

    static const GstMetaInfo *info() {
        static const GstMetaInfo *mi = nullptr;

        if (g_once_init_enter_pointer(&mi)) {
            const char *name = Traits::meta_name().data();

            const GstMetaInfo *i = gst_meta_get_info(name);

            if (!i) {
                i = gst_meta_register(
                    api_type(), name, sizeof(MetaType), &Meta::init, &Meta::free, &Meta::transform);

                if (!i) {
                    // Maybe another .so registered it between get_info() and register().
                    i = gst_meta_get_info(name);
                }
            }

            if (!i) {
                g_error("Failed to register GstMetaInfo '%s'", name);
            }

            g_once_init_leave_pointer(&mi, const_cast<GstMetaInfo *>(i));
        }

        return mi;
    }

    // ---- Attach / Get ----
    static MetaType *add(GstBuffer *buf, std::shared_ptr<Payload> p) {
        g_return_val_if_fail(GST_IS_BUFFER(buf), nullptr);
        auto *m = (MetaType *)gst_buffer_add_meta(buf, info(), nullptr);
        if (!m) {
            return nullptr;
        }
        m->payload = std::move(p);
        return m;
    }

    static MetaType *get(GstBuffer *buf) {
        g_return_val_if_fail(GST_IS_BUFFER(buf), nullptr);
        return (MetaType *)gst_buffer_get_meta(buf, api_type());
    }

    // ---- Read-only access ----
    static std::shared_ptr<const Payload> read(GstBuffer *buf) {
        auto *m = get(buf);
        if (!m || !m->payload)
            return nullptr;
        return m->payload;
    }

    // ---- Mutating access with copy-on-write ----
    template <class R>
    static std::variant<R, MetaError> mutate(GstBuffer *buf,
                                             std::function<R(typename Traits::Payload &)> &&fn) {
        // You should call gst_buffer_make_writable() in the element before mutating meta.
        auto *m = get(buf);
        if (!m || !m->payload) {
            return MetaError::NO_METADATA;
        }

        // Copy-on-write: if shared, clone before modifying.
        if (!m->payload.unique()) {
            auto cloned = std::make_shared<Payload>(Traits::clone(*m->payload));
            m->payload = std::move(cloned);
        }

        return std::move(fn(*m->payload));
    }

  private:
    // init/free/transform for GstMeta
    static gboolean init(GstMeta *meta, gpointer, GstBuffer *) {
        auto *m = (MetaType *)meta;
        // placement-new not needed for shared_ptr because MetaType is a C++ type and
        // default-initialized. But GStreamer allocates raw memory; we must construct it.
        if (m) {
            new (&m->payload) std::shared_ptr<Payload>();
            return TRUE;
        } else {
            return FALSE;
        }
    }

    static void free(GstMeta *meta, GstBuffer *) {
        auto *m = (MetaType *)meta;
        if (m) {
            m->payload.reset();
            m->payload.~shared_ptr();
        }
    }

    static gboolean transform(GstBuffer *dest, GstMeta *meta, GstBuffer *, GQuark, gpointer) {
        auto src = reinterpret_cast<MetaType *>(meta);
        auto *dst = (MetaType *)gst_buffer_add_meta(dest, info(), nullptr);
        if (!dst && !src)
            return FALSE;
        dst->payload.~shared_ptr();
        new (&dst->payload) std::shared_ptr<Payload>(src->payload); // shallow copy
        return TRUE;
    }
};

} // namespace pek
