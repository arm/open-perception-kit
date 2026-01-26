#pragma once

#include <memory>

template <typename T> struct GstMetaContainer {
    GstMeta meta;
    T *payload;
    GMutex *lock; // Thread-safe access
};

namespace amp {

template <typename T, typename Traits> class GstMetaWrapper {
  public:
    using MetaType = GstMetaContainer<T>;

    static std::shared_ptr<GstMetaWrapper> get(GstBuffer *buf);
    static std::shared_ptr<GstMetaWrapper> attach(GstBuffer *buf, T *context);

    GstMetaWrapper(const GstMetaWrapper &) = delete;
    GstMetaWrapper &operator=(const GstMetaWrapper &) = delete;
    GstMetaWrapper(GstMetaWrapper &&other) noexcept : gst_meta(other.gst_meta) {
        other.gst_meta = nullptr;
    }

    GstMetaWrapper &operator=(GstMetaWrapper &&other) noexcept {
        gst_meta = other.gst_meta;
        other.gst_meta = nullptr;
        return *this;
    }

    ~GstMetaWrapper();

    T *get_payload() const;
    const T *get_const_payload() const;

    static GType get_type();
    static const GstMetaInfo *get_info();

  private:
    explicit GstMetaWrapper(MetaType *meta) : gst_meta(meta) {}

    static gboolean init(GstMeta *meta, gpointer params, GstBuffer *buffer);
    static void free(GstMeta *meta, GstBuffer *buffer);
    static gboolean
    transform(GstBuffer *dest, GstMeta *meta, GstBuffer *src, GQuark type, gpointer data);
    static MetaType *attach_meta(GstBuffer *buf, T *context);
    static MetaType *get_meta(GstBuffer *buf);
    static void lock(MetaType *meta);
    static void unlock(MetaType *meta);

    MetaType *gst_meta;
};

} // namespace amp
