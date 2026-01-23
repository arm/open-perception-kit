#pragma once

#include "amp/PerceptionContext.h"
#include <gst/gst.h>
#include <memory>

G_BEGIN_DECLS

typedef struct _GstMetaPerceptionContext {
    GstMeta meta;

    amp::PerceptionContext *perceptionContext;
    GMutex *lock; // Thread-safe access

} GstMetaPerceptionContext;

GType GstMetaPerceptionContext_get_type(void);
const GstMetaInfo *GstMetaPerceptionContext_get_info(void);

#define GST_META_PERCEPTION_CONTEXT_TYPE (GstMetaPerceptionContext_get_type())
#define GST_META_PERCEPTION_CONTEXT_INFO (GstMetaPerceptionContext_get_info())

G_END_DECLS

namespace amp {

class PerceptionContextMeta {
  public:
    /**
     * Get PerceptionContext from a GStreamer buffer
     * @param buf GStreamer buffer
     * @return Shared pointer to wrapper, or nullptr if not found
     */
    static std::shared_ptr<PerceptionContextMeta> get(GstBuffer *buf);

    /**
     * Attach PerceptionContext to a GStreamer buffer
     * @param buf GStreamer buffer
     * @param context Newly allocated PerceptionContext (takes ownership)
     * @return Shared pointer to wrapper, or nullptr on failure
     */
    static std::shared_ptr<PerceptionContextMeta> attach(GstBuffer *buf,
                                                         amp::PerceptionContext *context);

    // Deleted copy - each wrapper is tied to a specific buffer
    PerceptionContextMeta(const PerceptionContextMeta &) = delete;
    PerceptionContextMeta &operator=(const PerceptionContextMeta &) = delete;

    PerceptionContextMeta(PerceptionContextMeta &&other) noexcept : gst_meta(other.gst_meta) {
        other.gst_meta = nullptr;
    }

    PerceptionContextMeta &operator=(PerceptionContextMeta &&other) noexcept {
        gst_meta = other.gst_meta;
        other.gst_meta = nullptr;
        return *this;
    }

    ~PerceptionContextMeta();

    amp::PerceptionContext *context() const;

  private:
    explicit PerceptionContextMeta(GstMetaPerceptionContext *meta) : gst_meta(meta) {}

    GstMetaPerceptionContext *gst_meta;
};

} // namespace amp
