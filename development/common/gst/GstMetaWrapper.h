/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <functional>
#include <gst/gst.h>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <variant>

namespace opk {

// Traits contract:
// struct MyTraits {
//   using Payload = ...;
//   static const char* api_name();   // unique
//   static const char* meta_name();  // unique
//   static const char* const* tags();// optional tags array, null-terminated
//   static Payload clone(const Payload&); // or std::unique_ptr clone
// };

// Shared even before the first read, so copies of a buffer reuse a lazy cache.
template <class ReadCache> struct MetaReadCache {
    std::mutex mutex;
    std::shared_ptr<const ReadCache> value;
};

enum class MetaError {
    OK,
    NO_METADATA,
};

// ReadCache must retain its shared_ptr<const Payload> while it may read the
// payload. It may release it after producing an independently owned result.
template <class Traits, class ReadCache = void> class Meta {
  public:
    using Payload = typename Traits::Payload;

  private:
    using CacheStorage = std::conditional_t<std::is_void_v<ReadCache>,
                                            std::monostate,
                                            std::shared_ptr<MetaReadCache<ReadCache>>>;
    struct MetaType {
        GstMeta meta;
        std::shared_ptr<Payload> payload;
        [[no_unique_address]] CacheStorage cache;
    };

  public:
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

    // ---- Attach / Presence / Remove ----
    // Store an owned value, not a caller's mutable shared_ptr. After attachment,
    // mutate() is the only supported write path; its callback must not retain
    // references to the payload. Pass std::move(p) to transfer an existing value.
    static GstMeta *add(GstBuffer *const buf, Payload p = Payload{}) {
        g_return_val_if_fail(GST_IS_BUFFER(buf), nullptr);
        auto payload = std::make_shared<Payload>(std::move(p));
        [[maybe_unused]] CacheStorage cache{};
        if constexpr (!std::is_void_v<ReadCache>)
            cache = std::make_shared<MetaReadCache<ReadCache>>();
        auto *m = (MetaType *)gst_buffer_add_meta(buf, info(), nullptr);
        if (!m) {
            return nullptr;
        }
        m->payload = std::move(payload);
        if constexpr (!std::is_void_v<ReadCache>)
            m->cache = std::move(cache);
        return &m->meta;
    }

    static bool has(GstBuffer *const buf) {
        return get(buf) != nullptr;
    }

    static bool remove(GstBuffer *const buf) {
        auto *const m = get(buf);
        return m && gst_buffer_remove_meta(buf, &m->meta);
    }

    // ---- Read-only access ----
    static std::shared_ptr<const Payload> read(GstBuffer *buf) {
        auto *m = get(buf);
        if (!m || !m->payload)
            return nullptr;
        return m->payload;
    }

    // The cache initially retains a read-only payload snapshot, just like read().
    // Acquiring it does not serialize. Concurrent readers share one instance.
    static std::shared_ptr<const ReadCache> readCached(GstBuffer *const buf)
        requires(!std::is_void_v<ReadCache>)
    {
        const auto *const m = get(buf);
        if (!m || !m->payload)
            return nullptr;
        const auto cache = m->cache;
        const std::scoped_lock lock(cache->mutex);
        if (!cache->value)
            cache->value = std::make_shared<ReadCache>(m->payload);
        return cache->value;
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

        // Mutations require exclusive buffer access, as before. Detach BEFORE
        // invoking fn (including if it throws). Other buffers and queued readers
        // keep their old snapshot and packet. No-op mutations invalidate too.
        if constexpr (!std::is_void_v<ReadCache>) {
            if (m->cache.use_count() != 1) {
                m->cache = std::make_shared<MetaReadCache<ReadCache>>();
            } else {
                m->cache->value.reset();
            }
        }

        // Copy-on-write: if shared, clone before modifying.
        if (m->payload.use_count() != 1) {
            auto cloned = std::make_shared<Payload>(Traits::clone(*m->payload));
            m->payload = std::move(cloned);
        }

        return std::move(fn(*m->payload));
    }

  private:
    static MetaType *get(GstBuffer *const buf) {
        g_return_val_if_fail(GST_IS_BUFFER(buf), nullptr);
        return (MetaType *)gst_buffer_get_meta(buf, api_type());
    }

    // init/free/transform for GstMeta
    static gboolean init(GstMeta *meta, gpointer, GstBuffer *) {
        auto *m = (MetaType *)meta;
        // GStreamer allocates raw memory; construct the C++ members without
        // overwriting the GstMeta header that GStreamer already initialized.
        if (m) {
            std::construct_at(&m->payload);
            if constexpr (!std::is_void_v<ReadCache>)
                std::construct_at(&m->cache);
            return TRUE;
        } else {
            return FALSE;
        }
    }

    static void free(GstMeta *meta, GstBuffer *) {
        auto *m = (MetaType *)meta;
        if (m) {
            if constexpr (!std::is_void_v<ReadCache>)
                std::destroy_at(&m->cache);
            std::destroy_at(&m->payload);
        }
    }

    static gboolean transform(GstBuffer *dest, GstMeta *meta, GstBuffer *, GQuark, gpointer) {
        auto src = reinterpret_cast<MetaType *>(meta);
        if (!src)
            return FALSE;

        auto *dst = (MetaType *)gst_buffer_add_meta(dest, info(), nullptr);
        if (!dst)
            return FALSE;
        dst->payload = src->payload;
        if constexpr (!std::is_void_v<ReadCache>)
            dst->cache = src->cache;
        return TRUE;
    }
};

} // namespace opk
