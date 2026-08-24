/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.

#include "perception_python_bridge.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace perception::python_bridge {

struct external_key_access {
    static container::external_key_t from_value(detail::id_t value) noexcept {
        return container::external_key_t(value);
    }
};

namespace {

struct live_envelope_object {
    PyObject_HEAD container::envelope *envelope;
    bool valid;
};

live_envelope_object *as_live_envelope(PyObject *object) noexcept;

struct native_proxy_anchor {
    virtual ~native_proxy_anchor() = default;
};

template <typename T> struct payload_anchor final : native_proxy_anchor {
    explicit payload_anchor(container::payload_ref<T> ref_in) : ref(std::move(ref_in)) {}

    container::payload_ref<T> ref;
};

using native_proxy_anchor_ptr = std::shared_ptr<const native_proxy_anchor>;

template <typename T> struct native_proxy_object {
    PyObject_HEAD const T *value;
    alignas(native_proxy_anchor_ptr)
        std::array<std::byte, sizeof(native_proxy_anchor_ptr)> anchor_storage;
};

template <typename T>
native_proxy_anchor_ptr *proxy_anchor_storage(native_proxy_object<T> *proxy) noexcept {
    return reinterpret_cast<native_proxy_anchor_ptr *>(proxy->anchor_storage.data());
}

template <typename T>
const native_proxy_anchor_ptr *proxy_anchor_storage(const native_proxy_object<T> *proxy) noexcept {
    return reinterpret_cast<const native_proxy_anchor_ptr *>(proxy->anchor_storage.data());
}

template <typename T>
native_proxy_anchor_ptr *proxy_anchor_ptr(native_proxy_object<T> *proxy) noexcept {
    return std::launder(proxy_anchor_storage(proxy));
}

template <typename T>
const native_proxy_anchor_ptr *proxy_anchor_ptr(const native_proxy_object<T> *proxy) noexcept {
    return std::launder(proxy_anchor_storage(proxy));
}

template <typename T> native_proxy_anchor_ptr proxy_anchor(PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<T> *>(self);
    return *proxy_anchor_ptr(proxy);
}

template <typename T> void native_proxy_dealloc(PyObject *self) {
    auto *proxy = reinterpret_cast<native_proxy_object<T> *>(self);
    std::destroy_at(proxy_anchor_ptr(proxy));
    proxy->value = nullptr;
    Py_TYPE(self)->tp_free(self);
}

template <typename T>
PyObject *make_native_proxy(PyTypeObject &type,
                            const T *value,
                            std::shared_ptr<const native_proxy_anchor> anchor) {
    using proxy_type = native_proxy_object<T>;
    auto *object = PyObject_New(proxy_type, &type);
    if (object == nullptr) {
        return nullptr;
    }
    object->value = value;
    std::construct_at(proxy_anchor_storage(object), std::move(anchor));
    return reinterpret_cast<PyObject *>(object);
}

template <typename VectorT>
PyObject *make_native_vector_proxy(PyTypeObject &type,
                                   const VectorT *value,
                                   std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<VectorT>(type, value, std::move(anchor));
}

PyTypeObject make_py_type_object() {
    struct type_head {
        PyVarObject ob_base;
    };
    type_head head = {PyVarObject_HEAD_INIT(nullptr, 0)};
    PyTypeObject type{};
    type.ob_base = head.ob_base;
    return type;
}

template <typename Function> PyCFunction py_c_function(Function function) {
    return reinterpret_cast<PyCFunction>(reinterpret_cast<void (*)()>(function));
}

PyObject *py_string_from_std(std::string_view value) {
    return PyUnicode_FromStringAndSize(value.data(), static_cast<Py_ssize_t>(value.size()));
}

template <typename T> PyObject *py_long_from_signed(T value) {
    return PyLong_FromLongLong(static_cast<long long>(value));
}

template <typename T> PyObject *py_long_from_unsigned(T value) {
    return PyLong_FromUnsignedLongLong(static_cast<unsigned long long>(value));
}

template <typename T> PyObject *py_long_from_enum(T value) {
    using underlying_type = std::underlying_type_t<T>;
    if constexpr (std::is_signed_v<underlying_type>) {
        return py_long_from_signed(static_cast<underlying_type>(value));
    } else {
        return py_long_from_unsigned(static_cast<underlying_type>(value));
    }
}

PyTypeObject &pytype_perception_metadata_BoxDetection() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_perception_metadata_BoxDetection_proxy(const perception::metadata::BoxDetectionT *value,
                                            std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_BoxDetections() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_perception_metadata_BoxDetections_proxy(const perception::metadata::BoxDetectionsT *value,
                                             std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_ClassificationCandidate() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *make_perception_metadata_ClassificationCandidate_proxy(
    const perception::metadata::ClassificationCandidateT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_Classification() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_perception_metadata_Classification_proxy(const perception::metadata::ClassificationT *value,
                                              std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_PersonPresence() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_perception_metadata_PersonPresence_proxy(const perception::metadata::PersonPresenceT *value,
                                              std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_Classifications() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_perception_metadata_Classifications_proxy(const perception::metadata::ClassificationsT *value,
                                               std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_ObjectMeta() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_perception_metadata_ObjectMeta_proxy(const perception::metadata::ObjectMetaT *value,
                                          std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_LayerInfo() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_perception_metadata_LayerInfo_proxy(const perception::metadata::LayerInfoT *value,
                                         std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_BoundingBox() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_perception_metadata_BoundingBox_proxy(const perception::metadata::BoundingBoxT *value,
                                           std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_Point2f() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *make_perception_metadata_Point2f_proxy(const perception::metadata::Point2fT *value,
                                                 std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_BitmapData() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_perception_metadata_BitmapData_proxy(const perception::metadata::BitmapDataT *value,
                                          std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_VideoFrameContext() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *make_perception_metadata_VideoFrameContext_proxy(
    const perception::metadata::VideoFrameContextT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_AudioFrameContext() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *make_perception_metadata_AudioFrameContext_proxy(
    const perception::metadata::AudioFrameContextT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_FrameContext() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_perception_metadata_FrameContext_proxy(const perception::metadata::FrameContextT *value,
                                            std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_ObjectEmbedding() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_perception_metadata_ObjectEmbedding_proxy(const perception::metadata::ObjectEmbeddingT *value,
                                               std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_ObjectEmbeddings() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *make_perception_metadata_ObjectEmbeddings_proxy(
    const perception::metadata::ObjectEmbeddingsT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_ObjectTrack() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_perception_metadata_ObjectTrack_proxy(const perception::metadata::ObjectTrackT *value,
                                           std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_ObjectTracks() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_perception_metadata_ObjectTracks_proxy(const perception::metadata::ObjectTracksT *value,
                                            std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_PerformanceOverlay() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *make_perception_metadata_PerformanceOverlay_proxy(
    const perception::metadata::PerformanceOverlayT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_PoseEstimation() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_perception_metadata_PoseEstimation_proxy(const perception::metadata::PoseEstimationT *value,
                                              std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_PoseEstimations() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_perception_metadata_PoseEstimations_proxy(const perception::metadata::PoseEstimationsT *value,
                                               std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_SegmentationMask() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *make_perception_metadata_SegmentationMask_proxy(
    const perception::metadata::SegmentationMaskT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_SegmentationMasks() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *make_perception_metadata_SegmentationMasks_proxy(
    const perception::metadata::SegmentationMasksT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_TrackTrace() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_perception_metadata_TrackTrace_proxy(const perception::metadata::TrackTraceT *value,
                                          std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_perception_metadata_TrackTraces() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_perception_metadata_TrackTraces_proxy(const perception::metadata::TrackTracesT *value,
                                           std::shared_ptr<const native_proxy_anchor> anchor);

PySequenceMethods &seq_perception_metadata_BoxDetections_detections_vector() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &pytype_perception_metadata_BoxDetections_detections_vector() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &seq_perception_metadata_Classification_candidates_vector() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &pytype_perception_metadata_Classification_candidates_vector() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &seq_perception_metadata_Classifications_classifications_vector() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &pytype_perception_metadata_Classifications_classifications_vector() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &seq_perception_metadata_Classifications_person_presence_vector() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &pytype_perception_metadata_Classifications_person_presence_vector() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &seq_perception_metadata_BitmapData_pixels_vector() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &pytype_perception_metadata_BitmapData_pixels_vector() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &seq_perception_metadata_ObjectEmbedding_values_vector() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &pytype_perception_metadata_ObjectEmbedding_values_vector() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &seq_perception_metadata_ObjectEmbeddings_embeddings_vector() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &pytype_perception_metadata_ObjectEmbeddings_embeddings_vector() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &seq_perception_metadata_ObjectTracks_tracks_vector() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &pytype_perception_metadata_ObjectTracks_tracks_vector() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &seq_perception_metadata_PerformanceOverlay_lines_vector() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &pytype_perception_metadata_PerformanceOverlay_lines_vector() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &seq_perception_metadata_PoseEstimations_poses_vector() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &pytype_perception_metadata_PoseEstimations_poses_vector() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &seq_perception_metadata_SegmentationMasks_masks_vector() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &pytype_perception_metadata_SegmentationMasks_masks_vector() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &seq_perception_metadata_TrackTrace_points_vector() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &pytype_perception_metadata_TrackTrace_points_vector() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &seq_perception_metadata_TrackTraces_traces_vector() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &pytype_perception_metadata_TrackTraces_traces_vector() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

void live_envelope_dealloc(PyObject *self) {
    auto *live = reinterpret_cast<live_envelope_object *>(self);
    live->envelope = nullptr;
    live->valid = false;
    Py_TYPE(self)->tp_free(self);
}

PyObject *live_envelope_is_valid(PyObject *self, PyObject *) {
    if (const auto *live = reinterpret_cast<const live_envelope_object *>(self);
        live->valid && live->envelope != nullptr) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

PyObject *producer_identity_status_object(container::producer_identity_status status) {
    const char *value = nullptr;
    switch (status) {
    case container::producer_identity_status::exact_match:
        value = "exact_match";
        break;
    case container::producer_identity_status::missing:
        value = "missing";
        break;
    case container::producer_identity_status::malformed:
        value = "malformed";
        break;
    case container::producer_identity_status::sdk_name_mismatch:
        value = "sdk_name_mismatch";
        break;
    case container::producer_identity_status::sdk_version_mismatch:
        value = "sdk_version_mismatch";
        break;
    case container::producer_identity_status::schema_set_mismatch:
        value = "schema_set_mismatch";
        break;
    }

    PyObject *sdk_module = PyImport_ImportModule("perception.sdk");
    if (sdk_module == nullptr) {
        return nullptr;
    }
    PyObject *enum_type = PyObject_GetAttrString(sdk_module, "ProducerIdentityStatus");
    Py_DECREF(sdk_module);
    if (enum_type == nullptr) {
        return nullptr;
    }
    PyObject *argument = PyUnicode_FromString(value);
    if (argument == nullptr) {
        Py_DECREF(enum_type);
        return nullptr;
    }
    PyObject *result = PyObject_CallOneArg(enum_type, argument);
    Py_DECREF(argument);
    Py_DECREF(enum_type);
    return result;
}

live_envelope_object *require_live_envelope(PyObject *self) {
    auto *live = as_live_envelope(self);
    if (live == nullptr) {
        PyErr_SetString(PyExc_TypeError, "expected perception_bridge.Envelope");
        return nullptr;
    }
    if (!live->valid || live->envelope == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception live envelope wrapper is no longer valid");
        return nullptr;
    }
    return live;
}

PyObject *live_envelope_producer_sdk_name(PyObject *self, void *) {
    const auto *live = require_live_envelope(self);
    if (live == nullptr) {
        return nullptr;
    }
    return py_string_from_std(live->envelope->producer_sdk_name());
}

PyObject *live_envelope_producer_sdk_version(PyObject *self, void *) {
    const auto *live = require_live_envelope(self);
    if (live == nullptr) {
        return nullptr;
    }
    return py_string_from_std(live->envelope->producer_sdk_version());
}

PyObject *live_envelope_producer_schema_set_sha256(PyObject *self, void *) {
    const auto *live = require_live_envelope(self);
    if (live == nullptr) {
        return nullptr;
    }
    return py_string_from_std(live->envelope->producer_schema_set_sha256());
}

PyObject *live_envelope_producer_identity(PyObject *self, PyObject *) {
    const auto *live = require_live_envelope(self);
    if (live == nullptr) {
        return nullptr;
    }
    return producer_identity_status_object(live->envelope->producer_identity());
}

std::optional<bool> is_external_key_python(PyObject *object) {
    if (PyUnicode_Check(object) || PyLong_Check(object)) {
        return false;
    }

    PyObject *sdk_module = PyImport_ImportModule("perception");
    if (sdk_module == nullptr) {
        PyErr_SetString(
            PyExc_TypeError,
            "perception ExternalKey requires the generated Python SDK to be importable");
        return std::nullopt;
    }

    PyObject *external_key_type = PyObject_GetAttrString(sdk_module, "ExternalKey");
    Py_DECREF(sdk_module);
    if (external_key_type == nullptr) {
        PyErr_SetString(PyExc_TypeError, "perception Python SDK does not expose ExternalKey");
        return std::nullopt;
    }

    const int is_instance = PyObject_IsInstance(object, external_key_type);
    Py_DECREF(external_key_type);
    if (is_instance < 0) {
        return std::nullopt;
    }
    return is_instance != 0;
}

std::optional<container::external_key_t> external_key_from_python(PyObject *object) {
    if (PyUnicode_Check(object)) {
        PyErr_SetString(PyExc_TypeError,
                        "perception external envelope operations require ExternalKey; call "
                        "external_key(...) first");
        return std::nullopt;
    }

    if (PyLong_Check(object)) {
        PyErr_SetString(PyExc_TypeError, "perception external key must be an ExternalKey, not int");
        return std::nullopt;
    }

    auto is_key = is_external_key_python(object);
    if (!is_key.has_value()) {
        return std::nullopt;
    }
    if (!is_key.value()) {
        PyErr_SetString(PyExc_TypeError, "perception external key must be an ExternalKey");
        return std::nullopt;
    }

    PyObject *value_object = PyObject_GetAttrString(object, "value");
    if (value_object == nullptr) {
        return std::nullopt;
    }

    const auto value = PyLong_AsUnsignedLongLong(value_object);
    Py_DECREF(value_object);
    if (PyErr_Occurred()) {
        return std::nullopt;
    }

    if (value < container::external_key_min) {
        PyErr_SetString(PyExc_TypeError,
                        "perception ExternalKey value is outside the external key domain");
        return std::nullopt;
    }

    return external_key_access::from_value(static_cast<detail::id_t>(value));
}

bool parse_index(PyObject *index_object, std::size_t *index) {
    if (index_object == nullptr) {
        *index = 0;
        return true;
    }
    if (!PyLong_Check(index_object)) {
        PyErr_SetString(PyExc_TypeError, "index must be an integer");
        return false;
    }
    const auto value = PyLong_AsSsize_t(index_object);
    if (PyErr_Occurred()) {
        return false;
    }
    if (value < 0) {
        PyErr_SetString(PyExc_ValueError, "index must be non-negative");
        return false;
    }
    *index = static_cast<std::size_t>(value);
    return true;
}

PyObject *bytes_from_span(std::span<const std::uint8_t> bytes) {
    return PyBytes_FromStringAndSize(reinterpret_cast<const char *>(bytes.data()),
                                     static_cast<Py_ssize_t>(bytes.size()));
}

enum class known_payload_kind {
    kind_perception_metadata_BoxDetections,
    kind_perception_metadata_Classifications,
    kind_perception_metadata_FrameContext,
    kind_perception_metadata_ObjectEmbeddings,
    kind_perception_metadata_ObjectTracks,
    kind_perception_metadata_PerformanceOverlay,
    kind_perception_metadata_PoseEstimations,
    kind_perception_metadata_SegmentationMasks,
    kind_perception_metadata_TrackTraces,
};

class py_object_handle {
  public:
    explicit py_object_handle(PyObject *object = nullptr) noexcept : object_(object) {}

    py_object_handle(const py_object_handle &) = delete;
    py_object_handle &operator=(const py_object_handle &) = delete;

    py_object_handle(py_object_handle &&other) noexcept
        : object_(std::exchange(other.object_, nullptr)) {}

    py_object_handle &operator=(py_object_handle &&other) noexcept {
        if (this != &other) {
            Py_XDECREF(object_);
            object_ = std::exchange(other.object_, nullptr);
        }
        return *this;
    }

    ~py_object_handle() {
        Py_XDECREF(object_);
    }

    [[nodiscard]] PyObject *get() const noexcept {
        return object_;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return object_ != nullptr;
    }

  private:
    PyObject *object_;
};

bool py_unicode_equals_ascii(PyObject *object, const char *expected) {
    return PyUnicode_Check(object) && PyUnicode_CompareWithASCIIString(object, expected) == 0;
}

bool py_type_matches(PyObject *type_object, const char *module_name, const char *class_name) {
    if (type_object == nullptr || !PyType_Check(type_object)) {
        return false;
    }

    py_object_handle python_module(PyObject_GetAttrString(type_object, "__module__"));
    py_object_handle name(PyObject_GetAttrString(type_object, "__name__"));
    if (!python_module || !name) {
        PyErr_Clear();
        return false;
    }

    return py_unicode_equals_ascii(python_module.get(), module_name) &&
           py_unicode_equals_ascii(name.get(), class_name);
}

bool py_object_type_matches(PyObject *value, const char *module_name, const char *class_name) {
    return value != nullptr &&
           py_type_matches(reinterpret_cast<PyObject *>(Py_TYPE(value)), module_name, class_name);
}

std::string py_type_description(PyObject *object) {
    if (object == nullptr) {
        return "<null>";
    }

    PyObject *type_object =
        PyType_Check(object) ? object : reinterpret_cast<PyObject *>(Py_TYPE(object));

    py_object_handle python_module(PyObject_GetAttrString(type_object, "__module__"));
    py_object_handle name(PyObject_GetAttrString(type_object, "__name__"));
    if (!python_module || !name || !PyUnicode_Check(python_module.get()) ||
        !PyUnicode_Check(name.get())) {
        PyErr_Clear();
        return "<unknown>";
    }

    const char *module_text = PyUnicode_AsUTF8(python_module.get());
    const char *name_text = PyUnicode_AsUTF8(name.get());
    if (module_text == nullptr || name_text == nullptr) {
        PyErr_Clear();
        return "<unknown>";
    }
    return std::string(module_text) + "." + name_text;
}

std::optional<known_payload_kind> known_payload_kind_from_type(PyObject *type_object) {
    if (type_object == nullptr || !PyType_Check(type_object)) {
        PyErr_SetString(
            PyExc_TypeError,
            "perception known payload argument must be a generated Python payload type");
        return std::nullopt;
    }

    if (py_type_matches(
            type_object, "perception.fb.perception.metadata.BoxDetections", "BoxDetectionsT")) {
        return known_payload_kind::kind_perception_metadata_BoxDetections;
    }
    if (py_type_matches(
            type_object, "perception.fb.perception.metadata.Classifications", "ClassificationsT")) {
        return known_payload_kind::kind_perception_metadata_Classifications;
    }
    if (py_type_matches(
            type_object, "perception.fb.perception.metadata.FrameContext", "FrameContextT")) {
        return known_payload_kind::kind_perception_metadata_FrameContext;
    }
    if (py_type_matches(type_object,
                        "perception.fb.perception.metadata.ObjectEmbeddings",
                        "ObjectEmbeddingsT")) {
        return known_payload_kind::kind_perception_metadata_ObjectEmbeddings;
    }
    if (py_type_matches(
            type_object, "perception.fb.perception.metadata.ObjectTracks", "ObjectTracksT")) {
        return known_payload_kind::kind_perception_metadata_ObjectTracks;
    }
    if (py_type_matches(type_object,
                        "perception.fb.perception.metadata.PerformanceOverlay",
                        "PerformanceOverlayT")) {
        return known_payload_kind::kind_perception_metadata_PerformanceOverlay;
    }
    if (py_type_matches(
            type_object, "perception.fb.perception.metadata.PoseEstimations", "PoseEstimationsT")) {
        return known_payload_kind::kind_perception_metadata_PoseEstimations;
    }
    if (py_type_matches(type_object,
                        "perception.fb.perception.metadata.SegmentationMasks",
                        "SegmentationMasksT")) {
        return known_payload_kind::kind_perception_metadata_SegmentationMasks;
    }
    if (py_type_matches(
            type_object, "perception.fb.perception.metadata.TrackTraces", "TrackTracesT")) {
        return known_payload_kind::kind_perception_metadata_TrackTraces;
    }

    const auto message =
        "perception unknown known payload type: " + py_type_description(type_object);
    PyErr_SetString(PyExc_TypeError, message.c_str());
    return std::nullopt;
}

std::optional<known_payload_kind> known_payload_kind_from_value(PyObject *value) {
    if (py_object_type_matches(
            value, "perception.fb.perception.metadata.BoxDetections", "BoxDetectionsT")) {
        return known_payload_kind::kind_perception_metadata_BoxDetections;
    }
    if (py_object_type_matches(
            value, "perception.fb.perception.metadata.Classifications", "ClassificationsT")) {
        return known_payload_kind::kind_perception_metadata_Classifications;
    }
    if (py_object_type_matches(
            value, "perception.fb.perception.metadata.FrameContext", "FrameContextT")) {
        return known_payload_kind::kind_perception_metadata_FrameContext;
    }
    if (py_object_type_matches(
            value, "perception.fb.perception.metadata.ObjectEmbeddings", "ObjectEmbeddingsT")) {
        return known_payload_kind::kind_perception_metadata_ObjectEmbeddings;
    }
    if (py_object_type_matches(
            value, "perception.fb.perception.metadata.ObjectTracks", "ObjectTracksT")) {
        return known_payload_kind::kind_perception_metadata_ObjectTracks;
    }
    if (py_object_type_matches(
            value, "perception.fb.perception.metadata.PerformanceOverlay", "PerformanceOverlayT")) {
        return known_payload_kind::kind_perception_metadata_PerformanceOverlay;
    }
    if (py_object_type_matches(
            value, "perception.fb.perception.metadata.PoseEstimations", "PoseEstimationsT")) {
        return known_payload_kind::kind_perception_metadata_PoseEstimations;
    }
    if (py_object_type_matches(
            value, "perception.fb.perception.metadata.SegmentationMasks", "SegmentationMasksT")) {
        return known_payload_kind::kind_perception_metadata_SegmentationMasks;
    }
    if (py_object_type_matches(
            value, "perception.fb.perception.metadata.TrackTraces", "TrackTracesT")) {
        return known_payload_kind::kind_perception_metadata_TrackTraces;
    }

    const auto message =
        "perception cannot add unsupported payload object: " + py_type_description(value);
    PyErr_SetString(PyExc_TypeError, message.c_str());
    return std::nullopt;
}

template <container::native_payload T>
PyObject *proxy_from_payload_ref(container::payload_ref<T> ref, PyTypeObject &type) {
    auto anchor = std::make_shared<payload_anchor<T>>(std::move(ref));
    const auto *value = anchor->ref.get();
    return make_native_proxy<T>(type, value, std::move(anchor));
}

template <container::native_payload T>
PyObject *get_known_payload(container::envelope &envelope, PyTypeObject &type, std::size_t index) {
    auto ref = envelope.get<T>(index);
    if (!ref) {
        Py_RETURN_NONE;
    }
    return proxy_from_payload_ref<T>(std::move(*ref), type);
}

template <container::native_payload T>
PyObject *list_known_payloads(container::envelope &envelope, PyTypeObject &type) {
    PyObject *result = PyList_New(0);
    if (result == nullptr) {
        return nullptr;
    }

    const auto count = envelope.count<T>();
    for (std::size_t index = 0; index < count; ++index) {
        auto ref = envelope.get<T>(index);
        if (!ref) {
            continue;
        }
        PyObject *item = proxy_from_payload_ref<T>(std::move(*ref), type);
        if (item == nullptr) {
            Py_DECREF(result);
            return nullptr;
        }
        if (PyList_Append(result, item) < 0) {
            Py_DECREF(item);
            Py_DECREF(result);
            return nullptr;
        }
        Py_DECREF(item);
    }

    return result;
}

template <container::native_payload T>
std::optional<T> python_payload_to_native(PyObject *value, const char *file_identifier) {
    py_object_handle flatbuffers_module(PyImport_ImportModule("flatbuffers"));
    if (!flatbuffers_module) {
        return std::nullopt;
    }

    py_object_handle builder_type(PyObject_GetAttrString(flatbuffers_module.get(), "Builder"));
    if (!builder_type) {
        return std::nullopt;
    }

    py_object_handle builder(PyObject_CallFunction(builder_type.get(), "i", 0));
    if (!builder) {
        return std::nullopt;
    }

    py_object_handle offset(PyObject_CallMethod(value, "Pack", "O", builder.get()));
    if (!offset) {
        return std::nullopt;
    }

    py_object_handle identifier(PyBytes_FromStringAndSize(file_identifier, 4));
    if (!identifier) {
        return std::nullopt;
    }

    if (py_object_handle finish_result(
            PyObject_CallMethod(builder.get(), "Finish", "OO", offset.get(), identifier.get()));
        !finish_result) {
        return std::nullopt;
    }

    py_object_handle output(PyObject_CallMethod(builder.get(), "Output", nullptr));
    if (!output) {
        return std::nullopt;
    }

    Py_buffer view{};
    if (PyObject_GetBuffer(output.get(), &view, PyBUF_SIMPLE) < 0) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> blob;
    if (view.len > 0) {
        const auto *begin = static_cast<const std::uint8_t *>(view.buf);
        blob.assign(begin, begin + static_cast<std::size_t>(view.len));
    }
    PyBuffer_Release(&view);

    using traits = detail::native_traits<T>;
    if (flatbuffers::Verifier verifier(blob.data(), blob.size());
        !verifier.VerifyBuffer<typename traits::table_type>(traits::file_identifier())) {
        PyErr_SetString(
            PyExc_ValueError,
            "perception payload object did not pack into the expected FlatBuffers root type");
        return std::nullopt;
    }

    const auto *root = flatbuffers::GetRoot<typename traits::table_type>(blob.data());
    if (root == nullptr) {
        PyErr_SetString(PyExc_ValueError,
                        "perception payload object packed an invalid FlatBuffers root");
        return std::nullopt;
    }

    T native{};
    root->UnPackTo(&native);
    return native;
}

template <container::native_payload T>
PyObject *
add_known_payload(container::envelope &envelope, PyObject *value, const char *file_identifier) {
    auto native = python_payload_to_native<T>(value, file_identifier);
    if (!native) {
        return nullptr;
    }

    try {
        envelope.add(std::move(*native));
    } catch (const std::exception &exc) {
        PyErr_SetString(PyExc_RuntimeError, exc.what());
        return nullptr;
    }

    Py_RETURN_NONE;
}

PyObject *live_envelope_count(PyObject *self, PyObject *args) {
    PyObject *selector = nullptr;
    if (!PyArg_ParseTuple(args, "O:count", &selector)) {
        return nullptr;
    }
    const auto *live = require_live_envelope(self);
    if (live == nullptr) {
        return nullptr;
    }

    if (!PyType_Check(selector)) {
        auto key = external_key_from_python(selector);
        if (!key) {
            return nullptr;
        }
        return PyLong_FromSize_t(live->envelope->count(*key));
    }

    auto kind = known_payload_kind_from_type(selector);
    if (!kind) {
        return nullptr;
    }

    switch (*kind) {
    case known_payload_kind::kind_perception_metadata_BoxDetections:
        return PyLong_FromSize_t(live->envelope->count<perception::metadata::BoxDetectionsT>());
    case known_payload_kind::kind_perception_metadata_Classifications:
        return PyLong_FromSize_t(live->envelope->count<perception::metadata::ClassificationsT>());
    case known_payload_kind::kind_perception_metadata_FrameContext:
        return PyLong_FromSize_t(live->envelope->count<perception::metadata::FrameContextT>());
    case known_payload_kind::kind_perception_metadata_ObjectEmbeddings:
        return PyLong_FromSize_t(live->envelope->count<perception::metadata::ObjectEmbeddingsT>());
    case known_payload_kind::kind_perception_metadata_ObjectTracks:
        return PyLong_FromSize_t(live->envelope->count<perception::metadata::ObjectTracksT>());
    case known_payload_kind::kind_perception_metadata_PerformanceOverlay:
        return PyLong_FromSize_t(
            live->envelope->count<perception::metadata::PerformanceOverlayT>());
    case known_payload_kind::kind_perception_metadata_PoseEstimations:
        return PyLong_FromSize_t(live->envelope->count<perception::metadata::PoseEstimationsT>());
    case known_payload_kind::kind_perception_metadata_SegmentationMasks:
        return PyLong_FromSize_t(live->envelope->count<perception::metadata::SegmentationMasksT>());
    case known_payload_kind::kind_perception_metadata_TrackTraces:
        return PyLong_FromSize_t(live->envelope->count<perception::metadata::TrackTracesT>());
    }

    Py_UNREACHABLE();
}

PyObject *live_envelope_contains_external(const container::envelope &envelope, PyObject *selector) {
    auto key = external_key_from_python(selector);
    if (!key) {
        return nullptr;
    }
    return PyBool_FromLong(envelope.contains(*key));
}

PyObject *live_envelope_contains(PyObject *self, PyObject *args) {
    PyObject *selector = nullptr;
    if (!PyArg_ParseTuple(args, "O:contains", &selector)) {
        return nullptr;
    }
    const auto *live = require_live_envelope(self);
    if (live == nullptr) {
        return nullptr;
    }

    if (!PyType_Check(selector)) {
        return live_envelope_contains_external(*live->envelope, selector);
    }

    auto kind = known_payload_kind_from_type(selector);
    if (!kind) {
        return nullptr;
    }

    switch (*kind) {
    case known_payload_kind::kind_perception_metadata_BoxDetections:
        if (live->envelope->contains<perception::metadata::BoxDetectionsT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::kind_perception_metadata_Classifications:
        if (live->envelope->contains<perception::metadata::ClassificationsT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::kind_perception_metadata_FrameContext:
        if (live->envelope->contains<perception::metadata::FrameContextT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::kind_perception_metadata_ObjectEmbeddings:
        if (live->envelope->contains<perception::metadata::ObjectEmbeddingsT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::kind_perception_metadata_ObjectTracks:
        if (live->envelope->contains<perception::metadata::ObjectTracksT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::kind_perception_metadata_PerformanceOverlay:
        if (live->envelope->contains<perception::metadata::PerformanceOverlayT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::kind_perception_metadata_PoseEstimations:
        if (live->envelope->contains<perception::metadata::PoseEstimationsT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::kind_perception_metadata_SegmentationMasks:
        if (live->envelope->contains<perception::metadata::SegmentationMasksT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::kind_perception_metadata_TrackTraces:
        if (live->envelope->contains<perception::metadata::TrackTracesT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    }

    Py_UNREACHABLE();
}

PyObject *live_envelope_get(PyObject *self, PyObject *args, PyObject *kwargs) {
    static char selector_keyword[] = "selector";
    static char index_keyword[] = "index";
    static char *keywords[] = {selector_keyword, index_keyword, nullptr};
    PyObject *selector = nullptr;
    PyObject *index_object = nullptr;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|O:get", keywords, &selector, &index_object)) {
        return nullptr;
    }
    auto *live = require_live_envelope(self);
    if (live == nullptr) {
        return nullptr;
    }
    std::size_t index = 0;
    if (!parse_index(index_object, &index)) {
        return nullptr;
    }

    if (!PyType_Check(selector)) {
        auto key = external_key_from_python(selector);
        if (!key) {
            return nullptr;
        }
        const auto payload = live->envelope->get(*key, index);
        if (!payload) {
            Py_RETURN_NONE;
        }
        return bytes_from_span(payload->bytes());
    }

    auto kind = known_payload_kind_from_type(selector);
    if (!kind) {
        return nullptr;
    }

    switch (*kind) {
    case known_payload_kind::kind_perception_metadata_BoxDetections:
        return get_known_payload<perception::metadata::BoxDetectionsT>(
            *live->envelope, pytype_perception_metadata_BoxDetections(), index);
    case known_payload_kind::kind_perception_metadata_Classifications:
        return get_known_payload<perception::metadata::ClassificationsT>(
            *live->envelope, pytype_perception_metadata_Classifications(), index);
    case known_payload_kind::kind_perception_metadata_FrameContext:
        return get_known_payload<perception::metadata::FrameContextT>(
            *live->envelope, pytype_perception_metadata_FrameContext(), index);
    case known_payload_kind::kind_perception_metadata_ObjectEmbeddings:
        return get_known_payload<perception::metadata::ObjectEmbeddingsT>(
            *live->envelope, pytype_perception_metadata_ObjectEmbeddings(), index);
    case known_payload_kind::kind_perception_metadata_ObjectTracks:
        return get_known_payload<perception::metadata::ObjectTracksT>(
            *live->envelope, pytype_perception_metadata_ObjectTracks(), index);
    case known_payload_kind::kind_perception_metadata_PerformanceOverlay:
        return get_known_payload<perception::metadata::PerformanceOverlayT>(
            *live->envelope, pytype_perception_metadata_PerformanceOverlay(), index);
    case known_payload_kind::kind_perception_metadata_PoseEstimations:
        return get_known_payload<perception::metadata::PoseEstimationsT>(
            *live->envelope, pytype_perception_metadata_PoseEstimations(), index);
    case known_payload_kind::kind_perception_metadata_SegmentationMasks:
        return get_known_payload<perception::metadata::SegmentationMasksT>(
            *live->envelope, pytype_perception_metadata_SegmentationMasks(), index);
    case known_payload_kind::kind_perception_metadata_TrackTraces:
        return get_known_payload<perception::metadata::TrackTracesT>(
            *live->envelope, pytype_perception_metadata_TrackTraces(), index);
    }

    Py_UNREACHABLE();
}

PyObject *live_envelope_for_each(PyObject *self, PyObject *args) {
    PyObject *selector = nullptr;
    if (!PyArg_ParseTuple(args, "O:for_each", &selector)) {
        return nullptr;
    }
    auto *live = require_live_envelope(self);
    if (live == nullptr) {
        return nullptr;
    }

    if (!PyType_Check(selector)) {
        auto key = external_key_from_python(selector);
        if (!key) {
            return nullptr;
        }
        PyObject *result = PyList_New(0);
        if (result == nullptr) {
            return nullptr;
        }
        bool ok = true;
        live->envelope->for_each(*key, [&ok, result](std::span<const std::uint8_t> bytes) {
            if (!ok) {
                return;
            }
            PyObject *item = bytes_from_span(bytes);
            if (item == nullptr) {
                ok = false;
                return;
            }
            if (PyList_Append(result, item) < 0) {
                Py_DECREF(item);
                ok = false;
                return;
            }
            Py_DECREF(item);
        });
        if (!ok) {
            Py_DECREF(result);
            return nullptr;
        }
        return result;
    }

    auto kind = known_payload_kind_from_type(selector);
    if (!kind) {
        return nullptr;
    }

    switch (*kind) {
    case known_payload_kind::kind_perception_metadata_BoxDetections:
        return list_known_payloads<perception::metadata::BoxDetectionsT>(
            *live->envelope, pytype_perception_metadata_BoxDetections());
    case known_payload_kind::kind_perception_metadata_Classifications:
        return list_known_payloads<perception::metadata::ClassificationsT>(
            *live->envelope, pytype_perception_metadata_Classifications());
    case known_payload_kind::kind_perception_metadata_FrameContext:
        return list_known_payloads<perception::metadata::FrameContextT>(
            *live->envelope, pytype_perception_metadata_FrameContext());
    case known_payload_kind::kind_perception_metadata_ObjectEmbeddings:
        return list_known_payloads<perception::metadata::ObjectEmbeddingsT>(
            *live->envelope, pytype_perception_metadata_ObjectEmbeddings());
    case known_payload_kind::kind_perception_metadata_ObjectTracks:
        return list_known_payloads<perception::metadata::ObjectTracksT>(
            *live->envelope, pytype_perception_metadata_ObjectTracks());
    case known_payload_kind::kind_perception_metadata_PerformanceOverlay:
        return list_known_payloads<perception::metadata::PerformanceOverlayT>(
            *live->envelope, pytype_perception_metadata_PerformanceOverlay());
    case known_payload_kind::kind_perception_metadata_PoseEstimations:
        return list_known_payloads<perception::metadata::PoseEstimationsT>(
            *live->envelope, pytype_perception_metadata_PoseEstimations());
    case known_payload_kind::kind_perception_metadata_SegmentationMasks:
        return list_known_payloads<perception::metadata::SegmentationMasksT>(
            *live->envelope, pytype_perception_metadata_SegmentationMasks());
    case known_payload_kind::kind_perception_metadata_TrackTraces:
        return list_known_payloads<perception::metadata::TrackTracesT>(
            *live->envelope, pytype_perception_metadata_TrackTraces());
    }

    Py_UNREACHABLE();
}

PyObject *live_envelope_add(PyObject *self, PyObject *args) {
    PyObject *value = nullptr;
    PyObject *blob_object = nullptr;
    if (!PyArg_ParseTuple(args, "O|O:add", &value, &blob_object)) {
        return nullptr;
    }
    auto *live = require_live_envelope(self);
    if (live == nullptr) {
        return nullptr;
    }

    if (blob_object != nullptr) {
        auto key = external_key_from_python(value);
        if (!key) {
            return nullptr;
        }

        Py_buffer view{};
        if (PyObject_GetBuffer(blob_object, &view, PyBUF_SIMPLE) < 0) {
            return nullptr;
        }

        if (view.len < 0) {
            PyBuffer_Release(&view);
            PyErr_SetString(PyExc_ValueError, "external payload byte buffer has invalid length");
            return nullptr;
        }

        live->envelope->add(
            *key,
            std::span<const std::uint8_t>(static_cast<const std::uint8_t *>(view.buf),
                                          static_cast<std::size_t>(view.len)));
        PyBuffer_Release(&view);
        Py_RETURN_NONE;
    }

    auto is_key = is_external_key_python(value);
    if (!is_key.has_value()) {
        return nullptr;
    }
    if (is_key.value()) {
        PyErr_SetString(PyExc_TypeError, "perception external add requires a bytes-like blob");
        return nullptr;
    }

    auto kind = known_payload_kind_from_value(value);
    if (!kind) {
        return nullptr;
    }

    switch (*kind) {
    case known_payload_kind::kind_perception_metadata_BoxDetections:
        return add_known_payload<perception::metadata::BoxDetectionsT>(
            *live->envelope, value, "BDET");
    case known_payload_kind::kind_perception_metadata_Classifications:
        return add_known_payload<perception::metadata::ClassificationsT>(
            *live->envelope, value, "CLSF");
    case known_payload_kind::kind_perception_metadata_FrameContext:
        return add_known_payload<perception::metadata::FrameContextT>(
            *live->envelope, value, "FCTX");
    case known_payload_kind::kind_perception_metadata_ObjectEmbeddings:
        return add_known_payload<perception::metadata::ObjectEmbeddingsT>(
            *live->envelope, value, "EMBE");
    case known_payload_kind::kind_perception_metadata_ObjectTracks:
        return add_known_payload<perception::metadata::ObjectTracksT>(
            *live->envelope, value, "TRKS");
    case known_payload_kind::kind_perception_metadata_PerformanceOverlay:
        return add_known_payload<perception::metadata::PerformanceOverlayT>(
            *live->envelope, value, "PERF");
    case known_payload_kind::kind_perception_metadata_PoseEstimations:
        return add_known_payload<perception::metadata::PoseEstimationsT>(
            *live->envelope, value, "POSE");
    case known_payload_kind::kind_perception_metadata_SegmentationMasks:
        return add_known_payload<perception::metadata::SegmentationMasksT>(
            *live->envelope, value, "SGMS");
    case known_payload_kind::kind_perception_metadata_TrackTraces:
        return add_known_payload<perception::metadata::TrackTracesT>(
            *live->envelope, value, "TRCE");
    }

    Py_UNREACHABLE();
}

Py_ssize_t len_perception_metadata_BoxDetections_detections_vector(PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<perception::metadata::BoxDetectionT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *item_perception_metadata_BoxDetections_detections_vector(PyObject *self,
                                                                   Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<perception::metadata::BoxDetectionT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "perception vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_BoxDetection_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<perception::metadata::BoxDetectionT>>>(self));
}

bool ensure_perception_metadata_BoxDetections_detections_vector_type() {
    auto &type = pytype_perception_metadata_BoxDetections_detections_vector();
    auto &sequence = seq_perception_metadata_BoxDetections_detections_vector();
    if (type.tp_name == nullptr) {
        sequence.sq_length = len_perception_metadata_BoxDetections_detections_vector;
        sequence.sq_item = item_perception_metadata_BoxDetections_detections_vector;
        type.tp_name = "perception_bridge.perception_metadata_BoxDetections_detections_vector";
        type.tp_basicsize = sizeof(
            native_proxy_object<std::vector<std::unique_ptr<perception::metadata::BoxDetectionT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc =
            native_proxy_dealloc<std::vector<std::unique_ptr<perception::metadata::BoxDetectionT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t len_perception_metadata_Classification_candidates_vector(PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<perception::metadata::ClassificationCandidateT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *item_perception_metadata_Classification_candidates_vector(PyObject *self,
                                                                    Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<perception::metadata::ClassificationCandidateT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "perception vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_ClassificationCandidate_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<perception::metadata::ClassificationCandidateT>>>(
            self));
}

bool ensure_perception_metadata_Classification_candidates_vector_type() {
    auto &type = pytype_perception_metadata_Classification_candidates_vector();
    auto &sequence = seq_perception_metadata_Classification_candidates_vector();
    if (type.tp_name == nullptr) {
        sequence.sq_length = len_perception_metadata_Classification_candidates_vector;
        sequence.sq_item = item_perception_metadata_Classification_candidates_vector;
        type.tp_name = "perception_bridge.perception_metadata_Classification_candidates_vector";
        type.tp_basicsize =
            sizeof(native_proxy_object<
                   std::vector<std::unique_ptr<perception::metadata::ClassificationCandidateT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<perception::metadata::ClassificationCandidateT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t len_perception_metadata_Classifications_classifications_vector(PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<perception::metadata::ClassificationT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *item_perception_metadata_Classifications_classifications_vector(PyObject *self,
                                                                          Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<perception::metadata::ClassificationT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "perception vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_Classification_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<perception::metadata::ClassificationT>>>(self));
}

bool ensure_perception_metadata_Classifications_classifications_vector_type() {
    auto &type = pytype_perception_metadata_Classifications_classifications_vector();
    auto &sequence = seq_perception_metadata_Classifications_classifications_vector();
    if (type.tp_name == nullptr) {
        sequence.sq_length = len_perception_metadata_Classifications_classifications_vector;
        sequence.sq_item = item_perception_metadata_Classifications_classifications_vector;
        type.tp_name =
            "perception_bridge.perception_metadata_Classifications_classifications_vector";
        type.tp_basicsize =
            sizeof(native_proxy_object<
                   std::vector<std::unique_ptr<perception::metadata::ClassificationT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<perception::metadata::ClassificationT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t len_perception_metadata_Classifications_person_presence_vector(PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<perception::metadata::PersonPresenceT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *item_perception_metadata_Classifications_person_presence_vector(PyObject *self,
                                                                          Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<perception::metadata::PersonPresenceT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "perception vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_PersonPresence_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<perception::metadata::PersonPresenceT>>>(self));
}

bool ensure_perception_metadata_Classifications_person_presence_vector_type() {
    auto &type = pytype_perception_metadata_Classifications_person_presence_vector();
    auto &sequence = seq_perception_metadata_Classifications_person_presence_vector();
    if (type.tp_name == nullptr) {
        sequence.sq_length = len_perception_metadata_Classifications_person_presence_vector;
        sequence.sq_item = item_perception_metadata_Classifications_person_presence_vector;
        type.tp_name =
            "perception_bridge.perception_metadata_Classifications_person_presence_vector";
        type.tp_basicsize =
            sizeof(native_proxy_object<
                   std::vector<std::unique_ptr<perception::metadata::PersonPresenceT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<perception::metadata::PersonPresenceT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t len_perception_metadata_BitmapData_pixels_vector(PyObject *self) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<std::vector<std::uint8_t>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *item_perception_metadata_BitmapData_pixels_vector(PyObject *self, Py_ssize_t index) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<std::vector<std::uint8_t>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "perception vector proxy index out of range");
        return nullptr;
    }
    return py_long_from_unsigned((*vector)[static_cast<std::size_t>(index)]);
}

bool ensure_perception_metadata_BitmapData_pixels_vector_type() {
    auto &type = pytype_perception_metadata_BitmapData_pixels_vector();
    auto &sequence = seq_perception_metadata_BitmapData_pixels_vector();
    if (type.tp_name == nullptr) {
        sequence.sq_length = len_perception_metadata_BitmapData_pixels_vector;
        sequence.sq_item = item_perception_metadata_BitmapData_pixels_vector;
        type.tp_name = "perception_bridge.perception_metadata_BitmapData_pixels_vector";
        type.tp_basicsize = sizeof(native_proxy_object<std::vector<std::uint8_t>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<std::vector<std::uint8_t>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t len_perception_metadata_ObjectEmbedding_values_vector(PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<std::vector<float>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *item_perception_metadata_ObjectEmbedding_values_vector(PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<std::vector<float>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "perception vector proxy index out of range");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>((*vector)[static_cast<std::size_t>(index)]));
}

bool ensure_perception_metadata_ObjectEmbedding_values_vector_type() {
    auto &type = pytype_perception_metadata_ObjectEmbedding_values_vector();
    auto &sequence = seq_perception_metadata_ObjectEmbedding_values_vector();
    if (type.tp_name == nullptr) {
        sequence.sq_length = len_perception_metadata_ObjectEmbedding_values_vector;
        sequence.sq_item = item_perception_metadata_ObjectEmbedding_values_vector;
        type.tp_name = "perception_bridge.perception_metadata_ObjectEmbedding_values_vector";
        type.tp_basicsize = sizeof(native_proxy_object<std::vector<float>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<std::vector<float>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t len_perception_metadata_ObjectEmbeddings_embeddings_vector(PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<perception::metadata::ObjectEmbeddingT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *item_perception_metadata_ObjectEmbeddings_embeddings_vector(PyObject *self,
                                                                      Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<perception::metadata::ObjectEmbeddingT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "perception vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_ObjectEmbedding_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<perception::metadata::ObjectEmbeddingT>>>(self));
}

bool ensure_perception_metadata_ObjectEmbeddings_embeddings_vector_type() {
    auto &type = pytype_perception_metadata_ObjectEmbeddings_embeddings_vector();
    auto &sequence = seq_perception_metadata_ObjectEmbeddings_embeddings_vector();
    if (type.tp_name == nullptr) {
        sequence.sq_length = len_perception_metadata_ObjectEmbeddings_embeddings_vector;
        sequence.sq_item = item_perception_metadata_ObjectEmbeddings_embeddings_vector;
        type.tp_name = "perception_bridge.perception_metadata_ObjectEmbeddings_embeddings_vector";
        type.tp_basicsize =
            sizeof(native_proxy_object<
                   std::vector<std::unique_ptr<perception::metadata::ObjectEmbeddingT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<perception::metadata::ObjectEmbeddingT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t len_perception_metadata_ObjectTracks_tracks_vector(PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<perception::metadata::ObjectTrackT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *item_perception_metadata_ObjectTracks_tracks_vector(PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<perception::metadata::ObjectTrackT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "perception vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_ObjectTrack_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<perception::metadata::ObjectTrackT>>>(self));
}

bool ensure_perception_metadata_ObjectTracks_tracks_vector_type() {
    auto &type = pytype_perception_metadata_ObjectTracks_tracks_vector();
    auto &sequence = seq_perception_metadata_ObjectTracks_tracks_vector();
    if (type.tp_name == nullptr) {
        sequence.sq_length = len_perception_metadata_ObjectTracks_tracks_vector;
        sequence.sq_item = item_perception_metadata_ObjectTracks_tracks_vector;
        type.tp_name = "perception_bridge.perception_metadata_ObjectTracks_tracks_vector";
        type.tp_basicsize = sizeof(
            native_proxy_object<std::vector<std::unique_ptr<perception::metadata::ObjectTrackT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc =
            native_proxy_dealloc<std::vector<std::unique_ptr<perception::metadata::ObjectTrackT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t len_perception_metadata_PerformanceOverlay_lines_vector(PyObject *self) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<std::vector<std::string>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *item_perception_metadata_PerformanceOverlay_lines_vector(PyObject *self,
                                                                   Py_ssize_t index) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<std::vector<std::string>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "perception vector proxy index out of range");
        return nullptr;
    }
    return py_string_from_std((*vector)[static_cast<std::size_t>(index)]);
}

bool ensure_perception_metadata_PerformanceOverlay_lines_vector_type() {
    auto &type = pytype_perception_metadata_PerformanceOverlay_lines_vector();
    auto &sequence = seq_perception_metadata_PerformanceOverlay_lines_vector();
    if (type.tp_name == nullptr) {
        sequence.sq_length = len_perception_metadata_PerformanceOverlay_lines_vector;
        sequence.sq_item = item_perception_metadata_PerformanceOverlay_lines_vector;
        type.tp_name = "perception_bridge.perception_metadata_PerformanceOverlay_lines_vector";
        type.tp_basicsize = sizeof(native_proxy_object<std::vector<std::string>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<std::vector<std::string>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t len_perception_metadata_PoseEstimations_poses_vector(PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<perception::metadata::PoseEstimationT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *item_perception_metadata_PoseEstimations_poses_vector(PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<perception::metadata::PoseEstimationT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "perception vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_PoseEstimation_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<perception::metadata::PoseEstimationT>>>(self));
}

bool ensure_perception_metadata_PoseEstimations_poses_vector_type() {
    auto &type = pytype_perception_metadata_PoseEstimations_poses_vector();
    auto &sequence = seq_perception_metadata_PoseEstimations_poses_vector();
    if (type.tp_name == nullptr) {
        sequence.sq_length = len_perception_metadata_PoseEstimations_poses_vector;
        sequence.sq_item = item_perception_metadata_PoseEstimations_poses_vector;
        type.tp_name = "perception_bridge.perception_metadata_PoseEstimations_poses_vector";
        type.tp_basicsize =
            sizeof(native_proxy_object<
                   std::vector<std::unique_ptr<perception::metadata::PoseEstimationT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<perception::metadata::PoseEstimationT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t len_perception_metadata_SegmentationMasks_masks_vector(PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<perception::metadata::SegmentationMaskT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *item_perception_metadata_SegmentationMasks_masks_vector(PyObject *self,
                                                                  Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<perception::metadata::SegmentationMaskT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "perception vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_SegmentationMask_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<perception::metadata::SegmentationMaskT>>>(self));
}

bool ensure_perception_metadata_SegmentationMasks_masks_vector_type() {
    auto &type = pytype_perception_metadata_SegmentationMasks_masks_vector();
    auto &sequence = seq_perception_metadata_SegmentationMasks_masks_vector();
    if (type.tp_name == nullptr) {
        sequence.sq_length = len_perception_metadata_SegmentationMasks_masks_vector;
        sequence.sq_item = item_perception_metadata_SegmentationMasks_masks_vector;
        type.tp_name = "perception_bridge.perception_metadata_SegmentationMasks_masks_vector";
        type.tp_basicsize =
            sizeof(native_proxy_object<
                   std::vector<std::unique_ptr<perception::metadata::SegmentationMaskT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<perception::metadata::SegmentationMaskT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t len_perception_metadata_TrackTrace_points_vector(PyObject *self) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<std::vector<std::unique_ptr<perception::metadata::Point2fT>>> *>(
        self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *item_perception_metadata_TrackTrace_points_vector(PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<std::vector<std::unique_ptr<perception::metadata::Point2fT>>> *>(
        self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "perception vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_Point2f_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<perception::metadata::Point2fT>>>(self));
}

bool ensure_perception_metadata_TrackTrace_points_vector_type() {
    auto &type = pytype_perception_metadata_TrackTrace_points_vector();
    auto &sequence = seq_perception_metadata_TrackTrace_points_vector();
    if (type.tp_name == nullptr) {
        sequence.sq_length = len_perception_metadata_TrackTrace_points_vector;
        sequence.sq_item = item_perception_metadata_TrackTrace_points_vector;
        type.tp_name = "perception_bridge.perception_metadata_TrackTrace_points_vector";
        type.tp_basicsize = sizeof(
            native_proxy_object<std::vector<std::unique_ptr<perception::metadata::Point2fT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc =
            native_proxy_dealloc<std::vector<std::unique_ptr<perception::metadata::Point2fT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t len_perception_metadata_TrackTraces_traces_vector(PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<perception::metadata::TrackTraceT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *item_perception_metadata_TrackTraces_traces_vector(PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<perception::metadata::TrackTraceT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "perception vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_TrackTrace_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<perception::metadata::TrackTraceT>>>(self));
}

bool ensure_perception_metadata_TrackTraces_traces_vector_type() {
    auto &type = pytype_perception_metadata_TrackTraces_traces_vector();
    auto &sequence = seq_perception_metadata_TrackTraces_traces_vector();
    if (type.tp_name == nullptr) {
        sequence.sq_length = len_perception_metadata_TrackTraces_traces_vector;
        sequence.sq_item = item_perception_metadata_TrackTraces_traces_vector;
        type.tp_name = "perception_bridge.perception_metadata_TrackTraces_traces_vector";
        type.tp_basicsize = sizeof(
            native_proxy_object<std::vector<std::unique_ptr<perception::metadata::TrackTraceT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc =
            native_proxy_dealloc<std::vector<std::unique_ptr<perception::metadata::TrackTraceT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *get_perception_metadata_BoxDetection_object(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::BoxDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_ObjectMeta_proxy(
        nested, proxy_anchor<perception::metadata::BoxDetectionT>(self));
}

PyObject *get_perception_metadata_BoxDetection_box(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::BoxDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->box.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_BoundingBox_proxy(
        nested, proxy_anchor<perception::metadata::BoxDetectionT>(self));
}

PyObject *get_perception_metadata_BoxDetection_confidence(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::BoxDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->confidence));
}

PyObject *get_perception_metadata_BoxDetection_class_id(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::BoxDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_signed(value->class_id);
}

PyObject *get_perception_metadata_BoxDetection_text(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::BoxDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->text);
}

PyGetSetDef *getsets_perception_metadata_BoxDetection() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(get_perception_metadata_BoxDetection_object),
         nullptr,
         "read-only object",
         nullptr},
        {"box",
         reinterpret_cast<getter>(get_perception_metadata_BoxDetection_box),
         nullptr,
         "read-only box",
         nullptr},
        {"confidence",
         reinterpret_cast<getter>(get_perception_metadata_BoxDetection_confidence),
         nullptr,
         "read-only confidence",
         nullptr},
        {"classId",
         reinterpret_cast<getter>(get_perception_metadata_BoxDetection_class_id),
         nullptr,
         "read-only class_id",
         nullptr},
        {"text",
         reinterpret_cast<getter>(get_perception_metadata_BoxDetection_text),
         nullptr,
         "read-only text",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_BoxDetection_type() {
    auto &type = pytype_perception_metadata_BoxDetection();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_BoxDetection";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::BoxDetectionT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::BoxDetectionT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_BoxDetection();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_perception_metadata_BoxDetection_proxy(const perception::metadata::BoxDetectionT *value,
                                            std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::BoxDetectionT>(
        pytype_perception_metadata_BoxDetection(), value, std::move(anchor));
}

PyObject *get_perception_metadata_BoxDetections_schema_major(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::BoxDetectionsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *get_perception_metadata_BoxDetections_schema_minor(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::BoxDetectionsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *get_perception_metadata_BoxDetections_layer(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::BoxDetectionsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_LayerInfo_proxy(
        nested, proxy_anchor<perception::metadata::BoxDetectionsT>(self));
}

PyObject *get_perception_metadata_BoxDetections_detections(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::BoxDetectionsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<perception::metadata::BoxDetectionT>>>(
        pytype_perception_metadata_BoxDetections_detections_vector(),
        std::addressof(value->detections),
        proxy_anchor<perception::metadata::BoxDetectionsT>(self));
}

PyGetSetDef *getsets_perception_metadata_BoxDetections() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(get_perception_metadata_BoxDetections_schema_major),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(get_perception_metadata_BoxDetections_schema_minor),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(get_perception_metadata_BoxDetections_layer),
         nullptr,
         "read-only layer",
         nullptr},
        {"detections",
         reinterpret_cast<getter>(get_perception_metadata_BoxDetections_detections),
         nullptr,
         "read-only detections",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_BoxDetections_type() {
    auto &type = pytype_perception_metadata_BoxDetections();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_BoxDetections";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::BoxDetectionsT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::BoxDetectionsT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_BoxDetections();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_perception_metadata_BoxDetections_proxy(const perception::metadata::BoxDetectionsT *value,
                                             std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::BoxDetectionsT>(
        pytype_perception_metadata_BoxDetections(), value, std::move(anchor));
}

PyObject *get_perception_metadata_ClassificationCandidate_confidence(PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<perception::metadata::ClassificationCandidateT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->confidence));
}

PyObject *get_perception_metadata_ClassificationCandidate_class_id(PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<perception::metadata::ClassificationCandidateT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_signed(value->class_id);
}

PyObject *get_perception_metadata_ClassificationCandidate_text(PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<perception::metadata::ClassificationCandidateT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->text);
}

PyObject *get_perception_metadata_ClassificationCandidate_x(PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<perception::metadata::ClassificationCandidateT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->x));
}

PyObject *get_perception_metadata_ClassificationCandidate_y(PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<perception::metadata::ClassificationCandidateT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->y));
}

PyObject *get_perception_metadata_ClassificationCandidate_w(PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<perception::metadata::ClassificationCandidateT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->w));
}

PyObject *get_perception_metadata_ClassificationCandidate_h(PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<perception::metadata::ClassificationCandidateT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->h));
}

PyGetSetDef *getsets_perception_metadata_ClassificationCandidate() {
    static PyGetSetDef definitions[] = {
        {"confidence",
         reinterpret_cast<getter>(get_perception_metadata_ClassificationCandidate_confidence),
         nullptr,
         "read-only confidence",
         nullptr},
        {"classId",
         reinterpret_cast<getter>(get_perception_metadata_ClassificationCandidate_class_id),
         nullptr,
         "read-only class_id",
         nullptr},
        {"text",
         reinterpret_cast<getter>(get_perception_metadata_ClassificationCandidate_text),
         nullptr,
         "read-only text",
         nullptr},
        {"x",
         reinterpret_cast<getter>(get_perception_metadata_ClassificationCandidate_x),
         nullptr,
         "read-only x",
         nullptr},
        {"y",
         reinterpret_cast<getter>(get_perception_metadata_ClassificationCandidate_y),
         nullptr,
         "read-only y",
         nullptr},
        {"w",
         reinterpret_cast<getter>(get_perception_metadata_ClassificationCandidate_w),
         nullptr,
         "read-only w",
         nullptr},
        {"h",
         reinterpret_cast<getter>(get_perception_metadata_ClassificationCandidate_h),
         nullptr,
         "read-only h",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_ClassificationCandidate_type() {
    auto &type = pytype_perception_metadata_ClassificationCandidate();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_ClassificationCandidate";
        type.tp_basicsize =
            sizeof(native_proxy_object<perception::metadata::ClassificationCandidateT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::ClassificationCandidateT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_ClassificationCandidate();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *make_perception_metadata_ClassificationCandidate_proxy(
    const perception::metadata::ClassificationCandidateT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::ClassificationCandidateT>(
        pytype_perception_metadata_ClassificationCandidate(), value, std::move(anchor));
}

PyObject *get_perception_metadata_Classification_object(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ClassificationT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_ObjectMeta_proxy(
        nested, proxy_anchor<perception::metadata::ClassificationT>(self));
}

PyObject *get_perception_metadata_Classification_candidates(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ClassificationT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<perception::metadata::ClassificationCandidateT>>>(
        pytype_perception_metadata_Classification_candidates_vector(),
        std::addressof(value->candidates),
        proxy_anchor<perception::metadata::ClassificationT>(self));
}

PyGetSetDef *getsets_perception_metadata_Classification() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(get_perception_metadata_Classification_object),
         nullptr,
         "read-only object",
         nullptr},
        {"candidates",
         reinterpret_cast<getter>(get_perception_metadata_Classification_candidates),
         nullptr,
         "read-only candidates",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_Classification_type() {
    auto &type = pytype_perception_metadata_Classification();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_Classification";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::ClassificationT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::ClassificationT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_Classification();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_perception_metadata_Classification_proxy(const perception::metadata::ClassificationT *value,
                                              std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::ClassificationT>(
        pytype_perception_metadata_Classification(), value, std::move(anchor));
}

PyObject *get_perception_metadata_PersonPresence_object(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::PersonPresenceT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_ObjectMeta_proxy(
        nested, proxy_anchor<perception::metadata::PersonPresenceT>(self));
}

PyObject *get_perception_metadata_PersonPresence_yes_confidence(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::PersonPresenceT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->yes_confidence));
}

PyObject *get_perception_metadata_PersonPresence_no_confidence(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::PersonPresenceT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->no_confidence));
}

PyGetSetDef *getsets_perception_metadata_PersonPresence() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(get_perception_metadata_PersonPresence_object),
         nullptr,
         "read-only object",
         nullptr},
        {"yesConfidence",
         reinterpret_cast<getter>(get_perception_metadata_PersonPresence_yes_confidence),
         nullptr,
         "read-only yes_confidence",
         nullptr},
        {"noConfidence",
         reinterpret_cast<getter>(get_perception_metadata_PersonPresence_no_confidence),
         nullptr,
         "read-only no_confidence",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_PersonPresence_type() {
    auto &type = pytype_perception_metadata_PersonPresence();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_PersonPresence";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::PersonPresenceT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::PersonPresenceT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_PersonPresence();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_perception_metadata_PersonPresence_proxy(const perception::metadata::PersonPresenceT *value,
                                              std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::PersonPresenceT>(
        pytype_perception_metadata_PersonPresence(), value, std::move(anchor));
}

PyObject *get_perception_metadata_Classifications_schema_major(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ClassificationsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *get_perception_metadata_Classifications_schema_minor(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ClassificationsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *get_perception_metadata_Classifications_layer(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ClassificationsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_LayerInfo_proxy(
        nested, proxy_anchor<perception::metadata::ClassificationsT>(self));
}

PyObject *get_perception_metadata_Classifications_classifications(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ClassificationsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<perception::metadata::ClassificationT>>>(
        pytype_perception_metadata_Classifications_classifications_vector(),
        std::addressof(value->classifications),
        proxy_anchor<perception::metadata::ClassificationsT>(self));
}

PyObject *get_perception_metadata_Classifications_person_presence(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ClassificationsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<perception::metadata::PersonPresenceT>>>(
        pytype_perception_metadata_Classifications_person_presence_vector(),
        std::addressof(value->person_presence),
        proxy_anchor<perception::metadata::ClassificationsT>(self));
}

PyGetSetDef *getsets_perception_metadata_Classifications() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(get_perception_metadata_Classifications_schema_major),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(get_perception_metadata_Classifications_schema_minor),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(get_perception_metadata_Classifications_layer),
         nullptr,
         "read-only layer",
         nullptr},
        {"classifications",
         reinterpret_cast<getter>(get_perception_metadata_Classifications_classifications),
         nullptr,
         "read-only classifications",
         nullptr},
        {"personPresence",
         reinterpret_cast<getter>(get_perception_metadata_Classifications_person_presence),
         nullptr,
         "read-only person_presence",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_Classifications_type() {
    auto &type = pytype_perception_metadata_Classifications();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_Classifications";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::ClassificationsT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::ClassificationsT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_Classifications();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_perception_metadata_Classifications_proxy(const perception::metadata::ClassificationsT *value,
                                               std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::ClassificationsT>(
        pytype_perception_metadata_Classifications(), value, std::move(anchor));
}

PyObject *get_perception_metadata_ObjectMeta_id(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectMetaT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->id);
}

PyObject *get_perception_metadata_ObjectMeta_parent_id(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectMetaT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->parent_id);
}

PyObject *get_perception_metadata_ObjectMeta_creation_ts_ns(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectMetaT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->creation_ts_ns);
}

PyGetSetDef *getsets_perception_metadata_ObjectMeta() {
    static PyGetSetDef definitions[] = {
        {"id",
         reinterpret_cast<getter>(get_perception_metadata_ObjectMeta_id),
         nullptr,
         "read-only id",
         nullptr},
        {"parentId",
         reinterpret_cast<getter>(get_perception_metadata_ObjectMeta_parent_id),
         nullptr,
         "read-only parent_id",
         nullptr},
        {"creationTsNs",
         reinterpret_cast<getter>(get_perception_metadata_ObjectMeta_creation_ts_ns),
         nullptr,
         "read-only creation_ts_ns",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_ObjectMeta_type() {
    auto &type = pytype_perception_metadata_ObjectMeta();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_ObjectMeta";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::ObjectMetaT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::ObjectMetaT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_ObjectMeta();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_perception_metadata_ObjectMeta_proxy(const perception::metadata::ObjectMetaT *value,
                                          std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::ObjectMetaT>(
        pytype_perception_metadata_ObjectMeta(), value, std::move(anchor));
}

PyObject *get_perception_metadata_LayerInfo_engine(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::LayerInfoT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->engine);
}

PyObject *get_perception_metadata_LayerInfo_model(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::LayerInfoT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->model);
}

PyObject *get_perception_metadata_LayerInfo_tags(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::LayerInfoT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->tags);
}

PyObject *get_perception_metadata_LayerInfo_infer_element_id(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::LayerInfoT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->infer_element_id);
}

PyObject *get_perception_metadata_LayerInfo_label_family(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::LayerInfoT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->label_family);
}

PyObject *get_perception_metadata_LayerInfo_content_type(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::LayerInfoT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->content_type);
}

PyObject *get_perception_metadata_LayerInfo_compositing_mode(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::LayerInfoT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->compositing_mode);
}

PyGetSetDef *getsets_perception_metadata_LayerInfo() {
    static PyGetSetDef definitions[] = {
        {"engine",
         reinterpret_cast<getter>(get_perception_metadata_LayerInfo_engine),
         nullptr,
         "read-only engine",
         nullptr},
        {"model",
         reinterpret_cast<getter>(get_perception_metadata_LayerInfo_model),
         nullptr,
         "read-only model",
         nullptr},
        {"tags",
         reinterpret_cast<getter>(get_perception_metadata_LayerInfo_tags),
         nullptr,
         "read-only tags",
         nullptr},
        {"inferElementId",
         reinterpret_cast<getter>(get_perception_metadata_LayerInfo_infer_element_id),
         nullptr,
         "read-only infer_element_id",
         nullptr},
        {"labelFamily",
         reinterpret_cast<getter>(get_perception_metadata_LayerInfo_label_family),
         nullptr,
         "read-only label_family",
         nullptr},
        {"contentType",
         reinterpret_cast<getter>(get_perception_metadata_LayerInfo_content_type),
         nullptr,
         "read-only content_type",
         nullptr},
        {"compositingMode",
         reinterpret_cast<getter>(get_perception_metadata_LayerInfo_compositing_mode),
         nullptr,
         "read-only compositing_mode",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_LayerInfo_type() {
    auto &type = pytype_perception_metadata_LayerInfo();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_LayerInfo";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::LayerInfoT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::LayerInfoT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_LayerInfo();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_perception_metadata_LayerInfo_proxy(const perception::metadata::LayerInfoT *value,
                                         std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::LayerInfoT>(
        pytype_perception_metadata_LayerInfo(), value, std::move(anchor));
}

PyObject *get_perception_metadata_BoundingBox_x(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::BoundingBoxT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->x));
}

PyObject *get_perception_metadata_BoundingBox_y(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::BoundingBoxT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->y));
}

PyObject *get_perception_metadata_BoundingBox_width(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::BoundingBoxT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->width));
}

PyObject *get_perception_metadata_BoundingBox_height(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::BoundingBoxT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->height));
}

PyGetSetDef *getsets_perception_metadata_BoundingBox() {
    static PyGetSetDef definitions[] = {
        {"x",
         reinterpret_cast<getter>(get_perception_metadata_BoundingBox_x),
         nullptr,
         "read-only x",
         nullptr},
        {"y",
         reinterpret_cast<getter>(get_perception_metadata_BoundingBox_y),
         nullptr,
         "read-only y",
         nullptr},
        {"width",
         reinterpret_cast<getter>(get_perception_metadata_BoundingBox_width),
         nullptr,
         "read-only width",
         nullptr},
        {"height",
         reinterpret_cast<getter>(get_perception_metadata_BoundingBox_height),
         nullptr,
         "read-only height",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_BoundingBox_type() {
    auto &type = pytype_perception_metadata_BoundingBox();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_BoundingBox";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::BoundingBoxT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::BoundingBoxT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_BoundingBox();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_perception_metadata_BoundingBox_proxy(const perception::metadata::BoundingBoxT *value,
                                           std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::BoundingBoxT>(
        pytype_perception_metadata_BoundingBox(), value, std::move(anchor));
}

PyObject *get_perception_metadata_Point2f_x(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::Point2fT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->x));
}

PyObject *get_perception_metadata_Point2f_y(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::Point2fT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->y));
}

PyGetSetDef *getsets_perception_metadata_Point2f() {
    static PyGetSetDef definitions[] = {
        {"x",
         reinterpret_cast<getter>(get_perception_metadata_Point2f_x),
         nullptr,
         "read-only x",
         nullptr},
        {"y",
         reinterpret_cast<getter>(get_perception_metadata_Point2f_y),
         nullptr,
         "read-only y",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_Point2f_type() {
    auto &type = pytype_perception_metadata_Point2f();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_Point2f";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::Point2fT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::Point2fT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_Point2f();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_perception_metadata_Point2f_proxy(const perception::metadata::Point2fT *value,
                                       std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::Point2fT>(
        pytype_perception_metadata_Point2f(), value, std::move(anchor));
}

PyObject *get_perception_metadata_BitmapData_width(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::BitmapDataT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->width);
}

PyObject *get_perception_metadata_BitmapData_height(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::BitmapDataT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->height);
}

PyObject *get_perception_metadata_BitmapData_value_type(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::BitmapDataT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->value_type);
}

PyObject *get_perception_metadata_BitmapData_pixels(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::BitmapDataT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<std::vector<std::uint8_t>>(
        pytype_perception_metadata_BitmapData_pixels_vector(),
        std::addressof(value->pixels),
        proxy_anchor<perception::metadata::BitmapDataT>(self));
}

PyGetSetDef *getsets_perception_metadata_BitmapData() {
    static PyGetSetDef definitions[] = {
        {"width",
         reinterpret_cast<getter>(get_perception_metadata_BitmapData_width),
         nullptr,
         "read-only width",
         nullptr},
        {"height",
         reinterpret_cast<getter>(get_perception_metadata_BitmapData_height),
         nullptr,
         "read-only height",
         nullptr},
        {"valueType",
         reinterpret_cast<getter>(get_perception_metadata_BitmapData_value_type),
         nullptr,
         "read-only value_type",
         nullptr},
        {"pixels",
         reinterpret_cast<getter>(get_perception_metadata_BitmapData_pixels),
         nullptr,
         "read-only pixels",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_BitmapData_type() {
    auto &type = pytype_perception_metadata_BitmapData();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_BitmapData";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::BitmapDataT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::BitmapDataT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_BitmapData();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_perception_metadata_BitmapData_proxy(const perception::metadata::BitmapDataT *value,
                                          std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::BitmapDataT>(
        pytype_perception_metadata_BitmapData(), value, std::move(anchor));
}

PyObject *get_perception_metadata_VideoFrameContext_object(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::VideoFrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_ObjectMeta_proxy(
        nested, proxy_anchor<perception::metadata::VideoFrameContextT>(self));
}

PyObject *get_perception_metadata_VideoFrameContext_original_width(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::VideoFrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->original_width);
}

PyObject *get_perception_metadata_VideoFrameContext_original_height(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::VideoFrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->original_height);
}

PyObject *get_perception_metadata_VideoFrameContext_source_crop_left(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::VideoFrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->source_crop_left);
}

PyObject *get_perception_metadata_VideoFrameContext_source_crop_right(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::VideoFrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->source_crop_right);
}

PyObject *get_perception_metadata_VideoFrameContext_source_crop_top(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::VideoFrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->source_crop_top);
}

PyObject *get_perception_metadata_VideoFrameContext_source_crop_bottom(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::VideoFrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->source_crop_bottom);
}

PyObject *get_perception_metadata_VideoFrameContext_letterbox_left(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::VideoFrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->letterbox_left);
}

PyObject *get_perception_metadata_VideoFrameContext_letterbox_right(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::VideoFrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->letterbox_right);
}

PyObject *get_perception_metadata_VideoFrameContext_letterbox_top(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::VideoFrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->letterbox_top);
}

PyObject *get_perception_metadata_VideoFrameContext_letterbox_bottom(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::VideoFrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->letterbox_bottom);
}

PyGetSetDef *getsets_perception_metadata_VideoFrameContext() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(get_perception_metadata_VideoFrameContext_object),
         nullptr,
         "read-only object",
         nullptr},
        {"originalWidth",
         reinterpret_cast<getter>(get_perception_metadata_VideoFrameContext_original_width),
         nullptr,
         "read-only original_width",
         nullptr},
        {"originalHeight",
         reinterpret_cast<getter>(get_perception_metadata_VideoFrameContext_original_height),
         nullptr,
         "read-only original_height",
         nullptr},
        {"sourceCropLeft",
         reinterpret_cast<getter>(get_perception_metadata_VideoFrameContext_source_crop_left),
         nullptr,
         "read-only source_crop_left",
         nullptr},
        {"sourceCropRight",
         reinterpret_cast<getter>(get_perception_metadata_VideoFrameContext_source_crop_right),
         nullptr,
         "read-only source_crop_right",
         nullptr},
        {"sourceCropTop",
         reinterpret_cast<getter>(get_perception_metadata_VideoFrameContext_source_crop_top),
         nullptr,
         "read-only source_crop_top",
         nullptr},
        {"sourceCropBottom",
         reinterpret_cast<getter>(get_perception_metadata_VideoFrameContext_source_crop_bottom),
         nullptr,
         "read-only source_crop_bottom",
         nullptr},
        {"letterboxLeft",
         reinterpret_cast<getter>(get_perception_metadata_VideoFrameContext_letterbox_left),
         nullptr,
         "read-only letterbox_left",
         nullptr},
        {"letterboxRight",
         reinterpret_cast<getter>(get_perception_metadata_VideoFrameContext_letterbox_right),
         nullptr,
         "read-only letterbox_right",
         nullptr},
        {"letterboxTop",
         reinterpret_cast<getter>(get_perception_metadata_VideoFrameContext_letterbox_top),
         nullptr,
         "read-only letterbox_top",
         nullptr},
        {"letterboxBottom",
         reinterpret_cast<getter>(get_perception_metadata_VideoFrameContext_letterbox_bottom),
         nullptr,
         "read-only letterbox_bottom",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_VideoFrameContext_type() {
    auto &type = pytype_perception_metadata_VideoFrameContext();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_VideoFrameContext";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::VideoFrameContextT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::VideoFrameContextT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_VideoFrameContext();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *make_perception_metadata_VideoFrameContext_proxy(
    const perception::metadata::VideoFrameContextT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::VideoFrameContextT>(
        pytype_perception_metadata_VideoFrameContext(), value, std::move(anchor));
}

PyObject *get_perception_metadata_AudioFrameContext_object(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::AudioFrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_ObjectMeta_proxy(
        nested, proxy_anchor<perception::metadata::AudioFrameContextT>(self));
}

PyObject *get_perception_metadata_AudioFrameContext_original_channels(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::AudioFrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->original_channels);
}

PyObject *get_perception_metadata_AudioFrameContext_original_frequency(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::AudioFrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->original_frequency);
}

PyObject *get_perception_metadata_AudioFrameContext_original_sample_count(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::AudioFrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->original_sample_count);
}

PyObject *get_perception_metadata_AudioFrameContext_cut_left_sample_count(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::AudioFrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->cut_left_sample_count);
}

PyObject *get_perception_metadata_AudioFrameContext_cut_right_sample_count(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::AudioFrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->cut_right_sample_count);
}

PyGetSetDef *getsets_perception_metadata_AudioFrameContext() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(get_perception_metadata_AudioFrameContext_object),
         nullptr,
         "read-only object",
         nullptr},
        {"originalChannels",
         reinterpret_cast<getter>(get_perception_metadata_AudioFrameContext_original_channels),
         nullptr,
         "read-only original_channels",
         nullptr},
        {"originalFrequency",
         reinterpret_cast<getter>(get_perception_metadata_AudioFrameContext_original_frequency),
         nullptr,
         "read-only original_frequency",
         nullptr},
        {"originalSampleCount",
         reinterpret_cast<getter>(get_perception_metadata_AudioFrameContext_original_sample_count),
         nullptr,
         "read-only original_sample_count",
         nullptr},
        {"cutLeftSampleCount",
         reinterpret_cast<getter>(get_perception_metadata_AudioFrameContext_cut_left_sample_count),
         nullptr,
         "read-only cut_left_sample_count",
         nullptr},
        {"cutRightSampleCount",
         reinterpret_cast<getter>(get_perception_metadata_AudioFrameContext_cut_right_sample_count),
         nullptr,
         "read-only cut_right_sample_count",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_AudioFrameContext_type() {
    auto &type = pytype_perception_metadata_AudioFrameContext();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_AudioFrameContext";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::AudioFrameContextT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::AudioFrameContextT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_AudioFrameContext();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *make_perception_metadata_AudioFrameContext_proxy(
    const perception::metadata::AudioFrameContextT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::AudioFrameContextT>(
        pytype_perception_metadata_AudioFrameContext(), value, std::move(anchor));
}

PyObject *get_perception_metadata_FrameContext_schema_major(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::FrameContextT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *get_perception_metadata_FrameContext_schema_minor(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::FrameContextT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *get_perception_metadata_FrameContext_layer(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::FrameContextT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_LayerInfo_proxy(
        nested, proxy_anchor<perception::metadata::FrameContextT>(self));
}

PyObject *get_perception_metadata_FrameContext_video(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::FrameContextT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->video.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_VideoFrameContext_proxy(
        nested, proxy_anchor<perception::metadata::FrameContextT>(self));
}

PyObject *get_perception_metadata_FrameContext_audio(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::FrameContextT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->audio.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_AudioFrameContext_proxy(
        nested, proxy_anchor<perception::metadata::FrameContextT>(self));
}

PyGetSetDef *getsets_perception_metadata_FrameContext() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(get_perception_metadata_FrameContext_schema_major),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(get_perception_metadata_FrameContext_schema_minor),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(get_perception_metadata_FrameContext_layer),
         nullptr,
         "read-only layer",
         nullptr},
        {"video",
         reinterpret_cast<getter>(get_perception_metadata_FrameContext_video),
         nullptr,
         "read-only video",
         nullptr},
        {"audio",
         reinterpret_cast<getter>(get_perception_metadata_FrameContext_audio),
         nullptr,
         "read-only audio",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_FrameContext_type() {
    auto &type = pytype_perception_metadata_FrameContext();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_FrameContext";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::FrameContextT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::FrameContextT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_FrameContext();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_perception_metadata_FrameContext_proxy(const perception::metadata::FrameContextT *value,
                                            std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::FrameContextT>(
        pytype_perception_metadata_FrameContext(), value, std::move(anchor));
}

PyObject *get_perception_metadata_ObjectEmbedding_object(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectEmbeddingT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_ObjectMeta_proxy(
        nested, proxy_anchor<perception::metadata::ObjectEmbeddingT>(self));
}

PyObject *get_perception_metadata_ObjectEmbedding_values(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectEmbeddingT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<std::vector<float>>(
        pytype_perception_metadata_ObjectEmbedding_values_vector(),
        std::addressof(value->values),
        proxy_anchor<perception::metadata::ObjectEmbeddingT>(self));
}

PyGetSetDef *getsets_perception_metadata_ObjectEmbedding() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(get_perception_metadata_ObjectEmbedding_object),
         nullptr,
         "read-only object",
         nullptr},
        {"values",
         reinterpret_cast<getter>(get_perception_metadata_ObjectEmbedding_values),
         nullptr,
         "read-only values",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_ObjectEmbedding_type() {
    auto &type = pytype_perception_metadata_ObjectEmbedding();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_ObjectEmbedding";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::ObjectEmbeddingT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::ObjectEmbeddingT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_ObjectEmbedding();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_perception_metadata_ObjectEmbedding_proxy(const perception::metadata::ObjectEmbeddingT *value,
                                               std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::ObjectEmbeddingT>(
        pytype_perception_metadata_ObjectEmbedding(), value, std::move(anchor));
}

PyObject *get_perception_metadata_ObjectEmbeddings_schema_major(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectEmbeddingsT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *get_perception_metadata_ObjectEmbeddings_schema_minor(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectEmbeddingsT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *get_perception_metadata_ObjectEmbeddings_layer(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectEmbeddingsT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_LayerInfo_proxy(
        nested, proxy_anchor<perception::metadata::ObjectEmbeddingsT>(self));
}

PyObject *get_perception_metadata_ObjectEmbeddings_embeddings(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectEmbeddingsT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<perception::metadata::ObjectEmbeddingT>>>(
        pytype_perception_metadata_ObjectEmbeddings_embeddings_vector(),
        std::addressof(value->embeddings),
        proxy_anchor<perception::metadata::ObjectEmbeddingsT>(self));
}

PyGetSetDef *getsets_perception_metadata_ObjectEmbeddings() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(get_perception_metadata_ObjectEmbeddings_schema_major),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(get_perception_metadata_ObjectEmbeddings_schema_minor),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(get_perception_metadata_ObjectEmbeddings_layer),
         nullptr,
         "read-only layer",
         nullptr},
        {"embeddings",
         reinterpret_cast<getter>(get_perception_metadata_ObjectEmbeddings_embeddings),
         nullptr,
         "read-only embeddings",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_ObjectEmbeddings_type() {
    auto &type = pytype_perception_metadata_ObjectEmbeddings();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_ObjectEmbeddings";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::ObjectEmbeddingsT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::ObjectEmbeddingsT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_ObjectEmbeddings();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *make_perception_metadata_ObjectEmbeddings_proxy(
    const perception::metadata::ObjectEmbeddingsT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::ObjectEmbeddingsT>(
        pytype_perception_metadata_ObjectEmbeddings(), value, std::move(anchor));
}

PyObject *get_perception_metadata_ObjectTrack_object(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectTrackT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_ObjectMeta_proxy(
        nested, proxy_anchor<perception::metadata::ObjectTrackT>(self));
}

PyObject *get_perception_metadata_ObjectTrack_source_id(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectTrackT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->source_id);
}

PyObject *get_perception_metadata_ObjectTrack_track_id(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectTrackT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->track_id);
}

PyObject *get_perception_metadata_ObjectTrack_box(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectTrackT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->box.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_BoundingBox_proxy(
        nested, proxy_anchor<perception::metadata::ObjectTrackT>(self));
}

PyObject *get_perception_metadata_ObjectTrack_confidence(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectTrackT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->confidence));
}

PyObject *get_perception_metadata_ObjectTrack_class_id(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectTrackT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_signed(value->class_id);
}

PyObject *get_perception_metadata_ObjectTrack_text(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectTrackT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->text);
}

PyObject *get_perception_metadata_ObjectTrack_diagnostic(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectTrackT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->diagnostic);
}

PyObject *get_perception_metadata_ObjectTrack_predicted_only(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectTrackT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    if (value->predicted_only) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

PyGetSetDef *getsets_perception_metadata_ObjectTrack() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(get_perception_metadata_ObjectTrack_object),
         nullptr,
         "read-only object",
         nullptr},
        {"sourceId",
         reinterpret_cast<getter>(get_perception_metadata_ObjectTrack_source_id),
         nullptr,
         "read-only source_id",
         nullptr},
        {"trackId",
         reinterpret_cast<getter>(get_perception_metadata_ObjectTrack_track_id),
         nullptr,
         "read-only track_id",
         nullptr},
        {"box",
         reinterpret_cast<getter>(get_perception_metadata_ObjectTrack_box),
         nullptr,
         "read-only box",
         nullptr},
        {"confidence",
         reinterpret_cast<getter>(get_perception_metadata_ObjectTrack_confidence),
         nullptr,
         "read-only confidence",
         nullptr},
        {"classId",
         reinterpret_cast<getter>(get_perception_metadata_ObjectTrack_class_id),
         nullptr,
         "read-only class_id",
         nullptr},
        {"text",
         reinterpret_cast<getter>(get_perception_metadata_ObjectTrack_text),
         nullptr,
         "read-only text",
         nullptr},
        {"diagnostic",
         reinterpret_cast<getter>(get_perception_metadata_ObjectTrack_diagnostic),
         nullptr,
         "read-only diagnostic",
         nullptr},
        {"predictedOnly",
         reinterpret_cast<getter>(get_perception_metadata_ObjectTrack_predicted_only),
         nullptr,
         "read-only predicted_only",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_ObjectTrack_type() {
    auto &type = pytype_perception_metadata_ObjectTrack();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_ObjectTrack";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::ObjectTrackT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::ObjectTrackT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_ObjectTrack();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_perception_metadata_ObjectTrack_proxy(const perception::metadata::ObjectTrackT *value,
                                           std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::ObjectTrackT>(
        pytype_perception_metadata_ObjectTrack(), value, std::move(anchor));
}

PyObject *get_perception_metadata_ObjectTracks_schema_major(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectTracksT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *get_perception_metadata_ObjectTracks_schema_minor(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectTracksT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *get_perception_metadata_ObjectTracks_layer(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectTracksT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_LayerInfo_proxy(
        nested, proxy_anchor<perception::metadata::ObjectTracksT>(self));
}

PyObject *get_perception_metadata_ObjectTracks_tracks(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::ObjectTracksT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<perception::metadata::ObjectTrackT>>>(
        pytype_perception_metadata_ObjectTracks_tracks_vector(),
        std::addressof(value->tracks),
        proxy_anchor<perception::metadata::ObjectTracksT>(self));
}

PyGetSetDef *getsets_perception_metadata_ObjectTracks() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(get_perception_metadata_ObjectTracks_schema_major),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(get_perception_metadata_ObjectTracks_schema_minor),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(get_perception_metadata_ObjectTracks_layer),
         nullptr,
         "read-only layer",
         nullptr},
        {"tracks",
         reinterpret_cast<getter>(get_perception_metadata_ObjectTracks_tracks),
         nullptr,
         "read-only tracks",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_ObjectTracks_type() {
    auto &type = pytype_perception_metadata_ObjectTracks();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_ObjectTracks";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::ObjectTracksT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::ObjectTracksT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_ObjectTracks();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_perception_metadata_ObjectTracks_proxy(const perception::metadata::ObjectTracksT *value,
                                            std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::ObjectTracksT>(
        pytype_perception_metadata_ObjectTracks(), value, std::move(anchor));
}

PyObject *get_perception_metadata_PerformanceOverlay_schema_major(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::PerformanceOverlayT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *get_perception_metadata_PerformanceOverlay_schema_minor(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::PerformanceOverlayT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *get_perception_metadata_PerformanceOverlay_lines(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::PerformanceOverlayT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<std::vector<std::string>>(
        pytype_perception_metadata_PerformanceOverlay_lines_vector(),
        std::addressof(value->lines),
        proxy_anchor<perception::metadata::PerformanceOverlayT>(self));
}

PyGetSetDef *getsets_perception_metadata_PerformanceOverlay() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(get_perception_metadata_PerformanceOverlay_schema_major),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(get_perception_metadata_PerformanceOverlay_schema_minor),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"lines",
         reinterpret_cast<getter>(get_perception_metadata_PerformanceOverlay_lines),
         nullptr,
         "read-only lines",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_PerformanceOverlay_type() {
    auto &type = pytype_perception_metadata_PerformanceOverlay();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_PerformanceOverlay";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::PerformanceOverlayT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::PerformanceOverlayT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_PerformanceOverlay();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *make_perception_metadata_PerformanceOverlay_proxy(
    const perception::metadata::PerformanceOverlayT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::PerformanceOverlayT>(
        pytype_perception_metadata_PerformanceOverlay(), value, std::move(anchor));
}

PyObject *get_perception_metadata_PoseEstimation_object(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::PoseEstimationT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_ObjectMeta_proxy(
        nested, proxy_anchor<perception::metadata::PoseEstimationT>(self));
}

PyObject *get_perception_metadata_PoseEstimation_confidence(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::PoseEstimationT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->confidence));
}

PyObject *get_perception_metadata_PoseEstimation_yaw(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::PoseEstimationT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->yaw));
}

PyObject *get_perception_metadata_PoseEstimation_pitch(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::PoseEstimationT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->pitch));
}

PyGetSetDef *getsets_perception_metadata_PoseEstimation() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(get_perception_metadata_PoseEstimation_object),
         nullptr,
         "read-only object",
         nullptr},
        {"confidence",
         reinterpret_cast<getter>(get_perception_metadata_PoseEstimation_confidence),
         nullptr,
         "read-only confidence",
         nullptr},
        {"yaw",
         reinterpret_cast<getter>(get_perception_metadata_PoseEstimation_yaw),
         nullptr,
         "read-only yaw",
         nullptr},
        {"pitch",
         reinterpret_cast<getter>(get_perception_metadata_PoseEstimation_pitch),
         nullptr,
         "read-only pitch",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_PoseEstimation_type() {
    auto &type = pytype_perception_metadata_PoseEstimation();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_PoseEstimation";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::PoseEstimationT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::PoseEstimationT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_PoseEstimation();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_perception_metadata_PoseEstimation_proxy(const perception::metadata::PoseEstimationT *value,
                                              std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::PoseEstimationT>(
        pytype_perception_metadata_PoseEstimation(), value, std::move(anchor));
}

PyObject *get_perception_metadata_PoseEstimations_schema_major(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::PoseEstimationsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *get_perception_metadata_PoseEstimations_schema_minor(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::PoseEstimationsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *get_perception_metadata_PoseEstimations_layer(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::PoseEstimationsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_LayerInfo_proxy(
        nested, proxy_anchor<perception::metadata::PoseEstimationsT>(self));
}

PyObject *get_perception_metadata_PoseEstimations_poses(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::PoseEstimationsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<perception::metadata::PoseEstimationT>>>(
        pytype_perception_metadata_PoseEstimations_poses_vector(),
        std::addressof(value->poses),
        proxy_anchor<perception::metadata::PoseEstimationsT>(self));
}

PyGetSetDef *getsets_perception_metadata_PoseEstimations() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(get_perception_metadata_PoseEstimations_schema_major),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(get_perception_metadata_PoseEstimations_schema_minor),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(get_perception_metadata_PoseEstimations_layer),
         nullptr,
         "read-only layer",
         nullptr},
        {"poses",
         reinterpret_cast<getter>(get_perception_metadata_PoseEstimations_poses),
         nullptr,
         "read-only poses",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_PoseEstimations_type() {
    auto &type = pytype_perception_metadata_PoseEstimations();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_PoseEstimations";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::PoseEstimationsT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::PoseEstimationsT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_PoseEstimations();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_perception_metadata_PoseEstimations_proxy(const perception::metadata::PoseEstimationsT *value,
                                               std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::PoseEstimationsT>(
        pytype_perception_metadata_PoseEstimations(), value, std::move(anchor));
}

PyObject *get_perception_metadata_SegmentationMask_object(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::SegmentationMaskT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_ObjectMeta_proxy(
        nested, proxy_anchor<perception::metadata::SegmentationMaskT>(self));
}

PyObject *get_perception_metadata_SegmentationMask_bitmap(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::SegmentationMaskT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->bitmap.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_BitmapData_proxy(
        nested, proxy_anchor<perception::metadata::SegmentationMaskT>(self));
}

PyGetSetDef *getsets_perception_metadata_SegmentationMask() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(get_perception_metadata_SegmentationMask_object),
         nullptr,
         "read-only object",
         nullptr},
        {"bitmap",
         reinterpret_cast<getter>(get_perception_metadata_SegmentationMask_bitmap),
         nullptr,
         "read-only bitmap",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_SegmentationMask_type() {
    auto &type = pytype_perception_metadata_SegmentationMask();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_SegmentationMask";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::SegmentationMaskT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::SegmentationMaskT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_SegmentationMask();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *make_perception_metadata_SegmentationMask_proxy(
    const perception::metadata::SegmentationMaskT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::SegmentationMaskT>(
        pytype_perception_metadata_SegmentationMask(), value, std::move(anchor));
}

PyObject *get_perception_metadata_SegmentationMasks_schema_major(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::SegmentationMasksT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *get_perception_metadata_SegmentationMasks_schema_minor(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::SegmentationMasksT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *get_perception_metadata_SegmentationMasks_layer(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::SegmentationMasksT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_LayerInfo_proxy(
        nested, proxy_anchor<perception::metadata::SegmentationMasksT>(self));
}

PyObject *get_perception_metadata_SegmentationMasks_masks(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::SegmentationMasksT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<perception::metadata::SegmentationMaskT>>>(
        pytype_perception_metadata_SegmentationMasks_masks_vector(),
        std::addressof(value->masks),
        proxy_anchor<perception::metadata::SegmentationMasksT>(self));
}

PyGetSetDef *getsets_perception_metadata_SegmentationMasks() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(get_perception_metadata_SegmentationMasks_schema_major),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(get_perception_metadata_SegmentationMasks_schema_minor),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(get_perception_metadata_SegmentationMasks_layer),
         nullptr,
         "read-only layer",
         nullptr},
        {"masks",
         reinterpret_cast<getter>(get_perception_metadata_SegmentationMasks_masks),
         nullptr,
         "read-only masks",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_SegmentationMasks_type() {
    auto &type = pytype_perception_metadata_SegmentationMasks();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_SegmentationMasks";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::SegmentationMasksT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::SegmentationMasksT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_SegmentationMasks();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *make_perception_metadata_SegmentationMasks_proxy(
    const perception::metadata::SegmentationMasksT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::SegmentationMasksT>(
        pytype_perception_metadata_SegmentationMasks(), value, std::move(anchor));
}

PyObject *get_perception_metadata_TrackTrace_object(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::TrackTraceT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_ObjectMeta_proxy(
        nested, proxy_anchor<perception::metadata::TrackTraceT>(self));
}

PyObject *get_perception_metadata_TrackTrace_track_id(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::TrackTraceT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->track_id);
}

PyObject *get_perception_metadata_TrackTrace_points(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::TrackTraceT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<std::vector<std::unique_ptr<perception::metadata::Point2fT>>>(
        pytype_perception_metadata_TrackTrace_points_vector(),
        std::addressof(value->points),
        proxy_anchor<perception::metadata::TrackTraceT>(self));
}

PyGetSetDef *getsets_perception_metadata_TrackTrace() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(get_perception_metadata_TrackTrace_object),
         nullptr,
         "read-only object",
         nullptr},
        {"trackId",
         reinterpret_cast<getter>(get_perception_metadata_TrackTrace_track_id),
         nullptr,
         "read-only track_id",
         nullptr},
        {"points",
         reinterpret_cast<getter>(get_perception_metadata_TrackTrace_points),
         nullptr,
         "read-only points",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_TrackTrace_type() {
    auto &type = pytype_perception_metadata_TrackTrace();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_TrackTrace";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::TrackTraceT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::TrackTraceT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_TrackTrace();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_perception_metadata_TrackTrace_proxy(const perception::metadata::TrackTraceT *value,
                                          std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::TrackTraceT>(
        pytype_perception_metadata_TrackTrace(), value, std::move(anchor));
}

PyObject *get_perception_metadata_TrackTraces_schema_major(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::TrackTracesT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *get_perception_metadata_TrackTraces_schema_minor(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::TrackTracesT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *get_perception_metadata_TrackTraces_layer(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::TrackTracesT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_perception_metadata_LayerInfo_proxy(
        nested, proxy_anchor<perception::metadata::TrackTracesT>(self));
}

PyObject *get_perception_metadata_TrackTraces_traces(PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<perception::metadata::TrackTracesT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "perception payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<perception::metadata::TrackTraceT>>>(
        pytype_perception_metadata_TrackTraces_traces_vector(),
        std::addressof(value->traces),
        proxy_anchor<perception::metadata::TrackTracesT>(self));
}

PyGetSetDef *getsets_perception_metadata_TrackTraces() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(get_perception_metadata_TrackTraces_schema_major),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(get_perception_metadata_TrackTraces_schema_minor),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(get_perception_metadata_TrackTraces_layer),
         nullptr,
         "read-only layer",
         nullptr},
        {"traces",
         reinterpret_cast<getter>(get_perception_metadata_TrackTraces_traces),
         nullptr,
         "read-only traces",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_perception_metadata_TrackTraces_type() {
    auto &type = pytype_perception_metadata_TrackTraces();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.perception_metadata_TrackTraces";
        type.tp_basicsize = sizeof(native_proxy_object<perception::metadata::TrackTracesT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<perception::metadata::TrackTracesT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception read-only live payload proxy";
        type.tp_getset = getsets_perception_metadata_TrackTraces();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_perception_metadata_TrackTraces_proxy(const perception::metadata::TrackTracesT *value,
                                           std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<perception::metadata::TrackTracesT>(
        pytype_perception_metadata_TrackTraces(), value, std::move(anchor));
}

bool ensure_known_proxy_types() {
    if (!ensure_perception_metadata_BoxDetections_detections_vector_type() ||
        !ensure_perception_metadata_Classification_candidates_vector_type() ||
        !ensure_perception_metadata_Classifications_classifications_vector_type() ||
        !ensure_perception_metadata_Classifications_person_presence_vector_type() ||
        !ensure_perception_metadata_BitmapData_pixels_vector_type() ||
        !ensure_perception_metadata_ObjectEmbedding_values_vector_type() ||
        !ensure_perception_metadata_ObjectEmbeddings_embeddings_vector_type() ||
        !ensure_perception_metadata_ObjectTracks_tracks_vector_type() ||
        !ensure_perception_metadata_PerformanceOverlay_lines_vector_type() ||
        !ensure_perception_metadata_PoseEstimations_poses_vector_type() ||
        !ensure_perception_metadata_SegmentationMasks_masks_vector_type() ||
        !ensure_perception_metadata_TrackTrace_points_vector_type() ||
        !ensure_perception_metadata_TrackTraces_traces_vector_type() ||
        !ensure_perception_metadata_BoxDetection_type() ||
        !ensure_perception_metadata_BoxDetections_type() ||
        !ensure_perception_metadata_ClassificationCandidate_type() ||
        !ensure_perception_metadata_Classification_type() ||
        !ensure_perception_metadata_PersonPresence_type() ||
        !ensure_perception_metadata_Classifications_type() ||
        !ensure_perception_metadata_ObjectMeta_type() ||
        !ensure_perception_metadata_LayerInfo_type() ||
        !ensure_perception_metadata_BoundingBox_type() ||
        !ensure_perception_metadata_Point2f_type() ||
        !ensure_perception_metadata_BitmapData_type() ||
        !ensure_perception_metadata_VideoFrameContext_type() ||
        !ensure_perception_metadata_AudioFrameContext_type() ||
        !ensure_perception_metadata_FrameContext_type() ||
        !ensure_perception_metadata_ObjectEmbedding_type() ||
        !ensure_perception_metadata_ObjectEmbeddings_type() ||
        !ensure_perception_metadata_ObjectTrack_type() ||
        !ensure_perception_metadata_ObjectTracks_type() ||
        !ensure_perception_metadata_PerformanceOverlay_type() ||
        !ensure_perception_metadata_PoseEstimation_type() ||
        !ensure_perception_metadata_PoseEstimations_type() ||
        !ensure_perception_metadata_SegmentationMask_type() ||
        !ensure_perception_metadata_SegmentationMasks_type() ||
        !ensure_perception_metadata_TrackTrace_type() ||
        !ensure_perception_metadata_TrackTraces_type()) {
        return false;
    }
    return true;
}

bool add_python_type(PyObject *python_module, PyTypeObject &type, const char *name) {
    Py_INCREF(&type);
    if (PyModule_AddObject(python_module, name, reinterpret_cast<PyObject *>(&type)) < 0) {
        Py_DECREF(&type);
        return false;
    }
    return true;
}

bool add_known_proxy_types(PyObject *python_module) {
    if (!add_python_type(python_module,
                         pytype_perception_metadata_BoxDetection(),
                         "perception_metadata_BoxDetection")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_BoxDetections(),
                         "perception_metadata_BoxDetections")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_ClassificationCandidate(),
                         "perception_metadata_ClassificationCandidate")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_Classification(),
                         "perception_metadata_Classification")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_PersonPresence(),
                         "perception_metadata_PersonPresence")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_Classifications(),
                         "perception_metadata_Classifications")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_ObjectMeta(),
                         "perception_metadata_ObjectMeta")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_LayerInfo(),
                         "perception_metadata_LayerInfo")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_BoundingBox(),
                         "perception_metadata_BoundingBox")) {
        return false;
    }
    if (!add_python_type(
            python_module, pytype_perception_metadata_Point2f(), "perception_metadata_Point2f")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_BitmapData(),
                         "perception_metadata_BitmapData")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_VideoFrameContext(),
                         "perception_metadata_VideoFrameContext")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_AudioFrameContext(),
                         "perception_metadata_AudioFrameContext")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_FrameContext(),
                         "perception_metadata_FrameContext")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_ObjectEmbedding(),
                         "perception_metadata_ObjectEmbedding")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_ObjectEmbeddings(),
                         "perception_metadata_ObjectEmbeddings")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_ObjectTrack(),
                         "perception_metadata_ObjectTrack")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_ObjectTracks(),
                         "perception_metadata_ObjectTracks")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_PerformanceOverlay(),
                         "perception_metadata_PerformanceOverlay")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_PoseEstimation(),
                         "perception_metadata_PoseEstimation")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_PoseEstimations(),
                         "perception_metadata_PoseEstimations")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_SegmentationMask(),
                         "perception_metadata_SegmentationMask")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_SegmentationMasks(),
                         "perception_metadata_SegmentationMasks")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_TrackTrace(),
                         "perception_metadata_TrackTrace")) {
        return false;
    }
    if (!add_python_type(python_module,
                         pytype_perception_metadata_TrackTraces(),
                         "perception_metadata_TrackTraces")) {
        return false;
    }
    return true;
}

PyMethodDef *live_envelope_methods() {
    static PyMethodDef methods[] = {
        {"valid",
         py_c_function(live_envelope_is_valid),
         METH_NOARGS,
         "Return True while this live envelope wrapper is valid."},
        {"producer_identity",
         py_c_function(live_envelope_producer_identity),
         METH_NOARGS,
         "Return the producer identity comparison status."},
        {"count",
         py_c_function(live_envelope_count),
         METH_VARARGS,
         "Count known payloads by generated Python payload type or external bytes by ExternalKey."},
        {"contains",
         py_c_function(live_envelope_contains),
         METH_VARARGS,
         "Return True if a known payload or external payload exists for the selector."},
        {"get",
         py_c_function(live_envelope_get),
         METH_VARARGS | METH_KEYWORDS,
         "Return a read-only live proxy for a known payload or bytes for an ExternalKey."},
        {"for_each",
         py_c_function(live_envelope_for_each),
         METH_VARARGS,
         "Return read-only live proxies or external bytes for all entries matching the selector."},
        {"add",
         py_c_function(live_envelope_add),
         METH_VARARGS,
         "Append a known payload value or external bytes with an ExternalKey."},
        {nullptr, nullptr, 0, nullptr},
    };
    return methods;
}

PyGetSetDef *live_envelope_getset() {
    static PyGetSetDef definitions[] = {
        {
            "producer_sdk_name",
            live_envelope_producer_sdk_name,
            nullptr,
            "SDK name recorded in the input envelope.",
            nullptr,
        },
        {
            "producer_sdk_version",
            live_envelope_producer_sdk_version,
            nullptr,
            "SDK version recorded in the input envelope.",
            nullptr,
        },
        {
            "producer_schema_set_sha256",
            live_envelope_producer_schema_set_sha256,
            nullptr,
            "Schema-set digest recorded in the input envelope.",
            nullptr,
        },
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

PyTypeObject &live_envelope_type() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

bool ensure_live_envelope_type() {
    auto &type = live_envelope_type();
    if (type.tp_name == nullptr) {
        type.tp_name = "perception_bridge.Envelope";
        type.tp_basicsize = sizeof(live_envelope_object);
        type.tp_itemsize = 0;
        type.tp_dealloc = live_envelope_dealloc;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "perception live C++ envelope wrapper";
        type.tp_methods = live_envelope_methods();
        type.tp_getset = live_envelope_getset();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

live_envelope_object *as_live_envelope(PyObject *object) noexcept {
    if (object == nullptr || !PyObject_TypeCheck(object, &live_envelope_type())) {
        return nullptr;
    }
    return reinterpret_cast<live_envelope_object *>(object);
}

PyModuleDef &bridge_module() {
    static PyModuleDef definition = {
        PyModuleDef_HEAD_INIT,
        "perception_bridge",
        "perception embedded Python bridge for live C++ envelopes",
        -1,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    };
    return definition;
}

PyObject *init_module_impl() {
    if (!ensure_live_envelope_type()) {
        return nullptr;
    }

    if (!ensure_known_proxy_types()) {
        return nullptr;
    }

    PyObject *python_module = PyModule_Create(&bridge_module());
    if (python_module == nullptr) {
        return nullptr;
    }

    PyObject *bridge_error =
        PyErr_NewException("perception_bridge.BridgeError", PyExc_RuntimeError, nullptr);
    if (bridge_error == nullptr) {
        Py_DECREF(python_module);
        return nullptr;
    }
    if (PyModule_AddObject(python_module, "BridgeError", bridge_error) < 0) {
        Py_DECREF(bridge_error);
        Py_DECREF(python_module);
        return nullptr;
    }

    auto &envelope_type = live_envelope_type();
    Py_INCREF(&envelope_type);
    if (PyModule_AddObject(
            python_module, "Envelope", reinterpret_cast<PyObject *>(&envelope_type)) < 0) {
        Py_DECREF(&envelope_type);
        Py_DECREF(python_module);
        return nullptr;
    }

    if (!add_known_proxy_types(python_module)) {
        Py_DECREF(python_module);
        return nullptr;
    }

    return python_module;
}

} // namespace
} // namespace perception::python_bridge

extern "C" PyObject *PyInit_perception_bridge() {
    return perception::python_bridge::init_module_impl();
}

namespace perception::python_bridge {

void append_inittab() {
    if (PyImport_AppendInittab("perception_bridge", &PyInit_perception_bridge) != 0) {
        throw std::runtime_error("failed to register perception_bridge Python bridge module");
    }
}

PyObject *wrap(container::envelope &envelope) {
    if (!Py_IsInitialized()) {
        return nullptr;
    }

    if (!ensure_live_envelope_type()) {
        return nullptr;
    }

    if (!ensure_known_proxy_types()) {
        return nullptr;
    }

    auto *object = PyObject_New(live_envelope_object, &live_envelope_type());
    if (object == nullptr) {
        return nullptr;
    }

    object->envelope = &envelope;
    object->valid = true;
    return reinterpret_cast<PyObject *>(object);
}

bool invalidate(PyObject *object) noexcept {
    auto *live = as_live_envelope(object);
    if (live == nullptr) {
        return false;
    }

    live->envelope = nullptr;
    live->valid = false;
    return true;
}

scoped_envelope::scoped_envelope(container::envelope &envelope) : object_(wrap(envelope)) {
    if (object_ == nullptr) {
        throw std::runtime_error("failed to create perception_bridge live envelope wrapper");
    }
}

scoped_envelope::scoped_envelope(scoped_envelope &&other) noexcept
    : object_(std::exchange(other.object_, nullptr)) {}

scoped_envelope &scoped_envelope::operator=(scoped_envelope &&other) noexcept {
    if (this != &other) {
        if (object_ != nullptr) {
            if (!Py_IsInitialized()) {
                object_ = nullptr;
            } else {
                const auto gil = PyGILState_Ensure();
                invalidate(object_);
                Py_DECREF(object_);
                PyGILState_Release(gil);
            }
        }
        object_ = std::exchange(other.object_, nullptr);
    }
    return *this;
}

scoped_envelope::~scoped_envelope() {
    if (object_ == nullptr) {
        return;
    }

    if (!Py_IsInitialized()) {
        object_ = nullptr;
        return;
    }

    const auto gil = PyGILState_Ensure();
    invalidate(object_);
    Py_DECREF(object_);
    PyGILState_Release(gil);
}

PyObject *scoped_envelope::py_object() const noexcept {
    return object_;
}

} // namespace perception::python_bridge
