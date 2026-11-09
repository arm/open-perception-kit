/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.

#include "open_perception_kit_python_bridge.h"

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

namespace open_perception_kit::python_bridge {

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

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e_proxy(
    const open_perception_kit::metadata::BoxDetectionT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73_proxy(
    const open_perception_kit::metadata::BoxDetectionsT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374526573756c74() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374526573756c74_proxy(
    const open_perception_kit::metadata::CameraContactResultT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374_proxy(
    const open_perception_kit::metadata::CameraContactT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473_proxy(
    const open_perception_kit::metadata::CameraContactsT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e43616e646964617465() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e43616e646964617465_proxy(
    const open_perception_kit::metadata::ClassificationCandidateT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e_proxy(
    const open_perception_kit::metadata::ClassificationT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73_proxy(
    const open_perception_kit::metadata::ClassificationsT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_proxy(
    const open_perception_kit::metadata::ObjectMetaT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e50726f6475636572496e666f() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e50726f6475636572496e666f_proxy(
    const open_perception_kit::metadata::ProducerInfoT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_proxy(
    const open_perception_kit::metadata::LayerInfoT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_proxy(
    const open_perception_kit::metadata::ImageSpaceT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78_proxy(
    const open_perception_kit::metadata::BoundingBoxT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506f696e743266() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506f696e743266_proxy(
    const open_perception_kit::metadata::Point2fT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461_proxy(
    const open_perception_kit::metadata::BitmapDataT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f4672616d65436f6e74657874() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f4672616d65436f6e74657874_proxy(
    const open_perception_kit::metadata::VideoFrameContextT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874_proxy(
    const open_perception_kit::metadata::AudioFrameContextT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874_proxy(
    const open_perception_kit::metadata::FrameContextT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a65416e676c6573() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a65416e676c6573_proxy(
    const open_perception_kit::metadata::GazeAnglesT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74_proxy(
    const open_perception_kit::metadata::GazeMeasurementT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473_proxy(
    const open_perception_kit::metadata::GazeMeasurementsT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b_proxy(
    const open_perception_kit::metadata::InstanceSegmentationMaskT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73_proxy(
    const open_perception_kit::metadata::InstanceSegmentationMasksT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e456d62656464696e675370616365() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e456d62656464696e675370616365_proxy(
    const open_perception_kit::metadata::EmbeddingSpaceT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67_proxy(
    const open_perception_kit::metadata::ObjectEmbeddingT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773_proxy(
    const open_perception_kit::metadata::ObjectEmbeddingsT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_proxy(
    const open_perception_kit::metadata::ObjectTrackT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73_proxy(
    const open_perception_kit::metadata::ObjectTracksT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67_proxy(
    const open_perception_kit::metadata::ScopeTimingT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d6552617465() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d6552617465_proxy(
    const open_perception_kit::metadata::FrameRateT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473_proxy(
    const open_perception_kit::metadata::PerformanceMeasurementsT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365526573756c74() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365526573756c74_proxy(
    const open_perception_kit::metadata::PersonPresenceResultT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365_proxy(
    const open_perception_kit::metadata::PersonPresenceT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573_proxy(
    const open_perception_kit::metadata::PersonPresencesT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b_proxy(
    const open_perception_kit::metadata::SegmentationMaskT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73_proxy(
    const open_perception_kit::metadata::SegmentationMasksT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b_proxy(
    const open_perception_kit::metadata::SemanticSegmentationMaskT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73_proxy(
    const open_perception_kit::metadata::SemanticSegmentationMasksT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365_proxy(
    const open_perception_kit::metadata::TrackTraceT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573_proxy(
    const open_perception_kit::metadata::TrackTracesT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_proxy(
    const open_perception_kit::metadata::TurnDetectionT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73_proxy(
    const open_perception_kit::metadata::TurnDetectionsT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_proxy(
    const open_perception_kit::metadata::VoiceActivityDetectionT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73() {
    static PyTypeObject type = make_py_type_object();
    return type;
}
PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73_proxy(
    const open_perception_kit::metadata::VoiceActivityDetectionsT *value,
    std::shared_ptr<const native_proxy_anchor> anchor);

PySequenceMethods &
seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e732e646574656374696f6e732e766563746f72() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e732e646574656374696f6e732e766563746f72() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &
seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374732e636f6e74616374732e766563746f72() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374732e636f6e74616374732e766563746f72() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &
seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e2e63616e646964617465732e766563746f72() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e2e63616e646964617465732e766563746f72() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &
seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e732e636c617373696669636174696f6e732e766563746f72() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e732e636c617373696669636174696f6e732e766563746f72() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &
seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d6170446174612e706978656c732e766563746f72() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d6170446174612e706978656c732e766563746f72() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &
seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74732e6d6561737572656d656e74732e766563746f72() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74732e6d6561737572656d656e74732e766563746f72() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &
seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &
seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e672e76616c7565732e766563746f72() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e672e76616c7565732e766563746f72() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &
seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67732e656d62656464696e67732e766563746f72() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67732e656d62656464696e67732e766563746f72() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &
seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b732e747261636b732e766563746f72() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b732e747261636b732e766563746f72() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &
seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e74732e74696d696e67732e766563746f72() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e74732e74696d696e67732e766563746f72() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &
seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365732e70726573656e6365732e766563746f72() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365732e70726573656e6365732e766563746f72() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &
seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &
seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &
seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b54726163652e706f696e74732e766563746f72() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b54726163652e706f696e74732e766563746f72() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &
seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365732e7472616365732e766563746f72() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365732e7472616365732e766563746f72() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &
seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e732e646574656374696f6e732e766563746f72() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e732e646574656374696f6e732e766563746f72() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

PySequenceMethods &
seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e732e646574656374696f6e732e766563746f72() {
    static PySequenceMethods methods = {};
    return methods;
}
PyTypeObject &
pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e732e646574656374696f6e732e766563746f72() {
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
    case container::producer_identity_status::name_mismatch:
        value = "name_mismatch";
        break;
    case container::producer_identity_status::version_mismatch:
        value = "version_mismatch";
        break;
    case container::producer_identity_status::compatible_match:
        value = "compatible_match";
        break;
    }

    PyObject *sdk_module = PyImport_ImportModule("open_perception_kit.sdk");
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
        PyErr_SetString(PyExc_TypeError, "expected open_perception_kit_bridge.Envelope");
        return nullptr;
    }
    if (!live->valid || live->envelope == nullptr) {
        PyErr_SetString(PyExc_RuntimeError,
                        "open_perception_kit live envelope wrapper is no longer valid");
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

    PyObject *sdk_module = PyImport_ImportModule("open_perception_kit");
    if (sdk_module == nullptr) {
        PyErr_SetString(
            PyExc_TypeError,
            "open_perception_kit ExternalKey requires the generated Python SDK to be importable");
        return std::nullopt;
    }

    PyObject *external_key_type = PyObject_GetAttrString(sdk_module, "ExternalKey");
    Py_DECREF(sdk_module);
    if (external_key_type == nullptr) {
        PyErr_SetString(PyExc_TypeError,
                        "open_perception_kit Python SDK does not expose ExternalKey");
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
                        "open_perception_kit external envelope operations require ExternalKey; "
                        "call external_key(...) first");
        return std::nullopt;
    }

    if (PyLong_Check(object)) {
        PyErr_SetString(PyExc_TypeError,
                        "open_perception_kit external key must be an ExternalKey, not int");
        return std::nullopt;
    }

    auto is_key = is_external_key_python(object);
    if (!is_key.has_value()) {
        return std::nullopt;
    }
    if (!is_key.value()) {
        PyErr_SetString(PyExc_TypeError, "open_perception_kit external key must be an ExternalKey");
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
                        "open_perception_kit ExternalKey value is outside the external key domain");
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
    kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73,
    kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473,
    kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73,
    kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874,
    kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473,
    kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73,
    kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773,
    kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73,
    kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473,
    kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573,
    kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73,
    kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73,
    kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573,
    kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73,
    kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73,
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
            "open_perception_kit known payload argument must be a generated Python payload type");
        return std::nullopt;
    }

    if (py_type_matches(type_object,
                        "open_perception_kit.fb.open_perception_kit.metadata.BoxDetections",
                        "BoxDetectionsT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73;
    }
    if (py_type_matches(type_object,
                        "open_perception_kit.fb.open_perception_kit.metadata.CameraContacts",
                        "CameraContactsT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473;
    }
    if (py_type_matches(type_object,
                        "open_perception_kit.fb.open_perception_kit.metadata.Classifications",
                        "ClassificationsT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73;
    }
    if (py_type_matches(type_object,
                        "open_perception_kit.fb.open_perception_kit.metadata.FrameContext",
                        "FrameContextT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874;
    }
    if (py_type_matches(type_object,
                        "open_perception_kit.fb.open_perception_kit.metadata.GazeMeasurements",
                        "GazeMeasurementsT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473;
    }
    if (py_type_matches(
            type_object,
            "open_perception_kit.fb.open_perception_kit.metadata.InstanceSegmentationMasks",
            "InstanceSegmentationMasksT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73;
    }
    if (py_type_matches(type_object,
                        "open_perception_kit.fb.open_perception_kit.metadata.ObjectEmbeddings",
                        "ObjectEmbeddingsT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773;
    }
    if (py_type_matches(type_object,
                        "open_perception_kit.fb.open_perception_kit.metadata.ObjectTracks",
                        "ObjectTracksT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73;
    }
    if (py_type_matches(
            type_object,
            "open_perception_kit.fb.open_perception_kit.metadata.PerformanceMeasurements",
            "PerformanceMeasurementsT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473;
    }
    if (py_type_matches(type_object,
                        "open_perception_kit.fb.open_perception_kit.metadata.PersonPresences",
                        "PersonPresencesT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573;
    }
    if (py_type_matches(type_object,
                        "open_perception_kit.fb.open_perception_kit.metadata.SegmentationMasks",
                        "SegmentationMasksT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73;
    }
    if (py_type_matches(
            type_object,
            "open_perception_kit.fb.open_perception_kit.metadata.SemanticSegmentationMasks",
            "SemanticSegmentationMasksT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73;
    }
    if (py_type_matches(type_object,
                        "open_perception_kit.fb.open_perception_kit.metadata.TrackTraces",
                        "TrackTracesT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573;
    }
    if (py_type_matches(type_object,
                        "open_perception_kit.fb.open_perception_kit.metadata.TurnDetections",
                        "TurnDetectionsT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73;
    }
    if (py_type_matches(
            type_object,
            "open_perception_kit.fb.open_perception_kit.metadata.VoiceActivityDetections",
            "VoiceActivityDetectionsT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73;
    }

    const auto message =
        "open_perception_kit unknown known payload type: " + py_type_description(type_object);
    PyErr_SetString(PyExc_TypeError, message.c_str());
    return std::nullopt;
}

std::optional<known_payload_kind> known_payload_kind_from_value(PyObject *value) {
    if (py_object_type_matches(value,
                               "open_perception_kit.fb.open_perception_kit.metadata.BoxDetections",
                               "BoxDetectionsT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73;
    }
    if (py_object_type_matches(value,
                               "open_perception_kit.fb.open_perception_kit.metadata.CameraContacts",
                               "CameraContactsT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473;
    }
    if (py_object_type_matches(
            value,
            "open_perception_kit.fb.open_perception_kit.metadata.Classifications",
            "ClassificationsT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73;
    }
    if (py_object_type_matches(value,
                               "open_perception_kit.fb.open_perception_kit.metadata.FrameContext",
                               "FrameContextT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874;
    }
    if (py_object_type_matches(
            value,
            "open_perception_kit.fb.open_perception_kit.metadata.GazeMeasurements",
            "GazeMeasurementsT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473;
    }
    if (py_object_type_matches(
            value,
            "open_perception_kit.fb.open_perception_kit.metadata.InstanceSegmentationMasks",
            "InstanceSegmentationMasksT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73;
    }
    if (py_object_type_matches(
            value,
            "open_perception_kit.fb.open_perception_kit.metadata.ObjectEmbeddings",
            "ObjectEmbeddingsT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773;
    }
    if (py_object_type_matches(value,
                               "open_perception_kit.fb.open_perception_kit.metadata.ObjectTracks",
                               "ObjectTracksT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73;
    }
    if (py_object_type_matches(
            value,
            "open_perception_kit.fb.open_perception_kit.metadata.PerformanceMeasurements",
            "PerformanceMeasurementsT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473;
    }
    if (py_object_type_matches(
            value,
            "open_perception_kit.fb.open_perception_kit.metadata.PersonPresences",
            "PersonPresencesT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573;
    }
    if (py_object_type_matches(
            value,
            "open_perception_kit.fb.open_perception_kit.metadata.SegmentationMasks",
            "SegmentationMasksT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73;
    }
    if (py_object_type_matches(
            value,
            "open_perception_kit.fb.open_perception_kit.metadata.SemanticSegmentationMasks",
            "SemanticSegmentationMasksT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73;
    }
    if (py_object_type_matches(value,
                               "open_perception_kit.fb.open_perception_kit.metadata.TrackTraces",
                               "TrackTracesT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573;
    }
    if (py_object_type_matches(value,
                               "open_perception_kit.fb.open_perception_kit.metadata.TurnDetections",
                               "TurnDetectionsT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73;
    }
    if (py_object_type_matches(
            value,
            "open_perception_kit.fb.open_perception_kit.metadata.VoiceActivityDetections",
            "VoiceActivityDetectionsT")) {
        return known_payload_kind::
            kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73;
    }

    const auto message =
        "open_perception_kit cannot add unsupported payload object: " + py_type_description(value);
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
        PyErr_SetString(PyExc_ValueError,
                        "open_perception_kit payload object did not pack into the expected "
                        "FlatBuffers root type");
        return std::nullopt;
    }

    const auto *root = flatbuffers::GetRoot<typename traits::table_type>(blob.data());
    if (root == nullptr) {
        PyErr_SetString(PyExc_ValueError,
                        "open_perception_kit payload object packed an invalid FlatBuffers root");
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
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73:
        return PyLong_FromSize_t(
            live->envelope->count<open_perception_kit::metadata::BoxDetectionsT>());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473:
        return PyLong_FromSize_t(
            live->envelope->count<open_perception_kit::metadata::CameraContactsT>());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73:
        return PyLong_FromSize_t(
            live->envelope->count<open_perception_kit::metadata::ClassificationsT>());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874:
        return PyLong_FromSize_t(
            live->envelope->count<open_perception_kit::metadata::FrameContextT>());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473:
        return PyLong_FromSize_t(
            live->envelope->count<open_perception_kit::metadata::GazeMeasurementsT>());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73:
        return PyLong_FromSize_t(
            live->envelope->count<open_perception_kit::metadata::InstanceSegmentationMasksT>());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773:
        return PyLong_FromSize_t(
            live->envelope->count<open_perception_kit::metadata::ObjectEmbeddingsT>());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73:
        return PyLong_FromSize_t(
            live->envelope->count<open_perception_kit::metadata::ObjectTracksT>());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473:
        return PyLong_FromSize_t(
            live->envelope->count<open_perception_kit::metadata::PerformanceMeasurementsT>());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573:
        return PyLong_FromSize_t(
            live->envelope->count<open_perception_kit::metadata::PersonPresencesT>());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73:
        return PyLong_FromSize_t(
            live->envelope->count<open_perception_kit::metadata::SegmentationMasksT>());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73:
        return PyLong_FromSize_t(
            live->envelope->count<open_perception_kit::metadata::SemanticSegmentationMasksT>());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573:
        return PyLong_FromSize_t(
            live->envelope->count<open_perception_kit::metadata::TrackTracesT>());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73:
        return PyLong_FromSize_t(
            live->envelope->count<open_perception_kit::metadata::TurnDetectionsT>());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73:
        return PyLong_FromSize_t(
            live->envelope->count<open_perception_kit::metadata::VoiceActivityDetectionsT>());
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
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73:
        if (live->envelope->contains<open_perception_kit::metadata::BoxDetectionsT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473:
        if (live->envelope->contains<open_perception_kit::metadata::CameraContactsT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73:
        if (live->envelope->contains<open_perception_kit::metadata::ClassificationsT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874:
        if (live->envelope->contains<open_perception_kit::metadata::FrameContextT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473:
        if (live->envelope->contains<open_perception_kit::metadata::GazeMeasurementsT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73:
        if (live->envelope->contains<open_perception_kit::metadata::InstanceSegmentationMasksT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773:
        if (live->envelope->contains<open_perception_kit::metadata::ObjectEmbeddingsT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73:
        if (live->envelope->contains<open_perception_kit::metadata::ObjectTracksT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473:
        if (live->envelope->contains<open_perception_kit::metadata::PerformanceMeasurementsT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573:
        if (live->envelope->contains<open_perception_kit::metadata::PersonPresencesT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73:
        if (live->envelope->contains<open_perception_kit::metadata::SegmentationMasksT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73:
        if (live->envelope->contains<open_perception_kit::metadata::SemanticSegmentationMasksT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573:
        if (live->envelope->contains<open_perception_kit::metadata::TrackTracesT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73:
        if (live->envelope->contains<open_perception_kit::metadata::TurnDetectionsT>()) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73:
        if (live->envelope->contains<open_perception_kit::metadata::VoiceActivityDetectionsT>()) {
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
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73:
        return get_known_payload<open_perception_kit::metadata::BoxDetectionsT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73(),
            index);
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473:
        return get_known_payload<open_perception_kit::metadata::CameraContactsT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473(),
            index);
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73:
        return get_known_payload<open_perception_kit::metadata::ClassificationsT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73(),
            index);
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874:
        return get_known_payload<open_perception_kit::metadata::FrameContextT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874(),
            index);
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473:
        return get_known_payload<open_perception_kit::metadata::GazeMeasurementsT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473(),
            index);
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73:
        return get_known_payload<open_perception_kit::metadata::InstanceSegmentationMasksT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73(),
            index);
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773:
        return get_known_payload<open_perception_kit::metadata::ObjectEmbeddingsT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773(),
            index);
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73:
        return get_known_payload<open_perception_kit::metadata::ObjectTracksT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73(),
            index);
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473:
        return get_known_payload<open_perception_kit::metadata::PerformanceMeasurementsT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473(),
            index);
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573:
        return get_known_payload<open_perception_kit::metadata::PersonPresencesT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573(),
            index);
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73:
        return get_known_payload<open_perception_kit::metadata::SegmentationMasksT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73(),
            index);
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73:
        return get_known_payload<open_perception_kit::metadata::SemanticSegmentationMasksT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73(),
            index);
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573:
        return get_known_payload<open_perception_kit::metadata::TrackTracesT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573(),
            index);
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73:
        return get_known_payload<open_perception_kit::metadata::TurnDetectionsT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73(),
            index);
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73:
        return get_known_payload<open_perception_kit::metadata::VoiceActivityDetectionsT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73(),
            index);
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
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73:
        return list_known_payloads<open_perception_kit::metadata::BoxDetectionsT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473:
        return list_known_payloads<open_perception_kit::metadata::CameraContactsT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73:
        return list_known_payloads<open_perception_kit::metadata::ClassificationsT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874:
        return list_known_payloads<open_perception_kit::metadata::FrameContextT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473:
        return list_known_payloads<open_perception_kit::metadata::GazeMeasurementsT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73:
        return list_known_payloads<open_perception_kit::metadata::InstanceSegmentationMasksT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773:
        return list_known_payloads<open_perception_kit::metadata::ObjectEmbeddingsT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73:
        return list_known_payloads<open_perception_kit::metadata::ObjectTracksT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473:
        return list_known_payloads<open_perception_kit::metadata::PerformanceMeasurementsT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573:
        return list_known_payloads<open_perception_kit::metadata::PersonPresencesT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73:
        return list_known_payloads<open_perception_kit::metadata::SegmentationMasksT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73:
        return list_known_payloads<open_perception_kit::metadata::SemanticSegmentationMasksT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573:
        return list_known_payloads<open_perception_kit::metadata::TrackTracesT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73:
        return list_known_payloads<open_perception_kit::metadata::TurnDetectionsT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73());
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73:
        return list_known_payloads<open_perception_kit::metadata::VoiceActivityDetectionsT>(
            *live->envelope,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73());
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

        try {
            live->envelope->add(
                *key,
                std::span<const std::uint8_t>(static_cast<const std::uint8_t *>(view.buf),
                                              static_cast<std::size_t>(view.len)));
        } catch (const std::exception &exc) {
            PyBuffer_Release(&view);
            PyErr_SetString(PyExc_RuntimeError, exc.what());
            return nullptr;
        }
        PyBuffer_Release(&view);
        Py_RETURN_NONE;
    }

    auto is_key = is_external_key_python(value);
    if (!is_key.has_value()) {
        return nullptr;
    }
    if (is_key.value()) {
        PyErr_SetString(PyExc_TypeError,
                        "open_perception_kit external add requires a bytes-like blob");
        return nullptr;
    }

    auto kind = known_payload_kind_from_value(value);
    if (!kind) {
        return nullptr;
    }

    switch (*kind) {
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73:
        return add_known_payload<open_perception_kit::metadata::BoxDetectionsT>(
            *live->envelope, value, "BDET");
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473:
        return add_known_payload<open_perception_kit::metadata::CameraContactsT>(
            *live->envelope, value, "CCON");
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73:
        return add_known_payload<open_perception_kit::metadata::ClassificationsT>(
            *live->envelope, value, "CLSF");
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874:
        return add_known_payload<open_perception_kit::metadata::FrameContextT>(
            *live->envelope, value, "FCTX");
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473:
        return add_known_payload<open_perception_kit::metadata::GazeMeasurementsT>(
            *live->envelope, value, "GZMS");
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73:
        return add_known_payload<open_perception_kit::metadata::InstanceSegmentationMasksT>(
            *live->envelope, value, "ISGM");
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773:
        return add_known_payload<open_perception_kit::metadata::ObjectEmbeddingsT>(
            *live->envelope, value, "EMBE");
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73:
        return add_known_payload<open_perception_kit::metadata::ObjectTracksT>(
            *live->envelope, value, "TRKS");
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473:
        return add_known_payload<open_perception_kit::metadata::PerformanceMeasurementsT>(
            *live->envelope, value, "PMET");
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573:
        return add_known_payload<open_perception_kit::metadata::PersonPresencesT>(
            *live->envelope, value, "PPRS");
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73:
        return add_known_payload<open_perception_kit::metadata::SegmentationMasksT>(
            *live->envelope, value, "SGMS");
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73:
        return add_known_payload<open_perception_kit::metadata::SemanticSegmentationMasksT>(
            *live->envelope, value, "SSGM");
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573:
        return add_known_payload<open_perception_kit::metadata::TrackTracesT>(
            *live->envelope, value, "TRCE");
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73:
        return add_known_payload<open_perception_kit::metadata::TurnDetectionsT>(
            *live->envelope, value, "TURN");
    case known_payload_kind::
        kind_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73:
        return add_known_payload<open_perception_kit::metadata::VoiceActivityDetectionsT>(
            *live->envelope, value, "VACT");
    }

    Py_UNREACHABLE();
}

Py_ssize_t
len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e732e646574656374696f6e732e766563746f72(
    PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::BoxDetectionT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *
item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e732e646574656374696f6e732e766563746f72(
    PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::BoxDetectionT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "open_perception_kit vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<open_perception_kit::metadata::BoxDetectionT>>>(
            self));
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e732e646574656374696f6e732e766563746f72_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e732e646574656374696f6e732e766563746f72();
    auto &sequence =
        seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e732e646574656374696f6e732e766563746f72();
    if (type.tp_name == nullptr) {
        sequence.sq_length =
            len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e732e646574656374696f6e732e766563746f72;
        sequence.sq_item =
            item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e732e646574656374696f6e732e766563746f72;
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f784465746563"
                       "74696f6e732e646574656374696f6e732e766563746f72";
        type.tp_basicsize =
            sizeof(native_proxy_object<
                   std::vector<std::unique_ptr<open_perception_kit::metadata::BoxDetectionT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<open_perception_kit::metadata::BoxDetectionT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t
len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374732e636f6e74616374732e766563746f72(
    PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::CameraContactT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *
item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374732e636f6e74616374732e766563746f72(
    PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::CameraContactT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "open_perception_kit vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<open_perception_kit::metadata::CameraContactT>>>(
            self));
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374732e636f6e74616374732e766563746f72_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374732e636f6e74616374732e766563746f72();
    auto &sequence =
        seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374732e636f6e74616374732e766563746f72();
    if (type.tp_name == nullptr) {
        sequence.sq_length =
            len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374732e636f6e74616374732e766563746f72;
        sequence.sq_item =
            item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374732e636f6e74616374732e766563746f72;
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f"
                       "6e74616374732e636f6e74616374732e766563746f72";
        type.tp_basicsize =
            sizeof(native_proxy_object<
                   std::vector<std::unique_ptr<open_perception_kit::metadata::CameraContactT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<open_perception_kit::metadata::CameraContactT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t
len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e2e63616e646964617465732e766563746f72(
    PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::ClassificationCandidateT>>> *>(
        self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *
item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e2e63616e646964617465732e766563746f72(
    PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::ClassificationCandidateT>>> *>(
        self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "open_perception_kit vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e43616e646964617465_proxy(
        item.get(),
        proxy_anchor<
            std::vector<std::unique_ptr<open_perception_kit::metadata::ClassificationCandidateT>>>(
            self));
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e2e63616e646964617465732e766563746f72_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e2e63616e646964617465732e766563746f72();
    auto &sequence =
        seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e2e63616e646964617465732e766563746f72();
    if (type.tp_name == nullptr) {
        sequence.sq_length =
            len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e2e63616e646964617465732e766563746f72;
        sequence.sq_item =
            item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e2e63616e646964617465732e766563746f72;
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669"
                       "636174696f6e2e63616e646964617465732e766563746f72";
        type.tp_basicsize =
            sizeof(native_proxy_object<std::vector<
                       std::unique_ptr<open_perception_kit::metadata::ClassificationCandidateT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<open_perception_kit::metadata::ClassificationCandidateT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t
len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e732e636c617373696669636174696f6e732e766563746f72(
    PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::ClassificationT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *
item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e732e636c617373696669636174696f6e732e766563746f72(
    PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::ClassificationT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "open_perception_kit vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<open_perception_kit::metadata::ClassificationT>>>(
            self));
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e732e636c617373696669636174696f6e732e766563746f72_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e732e636c617373696669636174696f6e732e766563746f72();
    auto &sequence =
        seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e732e636c617373696669636174696f6e732e766563746f72();
    if (type.tp_name == nullptr) {
        sequence.sq_length =
            len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e732e636c617373696669636174696f6e732e766563746f72;
        sequence.sq_item =
            item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e732e636c617373696669636174696f6e732e766563746f72;
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669"
                       "636174696f6e732e636c617373696669636174696f6e732e766563746f72";
        type.tp_basicsize =
            sizeof(native_proxy_object<
                   std::vector<std::unique_ptr<open_perception_kit::metadata::ClassificationT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<open_perception_kit::metadata::ClassificationT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t
len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d6170446174612e706978656c732e766563746f72(
    PyObject *self) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<std::vector<std::uint8_t>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *
item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d6170446174612e706978656c732e766563746f72(
    PyObject *self, Py_ssize_t index) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<std::vector<std::uint8_t>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "open_perception_kit vector proxy index out of range");
        return nullptr;
    }
    return py_long_from_unsigned((*vector)[static_cast<std::size_t>(index)]);
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d6170446174612e706978656c732e766563746f72_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d6170446174612e706978656c732e766563746f72();
    auto &sequence =
        seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d6170446174612e706978656c732e766563746f72();
    if (type.tp_name == nullptr) {
        sequence.sq_length =
            len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d6170446174612e706978656c732e766563746f72;
        sequence.sq_item =
            item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d6170446174612e706978656c732e766563746f72;
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d61704461"
                       "74612e706978656c732e766563746f72";
        type.tp_basicsize = sizeof(native_proxy_object<std::vector<std::uint8_t>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<std::vector<std::uint8_t>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t
len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74732e6d6561737572656d656e74732e766563746f72(
    PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::GazeMeasurementT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *
item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74732e6d6561737572656d656e74732e766563746f72(
    PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::GazeMeasurementT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "open_perception_kit vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<open_perception_kit::metadata::GazeMeasurementT>>>(
            self));
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74732e6d6561737572656d656e74732e766563746f72_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74732e6d6561737572656d656e74732e766563746f72();
    auto &sequence =
        seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74732e6d6561737572656d656e74732e766563746f72();
    if (type.tp_name == nullptr) {
        sequence.sq_length =
            len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74732e6d6561737572656d656e74732e766563746f72;
        sequence.sq_item =
            item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74732e6d6561737572656d656e74732e766563746f72;
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d656173"
                       "7572656d656e74732e6d6561737572656d656e74732e766563746f72";
        type.tp_basicsize =
            sizeof(native_proxy_object<
                   std::vector<std::unique_ptr<open_perception_kit::metadata::GazeMeasurementT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<open_perception_kit::metadata::GazeMeasurementT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t
len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72(
    PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::InstanceSegmentationMaskT>>> *>(
        self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *
item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72(
    PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::InstanceSegmentationMaskT>>> *>(
        self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "open_perception_kit vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b_proxy(
        item.get(),
        proxy_anchor<
            std::vector<std::unique_ptr<open_perception_kit::metadata::InstanceSegmentationMaskT>>>(
            self));
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72();
    auto &sequence =
        seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72();
    if (type.tp_name == nullptr) {
        sequence.sq_length =
            len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72;
        sequence.sq_item =
            item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72;
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e6365"
                       "5365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72";
        type.tp_basicsize =
            sizeof(native_proxy_object<std::vector<
                       std::unique_ptr<open_perception_kit::metadata::InstanceSegmentationMaskT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<open_perception_kit::metadata::InstanceSegmentationMaskT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t
len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e672e76616c7565732e766563746f72(
    PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<std::vector<float>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *
item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e672e76616c7565732e766563746f72(
    PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<std::vector<float>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "open_perception_kit vector proxy index out of range");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>((*vector)[static_cast<std::size_t>(index)]));
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e672e76616c7565732e766563746f72_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e672e76616c7565732e766563746f72();
    auto &sequence =
        seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e672e76616c7565732e766563746f72();
    if (type.tp_name == nullptr) {
        sequence.sq_length =
            len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e672e76616c7565732e766563746f72;
        sequence.sq_item =
            item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e672e76616c7565732e766563746f72;
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d"
                       "62656464696e672e76616c7565732e766563746f72";
        type.tp_basicsize = sizeof(native_proxy_object<std::vector<float>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<std::vector<float>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t
len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67732e656d62656464696e67732e766563746f72(
    PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::ObjectEmbeddingT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *
item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67732e656d62656464696e67732e766563746f72(
    PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::ObjectEmbeddingT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "open_perception_kit vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<open_perception_kit::metadata::ObjectEmbeddingT>>>(
            self));
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67732e656d62656464696e67732e766563746f72_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67732e656d62656464696e67732e766563746f72();
    auto &sequence =
        seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67732e656d62656464696e67732e766563746f72();
    if (type.tp_name == nullptr) {
        sequence.sq_length =
            len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67732e656d62656464696e67732e766563746f72;
        sequence.sq_item =
            item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67732e656d62656464696e67732e766563746f72;
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d"
                       "62656464696e67732e656d62656464696e67732e766563746f72";
        type.tp_basicsize =
            sizeof(native_proxy_object<
                   std::vector<std::unique_ptr<open_perception_kit::metadata::ObjectEmbeddingT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<open_perception_kit::metadata::ObjectEmbeddingT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t
len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b732e747261636b732e766563746f72(
    PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::ObjectTrackT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *
item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b732e747261636b732e766563746f72(
    PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::ObjectTrackT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "open_perception_kit vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<open_perception_kit::metadata::ObjectTrackT>>>(
            self));
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b732e747261636b732e766563746f72_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b732e747261636b732e766563746f72();
    auto &sequence =
        seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b732e747261636b732e766563746f72();
    if (type.tp_name == nullptr) {
        sequence.sq_length =
            len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b732e747261636b732e766563746f72;
        sequence.sq_item =
            item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b732e747261636b732e766563746f72;
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563745472"
                       "61636b732e747261636b732e766563746f72";
        type.tp_basicsize =
            sizeof(native_proxy_object<
                   std::vector<std::unique_ptr<open_perception_kit::metadata::ObjectTrackT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<open_perception_kit::metadata::ObjectTrackT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t
len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e74732e74696d696e67732e766563746f72(
    PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::ScopeTimingT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *
item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e74732e74696d696e67732e766563746f72(
    PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::ScopeTimingT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "open_perception_kit vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<open_perception_kit::metadata::ScopeTimingT>>>(
            self));
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e74732e74696d696e67732e766563746f72_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e74732e74696d696e67732e766563746f72();
    auto &sequence =
        seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e74732e74696d696e67732e766563746f72();
    if (type.tp_name == nullptr) {
        sequence.sq_length =
            len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e74732e74696d696e67732e766563746f72;
        sequence.sq_item =
            item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e74732e74696d696e67732e766563746f72;
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d61"
                       "6e63654d6561737572656d656e74732e74696d696e67732e766563746f72";
        type.tp_basicsize =
            sizeof(native_proxy_object<
                   std::vector<std::unique_ptr<open_perception_kit::metadata::ScopeTimingT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<open_perception_kit::metadata::ScopeTimingT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t
len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365732e70726573656e6365732e766563746f72(
    PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::PersonPresenceT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *
item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365732e70726573656e6365732e766563746f72(
    PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::PersonPresenceT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "open_perception_kit vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<open_perception_kit::metadata::PersonPresenceT>>>(
            self));
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365732e70726573656e6365732e766563746f72_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365732e70726573656e6365732e766563746f72();
    auto &sequence =
        seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365732e70726573656e6365732e766563746f72();
    if (type.tp_name == nullptr) {
        sequence.sq_length =
            len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365732e70726573656e6365732e766563746f72;
        sequence.sq_item =
            item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365732e70726573656e6365732e766563746f72;
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e5072"
                       "6573656e6365732e70726573656e6365732e766563746f72";
        type.tp_basicsize =
            sizeof(native_proxy_object<
                   std::vector<std::unique_ptr<open_perception_kit::metadata::PersonPresenceT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<open_perception_kit::metadata::PersonPresenceT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t
len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72(
    PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::SegmentationMaskT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *
item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72(
    PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::SegmentationMaskT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "open_perception_kit vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b_proxy(
        item.get(),
        proxy_anchor<
            std::vector<std::unique_ptr<open_perception_kit::metadata::SegmentationMaskT>>>(self));
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72();
    auto &sequence =
        seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72();
    if (type.tp_name == nullptr) {
        sequence.sq_length =
            len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72;
        sequence.sq_item =
            item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72;
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e7461"
                       "74696f6e4d61736b732e6d61736b732e766563746f72";
        type.tp_basicsize =
            sizeof(native_proxy_object<
                   std::vector<std::unique_ptr<open_perception_kit::metadata::SegmentationMaskT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<open_perception_kit::metadata::SegmentationMaskT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t
len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72(
    PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::SemanticSegmentationMaskT>>> *>(
        self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *
item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72(
    PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::SemanticSegmentationMaskT>>> *>(
        self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "open_perception_kit vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b_proxy(
        item.get(),
        proxy_anchor<
            std::vector<std::unique_ptr<open_perception_kit::metadata::SemanticSegmentationMaskT>>>(
            self));
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72();
    auto &sequence =
        seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72();
    if (type.tp_name == nullptr) {
        sequence.sq_length =
            len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72;
        sequence.sq_item =
            item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72;
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e746963"
                       "5365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72";
        type.tp_basicsize =
            sizeof(native_proxy_object<std::vector<
                       std::unique_ptr<open_perception_kit::metadata::SemanticSegmentationMaskT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<open_perception_kit::metadata::SemanticSegmentationMaskT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t
len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b54726163652e706f696e74732e766563746f72(
    PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::Point2fT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *
item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b54726163652e706f696e74732e766563746f72(
    PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::Point2fT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "open_perception_kit vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506f696e743266_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<open_perception_kit::metadata::Point2fT>>>(self));
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b54726163652e706f696e74732e766563746f72_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b54726163652e706f696e74732e766563746f72();
    auto &sequence =
        seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b54726163652e706f696e74732e766563746f72();
    if (type.tp_name == nullptr) {
        sequence.sq_length =
            len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b54726163652e706f696e74732e766563746f72;
        sequence.sq_item =
            item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b54726163652e706f696e74732e766563746f72;
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261"
                       "63652e706f696e74732e766563746f72";
        type.tp_basicsize =
            sizeof(native_proxy_object<
                   std::vector<std::unique_ptr<open_perception_kit::metadata::Point2fT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<open_perception_kit::metadata::Point2fT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t
len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365732e7472616365732e766563746f72(
    PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::TrackTraceT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *
item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365732e7472616365732e766563746f72(
    PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::TrackTraceT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "open_perception_kit vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<open_perception_kit::metadata::TrackTraceT>>>(
            self));
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365732e7472616365732e766563746f72_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365732e7472616365732e766563746f72();
    auto &sequence =
        seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365732e7472616365732e766563746f72();
    if (type.tp_name == nullptr) {
        sequence.sq_length =
            len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365732e7472616365732e766563746f72;
        sequence.sq_item =
            item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365732e7472616365732e766563746f72;
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261"
                       "6365732e7472616365732e766563746f72";
        type.tp_basicsize =
            sizeof(native_proxy_object<
                   std::vector<std::unique_ptr<open_perception_kit::metadata::TrackTraceT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<open_perception_kit::metadata::TrackTraceT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t
len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e732e646574656374696f6e732e766563746f72(
    PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::TurnDetectionT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *
item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e732e646574656374696f6e732e766563746f72(
    PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::TurnDetectionT>>> *>(self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "open_perception_kit vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_proxy(
        item.get(),
        proxy_anchor<std::vector<std::unique_ptr<open_perception_kit::metadata::TurnDetectionT>>>(
            self));
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e732e646574656374696f6e732e766563746f72_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e732e646574656374696f6e732e766563746f72();
    auto &sequence =
        seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e732e646574656374696f6e732e766563746f72();
    if (type.tp_name == nullptr) {
        sequence.sq_length =
            len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e732e646574656374696f6e732e766563746f72;
        sequence.sq_item =
            item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e732e646574656374696f6e732e766563746f72;
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e44657465"
                       "6374696f6e732e646574656374696f6e732e766563746f72";
        type.tp_basicsize =
            sizeof(native_proxy_object<
                   std::vector<std::unique_ptr<open_perception_kit::metadata::TurnDetectionT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<open_perception_kit::metadata::TurnDetectionT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

Py_ssize_t
len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e732e646574656374696f6e732e766563746f72(
    PyObject *self) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::VoiceActivityDetectionT>>> *>(
        self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        return 0;
    }
    return static_cast<Py_ssize_t>(vector->size());
}

PyObject *
item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e732e646574656374696f6e732e766563746f72(
    PyObject *self, Py_ssize_t index) {
    const auto *proxy = reinterpret_cast<const native_proxy_object<
        std::vector<std::unique_ptr<open_perception_kit::metadata::VoiceActivityDetectionT>>> *>(
        self);
    const auto *vector = proxy->value;
    if (vector == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit vector proxy is invalid");
        return nullptr;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {
        PyErr_SetString(PyExc_IndexError, "open_perception_kit vector proxy index out of range");
        return nullptr;
    }
    const auto &item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_proxy(
        item.get(),
        proxy_anchor<
            std::vector<std::unique_ptr<open_perception_kit::metadata::VoiceActivityDetectionT>>>(
            self));
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e732e646574656374696f6e732e766563746f72_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e732e646574656374696f6e732e766563746f72();
    auto &sequence =
        seq_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e732e646574656374696f6e732e766563746f72();
    if (type.tp_name == nullptr) {
        sequence.sq_length =
            len_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e732e646574656374696f6e732e766563746f72;
        sequence.sq_item =
            item_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e732e646574656374696f6e732e766563746f72;
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f696365416374"
                       "6976697479446574656374696f6e732e646574656374696f6e732e766563746f72";
        type.tp_basicsize =
            sizeof(native_proxy_object<std::vector<
                       std::unique_ptr<open_perception_kit::metadata::VoiceActivityDetectionT>>>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<
            std::vector<std::unique_ptr<open_perception_kit::metadata::VoiceActivityDetectionT>>>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live vector proxy";
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e_n6f626a656374(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::BoxDetectionT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::BoxDetectionT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e_n626f78(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::BoxDetectionT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->box.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::BoxDetectionT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e_n636f6e666964656e6365(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::BoxDetectionT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->confidence));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e_n636c6173735f6964(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::BoxDetectionT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->class_id) {
        Py_RETURN_NONE;
    }
    return py_long_from_unsigned(*(value->class_id));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e_n74657874(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::BoxDetectionT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->text);
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e_n6f626a656374),
         nullptr,
         "read-only object",
         nullptr},
        {"box",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e_n626f78),
         nullptr,
         "read-only box",
         nullptr},
        {"confidence",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e_n636f6e666964656e6365),
         nullptr,
         "read-only confidence",
         nullptr},
        {"classId",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e_n636c6173735f6964),
         nullptr,
         "read-only class_id",
         nullptr},
        {"text",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e_n74657874),
         nullptr,
         "read-only text",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e();
    if (type.tp_name == nullptr) {
        type.tp_name =
            "open_perception_kit_bridge."
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::BoxDetectionT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::BoxDetectionT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e_proxy(
    const open_perception_kit::metadata::BoxDetectionT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::BoxDetectionT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73_n736368656d615f6d616a6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::BoxDetectionsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73_n736368656d615f6d696e6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::BoxDetectionsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73_n6c61796572(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::BoxDetectionsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::BoxDetectionsT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73_n646574656374696f6e73(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::BoxDetectionsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<open_perception_kit::metadata::BoxDetectionT>>>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e732e646574656374696f6e732e766563746f72(),
        std::addressof(value->detections),
        proxy_anchor<open_perception_kit::metadata::BoxDetectionsT>(self));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73_n736368656d615f6d616a6f72),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73_n736368656d615f6d696e6f72),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73_n6c61796572),
         nullptr,
         "read-only layer",
         nullptr},
        {"detections",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73_n646574656374696f6e73),
         nullptr,
         "read-only detections",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73();
    if (type.tp_name == nullptr) {
        type.tp_name =
            "open_perception_kit_bridge."
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::BoxDetectionsT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::BoxDetectionsT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73_proxy(
    const open_perception_kit::metadata::BoxDetectionsT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::BoxDetectionsT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374526573756c74_n696e5f636f6e74616374(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::CameraContactResultT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (value->in_contact) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374526573756c74_n636f6e666964656e6365(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::CameraContactResultT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->confidence) {
        Py_RETURN_NONE;
    }
    return PyFloat_FromDouble(static_cast<double>(*(value->confidence)));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374526573756c74_n6d6f64656c5f636c6173735f696e646578(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::CameraContactResultT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->model_class_index) {
        Py_RETURN_NONE;
    }
    return py_long_from_unsigned(*(value->model_class_index));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374526573756c74() {
    static PyGetSetDef definitions[] = {
        {"inContact",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374526573756c74_n696e5f636f6e74616374),
         nullptr,
         "read-only in_contact",
         nullptr},
        {"confidence",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374526573756c74_n636f6e666964656e6365),
         nullptr,
         "read-only confidence",
         nullptr},
        {"modelClassIndex",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374526573756c74_n6d6f64656c5f636c6173735f696e646578),
         nullptr,
         "read-only model_class_index",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374526573756c74_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374526573756c74();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f"
                       "6e74616374526573756c74";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::CameraContactResultT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::CameraContactResultT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374526573756c74();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374526573756c74_proxy(
    const open_perception_kit::metadata::CameraContactResultT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::CameraContactResultT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374526573756c74(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374_n6f626a656374(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::CameraContactT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::CameraContactT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374_n726573756c74(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::CameraContactT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->result.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374526573756c74_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::CameraContactT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374_n7375626a6563745f6964(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::CameraContactT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->subject_id) {
        Py_RETURN_NONE;
    }
    return py_long_from_unsigned(*(value->subject_id));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374_n6f626a656374),
         nullptr,
         "read-only object",
         nullptr},
        {"result",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374_n726573756c74),
         nullptr,
         "read-only result",
         nullptr},
        {"subjectId",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374_n7375626a6563745f6964),
         nullptr,
         "read-only subject_id",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374();
    if (type.tp_name == nullptr) {
        type.tp_name =
            "open_perception_kit_bridge."
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::CameraContactT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::CameraContactT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374_proxy(
    const open_perception_kit::metadata::CameraContactT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::CameraContactT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473_n736368656d615f6d616a6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::CameraContactsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473_n736368656d615f6d696e6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::CameraContactsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473_n6c61796572(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::CameraContactsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::CameraContactsT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473_n636f6e7461637473(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::CameraContactsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<open_perception_kit::metadata::CameraContactT>>>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374732e636f6e74616374732e766563746f72(),
        std::addressof(value->contacts),
        proxy_anchor<open_perception_kit::metadata::CameraContactsT>(self));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473_n736368656d615f6d616a6f72),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473_n736368656d615f6d696e6f72),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473_n6c61796572),
         nullptr,
         "read-only layer",
         nullptr},
        {"contacts",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473_n636f6e7461637473),
         nullptr,
         "read-only contacts",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f"
                       "6e7461637473";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::CameraContactsT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::CameraContactsT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473_proxy(
    const open_perception_kit::metadata::CameraContactsT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::CameraContactsT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e43616e646964617465_n636f6e666964656e6365(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::ClassificationCandidateT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->confidence) {
        Py_RETURN_NONE;
    }
    return PyFloat_FromDouble(static_cast<double>(*(value->confidence)));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e43616e646964617465_n636c6173735f6964(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::ClassificationCandidateT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->class_id) {
        Py_RETURN_NONE;
    }
    return py_long_from_unsigned(*(value->class_id));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e43616e646964617465_n74657874(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::ClassificationCandidateT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->text);
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e43616e646964617465() {
    static PyGetSetDef definitions[] = {
        {"confidence",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e43616e646964617465_n636f6e666964656e6365),
         nullptr,
         "read-only confidence",
         nullptr},
        {"classId",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e43616e646964617465_n636c6173735f6964),
         nullptr,
         "read-only class_id",
         nullptr},
        {"text",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e43616e646964617465_n74657874),
         nullptr,
         "read-only text",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e43616e646964617465_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e43616e646964617465();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669"
                       "636174696f6e43616e646964617465";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::ClassificationCandidateT>);
        type.tp_itemsize = 0;
        type.tp_dealloc =
            native_proxy_dealloc<open_perception_kit::metadata::ClassificationCandidateT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e43616e646964617465();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e43616e646964617465_proxy(
    const open_perception_kit::metadata::ClassificationCandidateT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::ClassificationCandidateT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e43616e646964617465(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e_n6f626a656374(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::ClassificationT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::ClassificationT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e_n63616e64696461746573(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::ClassificationT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<open_perception_kit::metadata::ClassificationCandidateT>>>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e2e63616e646964617465732e766563746f72(),
        std::addressof(value->candidates),
        proxy_anchor<open_perception_kit::metadata::ClassificationT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e_n6c6162656c5f736574(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::ClassificationT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->label_set);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e_n6b696e64(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::ClassificationT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_enum(value->kind);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e_n7375626a6563745f6964(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::ClassificationT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->subject_id) {
        Py_RETURN_NONE;
    }
    return py_long_from_unsigned(*(value->subject_id));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e_n6f626a656374),
         nullptr,
         "read-only object",
         nullptr},
        {"candidates",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e_n63616e64696461746573),
         nullptr,
         "read-only candidates",
         nullptr},
        {"labelSet",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e_n6c6162656c5f736574),
         nullptr,
         "read-only label_set",
         nullptr},
        {"kind",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e_n6b696e64),
         nullptr,
         "read-only kind",
         nullptr},
        {"subjectId",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e_n7375626a6563745f6964),
         nullptr,
         "read-only subject_id",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669"
                       "636174696f6e";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::ClassificationT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::ClassificationT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e_proxy(
    const open_perception_kit::metadata::ClassificationT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::ClassificationT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73_n736368656d615f6d616a6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::ClassificationsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73_n736368656d615f6d696e6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::ClassificationsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73_n6c61796572(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::ClassificationsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::ClassificationsT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73_n636c617373696669636174696f6e73(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::ClassificationsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<open_perception_kit::metadata::ClassificationT>>>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e732e636c617373696669636174696f6e732e766563746f72(),
        std::addressof(value->classifications),
        proxy_anchor<open_perception_kit::metadata::ClassificationsT>(self));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73_n736368656d615f6d616a6f72),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73_n736368656d615f6d696e6f72),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73_n6c61796572),
         nullptr,
         "read-only layer",
         nullptr},
        {"classifications",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73_n636c617373696669636174696f6e73),
         nullptr,
         "read-only classifications",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669"
                       "636174696f6e73";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::ClassificationsT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::ClassificationsT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73_proxy(
    const open_perception_kit::metadata::ClassificationsT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::ClassificationsT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73(),
        value,
        std::move(anchor));
}

PyObject *get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_n6964(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ObjectMetaT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->id);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_n706172656e745f6964(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ObjectMetaT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->parent_id);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_n6372656174696f6e5f74735f6e73(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ObjectMetaT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->creation_ts_ns);
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461() {
    static PyGetSetDef definitions[] = {
        {"id",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_n6964),
         nullptr,
         "read-only id",
         nullptr},
        {"parentId",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_n706172656e745f6964),
         nullptr,
         "read-only parent_id",
         nullptr},
        {"creationTsNs",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_n6372656174696f6e5f74735f6e73),
         nullptr,
         "read-only creation_ts_ns",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461();
    if (type.tp_name == nullptr) {
        type.tp_name =
            "open_perception_kit_bridge."
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461";
        type.tp_basicsize = sizeof(native_proxy_object<open_perception_kit::metadata::ObjectMetaT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::ObjectMetaT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_proxy(
    const open_perception_kit::metadata::ObjectMetaT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::ObjectMetaT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e50726f6475636572496e666f_n696e7374616e63655f6964(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ProducerInfoT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->instance_id);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e50726f6475636572496e666f_n636f6d706f6e656e74(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ProducerInfoT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->component);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e50726f6475636572496e666f_n696d706c656d656e746174696f6e(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ProducerInfoT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->implementation);
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e50726f6475636572496e666f() {
    static PyGetSetDef definitions[] = {
        {"instanceId",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e50726f6475636572496e666f_n696e7374616e63655f6964),
         nullptr,
         "read-only instance_id",
         nullptr},
        {"component",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e50726f6475636572496e666f_n636f6d706f6e656e74),
         nullptr,
         "read-only component",
         nullptr},
        {"implementation",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e50726f6475636572496e666f_n696d706c656d656e746174696f6e),
         nullptr,
         "read-only implementation",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e50726f6475636572496e666f_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e50726f6475636572496e666f();
    if (type.tp_name == nullptr) {
        type.tp_name =
            "open_perception_kit_bridge."
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e50726f6475636572496e666f";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::ProducerInfoT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::ProducerInfoT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e50726f6475636572496e666f();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e50726f6475636572496e666f_proxy(
    const open_perception_kit::metadata::ProducerInfoT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::ProducerInfoT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e50726f6475636572496e666f(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_n656e67696e65(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::LayerInfoT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->engine);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_n6d6f64656c(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::LayerInfoT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->model);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_n74616773(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::LayerInfoT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->tags);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_n696e6665725f656c656d656e745f6964(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::LayerInfoT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->infer_element_id);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_n6c6162656c5f66616d696c79(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::LayerInfoT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->label_family);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_n636f6e74656e745f74797065(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::LayerInfoT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->content_type);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_n636f6d706f736974696e675f6d6f6465(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::LayerInfoT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->compositing_mode);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_n70726f6475636572(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::LayerInfoT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->producer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e50726f6475636572496e666f_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::LayerInfoT>(self));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f() {
    static PyGetSetDef definitions[] = {
        {"engine",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_n656e67696e65),
         nullptr,
         "read-only engine",
         nullptr},
        {"model",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_n6d6f64656c),
         nullptr,
         "read-only model",
         nullptr},
        {"tags",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_n74616773),
         nullptr,
         "read-only tags",
         nullptr},
        {"inferElementId",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_n696e6665725f656c656d656e745f6964),
         nullptr,
         "read-only infer_element_id",
         nullptr},
        {"labelFamily",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_n6c6162656c5f66616d696c79),
         nullptr,
         "read-only label_family",
         nullptr},
        {"contentType",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_n636f6e74656e745f74797065),
         nullptr,
         "read-only content_type",
         nullptr},
        {"compositingMode",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_n636f6d706f736974696e675f6d6f6465),
         nullptr,
         "read-only compositing_mode",
         nullptr},
        {"producer",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_n70726f6475636572),
         nullptr,
         "read-only producer",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f();
    if (type.tp_name == nullptr) {
        type.tp_name =
            "open_perception_kit_bridge."
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f";
        type.tp_basicsize = sizeof(native_proxy_object<open_perception_kit::metadata::LayerInfoT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::LayerInfoT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_proxy(
    const open_perception_kit::metadata::LayerInfoT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::LayerInfoT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_n6672616d655f6964(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ImageSpaceT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->frame_id);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_n7769647468(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ImageSpaceT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->width);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_n686569676874(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ImageSpaceT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->height);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_n6f726967696e5f78(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ImageSpaceT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->origin_x));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_n6f726967696e5f79(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ImageSpaceT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->origin_y));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_n657874656e745f7769647468(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ImageSpaceT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->extent_width));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_n657874656e745f686569676874(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ImageSpaceT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->extent_height));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365() {
    static PyGetSetDef definitions[] = {
        {"frameId",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_n6672616d655f6964),
         nullptr,
         "read-only frame_id",
         nullptr},
        {"width",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_n7769647468),
         nullptr,
         "read-only width",
         nullptr},
        {"height",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_n686569676874),
         nullptr,
         "read-only height",
         nullptr},
        {"originX",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_n6f726967696e5f78),
         nullptr,
         "read-only origin_x",
         nullptr},
        {"originY",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_n6f726967696e5f79),
         nullptr,
         "read-only origin_y",
         nullptr},
        {"extentWidth",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_n657874656e745f7769647468),
         nullptr,
         "read-only extent_width",
         nullptr},
        {"extentHeight",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_n657874656e745f686569676874),
         nullptr,
         "read-only extent_height",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365();
    if (type.tp_name == nullptr) {
        type.tp_name =
            "open_perception_kit_bridge."
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365";
        type.tp_basicsize = sizeof(native_proxy_object<open_perception_kit::metadata::ImageSpaceT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::ImageSpaceT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_proxy(
    const open_perception_kit::metadata::ImageSpaceT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::ImageSpaceT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365(),
        value,
        std::move(anchor));
}

PyObject *get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78_n78(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::BoundingBoxT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->x));
}

PyObject *get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78_n79(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::BoundingBoxT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->y));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78_n7769647468(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::BoundingBoxT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->width));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78_n686569676874(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::BoundingBoxT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->height));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78_n7370616365(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::BoundingBoxT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->space.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::BoundingBoxT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78_n756e697473(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::BoundingBoxT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_enum(value->units);
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78() {
    static PyGetSetDef definitions[] = {
        {"x",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78_n78),
         nullptr,
         "read-only x",
         nullptr},
        {"y",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78_n79),
         nullptr,
         "read-only y",
         nullptr},
        {"width",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78_n7769647468),
         nullptr,
         "read-only width",
         nullptr},
        {"height",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78_n686569676874),
         nullptr,
         "read-only height",
         nullptr},
        {"space",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78_n7370616365),
         nullptr,
         "read-only space",
         nullptr},
        {"units",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78_n756e697473),
         nullptr,
         "read-only units",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78();
    if (type.tp_name == nullptr) {
        type.tp_name =
            "open_perception_kit_bridge."
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::BoundingBoxT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::BoundingBoxT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78_proxy(
    const open_perception_kit::metadata::BoundingBoxT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::BoundingBoxT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506f696e743266_n78(PyObject *self,
                                                                                  void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::Point2fT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->x));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506f696e743266_n79(PyObject *self,
                                                                                  void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::Point2fT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->y));
}

PyGetSetDef *getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506f696e743266() {
    static PyGetSetDef definitions[] = {
        {"x",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506f696e743266_n78),
         nullptr,
         "read-only x",
         nullptr},
        {"y",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506f696e743266_n79),
         nullptr,
         "read-only y",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506f696e743266_type() {
    auto &type = pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506f696e743266();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506f696e743266";
        type.tp_basicsize = sizeof(native_proxy_object<open_perception_kit::metadata::Point2fT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::Point2fT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506f696e743266();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506f696e743266_proxy(
    const open_perception_kit::metadata::Point2fT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::Point2fT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506f696e743266(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461_n7769647468(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::BitmapDataT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->width);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461_n686569676874(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::BitmapDataT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->height);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461_n656e636f64696e67(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::BitmapDataT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_enum(value->encoding);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461_n706978656c73(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::BitmapDataT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<std::vector<std::uint8_t>>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d6170446174612e706978656c732e766563746f72(),
        std::addressof(value->pixels),
        proxy_anchor<open_perception_kit::metadata::BitmapDataT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461_n7370616365(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::BitmapDataT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->space.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::BitmapDataT>(self));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461() {
    static PyGetSetDef definitions[] = {
        {"width",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461_n7769647468),
         nullptr,
         "read-only width",
         nullptr},
        {"height",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461_n686569676874),
         nullptr,
         "read-only height",
         nullptr},
        {"encoding",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461_n656e636f64696e67),
         nullptr,
         "read-only encoding",
         nullptr},
        {"pixels",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461_n706978656c73),
         nullptr,
         "read-only pixels",
         nullptr},
        {"space",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461_n7370616365),
         nullptr,
         "read-only space",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461();
    if (type.tp_name == nullptr) {
        type.tp_name =
            "open_perception_kit_bridge."
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461";
        type.tp_basicsize = sizeof(native_proxy_object<open_perception_kit::metadata::BitmapDataT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::BitmapDataT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461_proxy(
    const open_perception_kit::metadata::BitmapDataT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::BitmapDataT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f4672616d65436f6e74657874_n6f626a656374(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::VideoFrameContextT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::VideoFrameContextT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f4672616d65436f6e74657874_n6f726967696e616c5f7769647468(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::VideoFrameContextT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->original_width);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f4672616d65436f6e74657874_n6f726967696e616c5f686569676874(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::VideoFrameContextT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->original_height);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f4672616d65436f6e74657874_n70726573656e746174696f6e5f74735f6e73(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::VideoFrameContextT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->presentation_ts_ns) {
        Py_RETURN_NONE;
    }
    return py_long_from_unsigned(*(value->presentation_ts_ns));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f4672616d65436f6e74657874() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f4672616d65436f6e74657874_n6f626a656374),
         nullptr,
         "read-only object",
         nullptr},
        {"originalWidth",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f4672616d65436f6e74657874_n6f726967696e616c5f7769647468),
         nullptr,
         "read-only original_width",
         nullptr},
        {"originalHeight",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f4672616d65436f6e74657874_n6f726967696e616c5f686569676874),
         nullptr,
         "read-only original_height",
         nullptr},
        {"presentationTsNs",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f4672616d65436f6e74657874_n70726573656e746174696f6e5f74735f6e73),
         nullptr,
         "read-only presentation_ts_ns",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f4672616d65436f6e74657874_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f4672616d65436f6e74657874();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f467261"
                       "6d65436f6e74657874";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::VideoFrameContextT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::VideoFrameContextT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f4672616d65436f6e74657874();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f4672616d65436f6e74657874_proxy(
    const open_perception_kit::metadata::VideoFrameContextT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::VideoFrameContextT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f4672616d65436f6e74657874(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874_n6f626a656374(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::AudioFrameContextT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::AudioFrameContextT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874_n6f726967696e616c5f6368616e6e656c73(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::AudioFrameContextT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->original_channels);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874_n6f726967696e616c5f6672657175656e6379(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::AudioFrameContextT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->original_frequency);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874_n6f726967696e616c5f73616d706c655f636f756e74(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::AudioFrameContextT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->original_sample_count);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874_n6375745f6c6566745f73616d706c655f636f756e74(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::AudioFrameContextT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->cut_left_sample_count);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874_n6375745f72696768745f73616d706c655f636f756e74(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::AudioFrameContextT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->cut_right_sample_count);
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874_n6f626a656374),
         nullptr,
         "read-only object",
         nullptr},
        {"originalChannels",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874_n6f726967696e616c5f6368616e6e656c73),
         nullptr,
         "read-only original_channels",
         nullptr},
        {"originalFrequency",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874_n6f726967696e616c5f6672657175656e6379),
         nullptr,
         "read-only original_frequency",
         nullptr},
        {"originalSampleCount",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874_n6f726967696e616c5f73616d706c655f636f756e74),
         nullptr,
         "read-only original_sample_count",
         nullptr},
        {"cutLeftSampleCount",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874_n6375745f6c6566745f73616d706c655f636f756e74),
         nullptr,
         "read-only cut_left_sample_count",
         nullptr},
        {"cutRightSampleCount",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874_n6375745f72696768745f73616d706c655f636f756e74),
         nullptr,
         "read-only cut_right_sample_count",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f467261"
                       "6d65436f6e74657874";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::AudioFrameContextT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::AudioFrameContextT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874_proxy(
    const open_perception_kit::metadata::AudioFrameContextT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::AudioFrameContextT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874_n736368656d615f6d616a6f72(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::FrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874_n736368656d615f6d696e6f72(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::FrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874_n6c61796572(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::FrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::FrameContextT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874_n766964656f(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::FrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->video.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f4672616d65436f6e74657874_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::FrameContextT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874_n617564696f(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::FrameContextT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->audio.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::FrameContextT>(self));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874_n736368656d615f6d616a6f72),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874_n736368656d615f6d696e6f72),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874_n6c61796572),
         nullptr,
         "read-only layer",
         nullptr},
        {"video",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874_n766964656f),
         nullptr,
         "read-only video",
         nullptr},
        {"audio",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874_n617564696f),
         nullptr,
         "read-only audio",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874();
    if (type.tp_name == nullptr) {
        type.tp_name =
            "open_perception_kit_bridge."
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::FrameContextT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::FrameContextT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874_proxy(
    const open_perception_kit::metadata::FrameContextT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::FrameContextT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a65416e676c6573_n7961775f64656772656573(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::GazeAnglesT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->yaw_degrees));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a65416e676c6573_n70697463685f64656772656573(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::GazeAnglesT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->pitch_degrees));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a65416e676c6573() {
    static PyGetSetDef definitions[] = {
        {"yawDegrees",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a65416e676c6573_n7961775f64656772656573),
         nullptr,
         "read-only yaw_degrees",
         nullptr},
        {"pitchDegrees",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a65416e676c6573_n70697463685f64656772656573),
         nullptr,
         "read-only pitch_degrees",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a65416e676c6573_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a65416e676c6573();
    if (type.tp_name == nullptr) {
        type.tp_name =
            "open_perception_kit_bridge."
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a65416e676c6573";
        type.tp_basicsize = sizeof(native_proxy_object<open_perception_kit::metadata::GazeAnglesT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::GazeAnglesT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a65416e676c6573();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a65416e676c6573_proxy(
    const open_perception_kit::metadata::GazeAnglesT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::GazeAnglesT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a65416e676c6573(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74_n6f626a656374(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::GazeMeasurementT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::GazeMeasurementT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74_n7375626a6563745f6964(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::GazeMeasurementT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->subject_id) {
        Py_RETURN_NONE;
    }
    return py_long_from_unsigned(*(value->subject_id));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74_n616e676c6573(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::GazeMeasurementT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->angles.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a65416e676c6573_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::GazeMeasurementT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74_n636f6e666964656e6365(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::GazeMeasurementT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->confidence) {
        Py_RETURN_NONE;
    }
    return PyFloat_FromDouble(static_cast<double>(*(value->confidence)));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74_n6f626a656374),
         nullptr,
         "read-only object",
         nullptr},
        {"subjectId",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74_n7375626a6563745f6964),
         nullptr,
         "read-only subject_id",
         nullptr},
        {"angles",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74_n616e676c6573),
         nullptr,
         "read-only angles",
         nullptr},
        {"confidence",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74_n636f6e666964656e6365),
         nullptr,
         "read-only confidence",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d656173"
                       "7572656d656e74";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::GazeMeasurementT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::GazeMeasurementT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74_proxy(
    const open_perception_kit::metadata::GazeMeasurementT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::GazeMeasurementT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473_n736368656d615f6d616a6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::GazeMeasurementsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473_n736368656d615f6d696e6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::GazeMeasurementsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473_n6c61796572(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::GazeMeasurementsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::GazeMeasurementsT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473_n6d6561737572656d656e7473(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::GazeMeasurementsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<open_perception_kit::metadata::GazeMeasurementT>>>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74732e6d6561737572656d656e74732e766563746f72(),
        std::addressof(value->measurements),
        proxy_anchor<open_perception_kit::metadata::GazeMeasurementsT>(self));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473_n736368656d615f6d616a6f72),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473_n736368656d615f6d696e6f72),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473_n6c61796572),
         nullptr,
         "read-only layer",
         nullptr},
        {"measurements",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473_n6d6561737572656d656e7473),
         nullptr,
         "read-only measurements",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d656173"
                       "7572656d656e7473";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::GazeMeasurementsT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::GazeMeasurementsT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473_proxy(
    const open_perception_kit::metadata::GazeMeasurementsT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::GazeMeasurementsT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b_n6f626a656374(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::InstanceSegmentationMaskT> *>(
        self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::InstanceSegmentationMaskT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b_n6269746d6170(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::InstanceSegmentationMaskT> *>(
        self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->bitmap.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::InstanceSegmentationMaskT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b_n636c6173735f6964(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::InstanceSegmentationMaskT> *>(
        self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->class_id) {
        Py_RETURN_NONE;
    }
    return py_long_from_unsigned(*(value->class_id));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b_n636f6e666964656e6365(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::InstanceSegmentationMaskT> *>(
        self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->confidence) {
        Py_RETURN_NONE;
    }
    return PyFloat_FromDouble(static_cast<double>(*(value->confidence)));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b_n626f78(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::InstanceSegmentationMaskT> *>(
        self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->box.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::InstanceSegmentationMaskT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b_n6c6162656c5f736574(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::InstanceSegmentationMaskT> *>(
        self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->label_set);
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b_n6f626a656374),
         nullptr,
         "read-only object",
         nullptr},
        {"bitmap",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b_n6269746d6170),
         nullptr,
         "read-only bitmap",
         nullptr},
        {"classId",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b_n636c6173735f6964),
         nullptr,
         "read-only class_id",
         nullptr},
        {"confidence",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b_n636f6e666964656e6365),
         nullptr,
         "read-only confidence",
         nullptr},
        {"box",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b_n626f78),
         nullptr,
         "read-only box",
         nullptr},
        {"labelSet",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b_n6c6162656c5f736574),
         nullptr,
         "read-only label_set",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e6365"
                       "5365676d656e746174696f6e4d61736b";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::InstanceSegmentationMaskT>);
        type.tp_itemsize = 0;
        type.tp_dealloc =
            native_proxy_dealloc<open_perception_kit::metadata::InstanceSegmentationMaskT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b_proxy(
    const open_perception_kit::metadata::InstanceSegmentationMaskT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::InstanceSegmentationMaskT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73_n736368656d615f6d616a6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::InstanceSegmentationMasksT> *>(
        self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73_n736368656d615f6d696e6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::InstanceSegmentationMasksT> *>(
        self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73_n6c61796572(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::InstanceSegmentationMasksT> *>(
        self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::InstanceSegmentationMasksT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73_n6d61736b73(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::InstanceSegmentationMasksT> *>(
        self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<open_perception_kit::metadata::InstanceSegmentationMaskT>>>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72(),
        std::addressof(value->masks),
        proxy_anchor<open_perception_kit::metadata::InstanceSegmentationMasksT>(self));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73_n736368656d615f6d616a6f72),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73_n736368656d615f6d696e6f72),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73_n6c61796572),
         nullptr,
         "read-only layer",
         nullptr},
        {"masks",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73_n6d61736b73),
         nullptr,
         "read-only masks",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e6365"
                       "5365676d656e746174696f6e4d61736b73";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::InstanceSegmentationMasksT>);
        type.tp_itemsize = 0;
        type.tp_dealloc =
            native_proxy_dealloc<open_perception_kit::metadata::InstanceSegmentationMasksT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73_proxy(
    const open_perception_kit::metadata::InstanceSegmentationMasksT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::InstanceSegmentationMasksT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e456d62656464696e675370616365_n6964(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::EmbeddingSpaceT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->id);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e456d62656464696e675370616365_n64696d656e73696f6e73(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::EmbeddingSpaceT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->dimensions);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e456d62656464696e675370616365_n6d6574726963(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::EmbeddingSpaceT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_enum(value->metric);
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e456d62656464696e675370616365() {
    static PyGetSetDef definitions[] = {
        {"id",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e456d62656464696e675370616365_n6964),
         nullptr,
         "read-only id",
         nullptr},
        {"dimensions",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e456d62656464696e675370616365_n64696d656e73696f6e73),
         nullptr,
         "read-only dimensions",
         nullptr},
        {"metric",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e456d62656464696e675370616365_n6d6574726963),
         nullptr,
         "read-only metric",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e456d62656464696e675370616365_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e456d62656464696e675370616365();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e456d62656464696e"
                       "675370616365";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::EmbeddingSpaceT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::EmbeddingSpaceT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e456d62656464696e675370616365();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e456d62656464696e675370616365_proxy(
    const open_perception_kit::metadata::EmbeddingSpaceT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::EmbeddingSpaceT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e456d62656464696e675370616365(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67_n6f626a656374(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::ObjectEmbeddingT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::ObjectEmbeddingT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67_n76616c756573(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::ObjectEmbeddingT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<std::vector<float>>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e672e76616c7565732e766563746f72(),
        std::addressof(value->values),
        proxy_anchor<open_perception_kit::metadata::ObjectEmbeddingT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67_n7370616365(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::ObjectEmbeddingT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->space.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e456d62656464696e675370616365_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::ObjectEmbeddingT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67_n7375626a6563745f6964(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::ObjectEmbeddingT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->subject_id) {
        Py_RETURN_NONE;
    }
    return py_long_from_unsigned(*(value->subject_id));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67_n6f626a656374),
         nullptr,
         "read-only object",
         nullptr},
        {"values",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67_n76616c756573),
         nullptr,
         "read-only values",
         nullptr},
        {"space",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67_n7370616365),
         nullptr,
         "read-only space",
         nullptr},
        {"subjectId",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67_n7375626a6563745f6964),
         nullptr,
         "read-only subject_id",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d"
                       "62656464696e67";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::ObjectEmbeddingT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::ObjectEmbeddingT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67_proxy(
    const open_perception_kit::metadata::ObjectEmbeddingT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::ObjectEmbeddingT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773_n736368656d615f6d616a6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::ObjectEmbeddingsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773_n736368656d615f6d696e6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::ObjectEmbeddingsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773_n6c61796572(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::ObjectEmbeddingsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::ObjectEmbeddingsT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773_n656d62656464696e6773(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::ObjectEmbeddingsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<open_perception_kit::metadata::ObjectEmbeddingT>>>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67732e656d62656464696e67732e766563746f72(),
        std::addressof(value->embeddings),
        proxy_anchor<open_perception_kit::metadata::ObjectEmbeddingsT>(self));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773_n736368656d615f6d616a6f72),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773_n736368656d615f6d696e6f72),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773_n6c61796572),
         nullptr,
         "read-only layer",
         nullptr},
        {"embeddings",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773_n656d62656464696e6773),
         nullptr,
         "read-only embeddings",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d"
                       "62656464696e6773";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::ObjectEmbeddingsT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::ObjectEmbeddingsT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773_proxy(
    const open_perception_kit::metadata::ObjectEmbeddingsT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::ObjectEmbeddingsT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_n6f626a656374(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ObjectTrackT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::ObjectTrackT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_n736f757263655f6964(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ObjectTrackT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->source_id);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_n747261636b5f6964(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ObjectTrackT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->track_id);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_n626f78(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ObjectTrackT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->box.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::ObjectTrackT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_n636f6e666964656e6365(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ObjectTrackT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->confidence));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_n636c6173735f6964(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ObjectTrackT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->class_id) {
        Py_RETURN_NONE;
    }
    return py_long_from_unsigned(*(value->class_id));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_n74657874(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ObjectTrackT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->text);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_n646961676e6f73746963(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ObjectTrackT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->diagnostic);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_n7072656469637465645f6f6e6c79(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ObjectTrackT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (value->predicted_only) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_n6f626a656374),
         nullptr,
         "read-only object",
         nullptr},
        {"sourceId",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_n736f757263655f6964),
         nullptr,
         "read-only source_id",
         nullptr},
        {"trackId",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_n747261636b5f6964),
         nullptr,
         "read-only track_id",
         nullptr},
        {"box",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_n626f78),
         nullptr,
         "read-only box",
         nullptr},
        {"confidence",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_n636f6e666964656e6365),
         nullptr,
         "read-only confidence",
         nullptr},
        {"classId",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_n636c6173735f6964),
         nullptr,
         "read-only class_id",
         nullptr},
        {"text",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_n74657874),
         nullptr,
         "read-only text",
         nullptr},
        {"diagnostic",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_n646961676e6f73746963),
         nullptr,
         "read-only diagnostic",
         nullptr},
        {"predictedOnly",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_n7072656469637465645f6f6e6c79),
         nullptr,
         "read-only predicted_only",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b();
    if (type.tp_name == nullptr) {
        type.tp_name =
            "open_perception_kit_bridge."
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::ObjectTrackT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::ObjectTrackT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_proxy(
    const open_perception_kit::metadata::ObjectTrackT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::ObjectTrackT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73_n736368656d615f6d616a6f72(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ObjectTracksT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73_n736368656d615f6d696e6f72(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ObjectTracksT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73_n6c61796572(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ObjectTracksT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::ObjectTracksT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73_n747261636b73(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ObjectTracksT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<open_perception_kit::metadata::ObjectTrackT>>>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b732e747261636b732e766563746f72(),
        std::addressof(value->tracks),
        proxy_anchor<open_perception_kit::metadata::ObjectTracksT>(self));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73_n736368656d615f6d616a6f72),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73_n736368656d615f6d696e6f72),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73_n6c61796572),
         nullptr,
         "read-only layer",
         nullptr},
        {"tracks",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73_n747261636b73),
         nullptr,
         "read-only tracks",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73();
    if (type.tp_name == nullptr) {
        type.tp_name =
            "open_perception_kit_bridge."
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::ObjectTracksT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::ObjectTracksT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73_proxy(
    const open_perception_kit::metadata::ObjectTracksT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::ObjectTracksT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67_n6e616d65(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ScopeTimingT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->name);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67_n6d65616e5f6d73(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ScopeTimingT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->mean_ms) {
        Py_RETURN_NONE;
    }
    return PyFloat_FromDouble(static_cast<double>(*(value->mean_ms)));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67_n7039355f6d73(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ScopeTimingT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->p95_ms) {
        Py_RETURN_NONE;
    }
    return PyFloat_FromDouble(static_cast<double>(*(value->p95_ms)));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67_n73636f70655f6964(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::ScopeTimingT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->scope_id);
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67() {
    static PyGetSetDef definitions[] = {
        {"name",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67_n6e616d65),
         nullptr,
         "read-only name",
         nullptr},
        {"meanMs",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67_n6d65616e5f6d73),
         nullptr,
         "read-only mean_ms",
         nullptr},
        {"p95Ms",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67_n7039355f6d73),
         nullptr,
         "read-only p95_ms",
         nullptr},
        {"scopeId",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67_n73636f70655f6964),
         nullptr,
         "read-only scope_id",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67();
    if (type.tp_name == nullptr) {
        type.tp_name =
            "open_perception_kit_bridge."
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::ScopeTimingT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::ScopeTimingT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67_proxy(
    const open_perception_kit::metadata::ScopeTimingT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::ScopeTimingT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67(),
        value,
        std::move(anchor));
}

PyObject *get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d6552617465_n667073(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::FrameRateT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->fps) {
        Py_RETURN_NONE;
    }
    return PyFloat_FromDouble(static_cast<double>(*(value->fps)));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d6552617465() {
    static PyGetSetDef definitions[] = {
        {"fps",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d6552617465_n667073),
         nullptr,
         "read-only fps",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d6552617465_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d6552617465();
    if (type.tp_name == nullptr) {
        type.tp_name =
            "open_perception_kit_bridge."
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d6552617465";
        type.tp_basicsize = sizeof(native_proxy_object<open_perception_kit::metadata::FrameRateT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::FrameRateT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d6552617465();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d6552617465_proxy(
    const open_perception_kit::metadata::FrameRateT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::FrameRateT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d6552617465(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473_n736368656d615f6d616a6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::PerformanceMeasurementsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473_n736368656d615f6d696e6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::PerformanceMeasurementsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473_n706978656c5f666f726d6174(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::PerformanceMeasurementsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->pixel_format);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473_n74696d696e6773(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::PerformanceMeasurementsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<open_perception_kit::metadata::ScopeTimingT>>>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e74732e74696d696e67732e766563746f72(),
        std::addressof(value->timings),
        proxy_anchor<open_perception_kit::metadata::PerformanceMeasurementsT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473_n6672616d655f72617465(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::PerformanceMeasurementsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->frame_rate.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d6552617465_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::PerformanceMeasurementsT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473_n70726f6475636572(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::PerformanceMeasurementsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->producer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e50726f6475636572496e666f_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::PerformanceMeasurementsT>(self));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473_n736368656d615f6d616a6f72),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473_n736368656d615f6d696e6f72),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"pixelFormat",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473_n706978656c5f666f726d6174),
         nullptr,
         "read-only pixel_format",
         nullptr},
        {"timings",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473_n74696d696e6773),
         nullptr,
         "read-only timings",
         nullptr},
        {"frameRate",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473_n6672616d655f72617465),
         nullptr,
         "read-only frame_rate",
         nullptr},
        {"producer",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473_n70726f6475636572),
         nullptr,
         "read-only producer",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d61"
                       "6e63654d6561737572656d656e7473";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::PerformanceMeasurementsT>);
        type.tp_itemsize = 0;
        type.tp_dealloc =
            native_proxy_dealloc<open_perception_kit::metadata::PerformanceMeasurementsT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473_proxy(
    const open_perception_kit::metadata::PerformanceMeasurementsT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::PerformanceMeasurementsT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365526573756c74_n706572736f6e5f70726573656e74(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::PersonPresenceResultT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (value->person_present) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365526573756c74_n7965735f73636f7265(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::PersonPresenceResultT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->yes_score) {
        Py_RETURN_NONE;
    }
    return PyFloat_FromDouble(static_cast<double>(*(value->yes_score)));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365526573756c74_n6e6f5f73636f7265(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::PersonPresenceResultT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->no_score) {
        Py_RETURN_NONE;
    }
    return PyFloat_FromDouble(static_cast<double>(*(value->no_score)));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365526573756c74() {
    static PyGetSetDef definitions[] = {
        {"personPresent",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365526573756c74_n706572736f6e5f70726573656e74),
         nullptr,
         "read-only person_present",
         nullptr},
        {"yesScore",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365526573756c74_n7965735f73636f7265),
         nullptr,
         "read-only yes_score",
         nullptr},
        {"noScore",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365526573756c74_n6e6f5f73636f7265),
         nullptr,
         "read-only no_score",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365526573756c74_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365526573756c74();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e5072"
                       "6573656e6365526573756c74";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::PersonPresenceResultT>);
        type.tp_itemsize = 0;
        type.tp_dealloc =
            native_proxy_dealloc<open_perception_kit::metadata::PersonPresenceResultT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365526573756c74();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365526573756c74_proxy(
    const open_perception_kit::metadata::PersonPresenceResultT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::PersonPresenceResultT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365526573756c74(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365_n6f626a656374(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::PersonPresenceT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::PersonPresenceT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365_n726573756c74(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::PersonPresenceT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->result.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365526573756c74_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::PersonPresenceT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365_n7375626a6563745f6964(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::PersonPresenceT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->subject_id) {
        Py_RETURN_NONE;
    }
    return py_long_from_unsigned(*(value->subject_id));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365_n6f626a656374),
         nullptr,
         "read-only object",
         nullptr},
        {"result",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365_n726573756c74),
         nullptr,
         "read-only result",
         nullptr},
        {"subjectId",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365_n7375626a6563745f6964),
         nullptr,
         "read-only subject_id",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e5072"
                       "6573656e6365";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::PersonPresenceT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::PersonPresenceT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365_proxy(
    const open_perception_kit::metadata::PersonPresenceT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::PersonPresenceT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573_n736368656d615f6d616a6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::PersonPresencesT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573_n736368656d615f6d696e6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::PersonPresencesT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573_n6c61796572(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::PersonPresencesT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::PersonPresencesT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573_n70726573656e636573(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::PersonPresencesT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<open_perception_kit::metadata::PersonPresenceT>>>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365732e70726573656e6365732e766563746f72(),
        std::addressof(value->presences),
        proxy_anchor<open_perception_kit::metadata::PersonPresencesT>(self));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573_n736368656d615f6d616a6f72),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573_n736368656d615f6d696e6f72),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573_n6c61796572),
         nullptr,
         "read-only layer",
         nullptr},
        {"presences",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573_n70726573656e636573),
         nullptr,
         "read-only presences",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e5072"
                       "6573656e636573";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::PersonPresencesT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::PersonPresencesT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573_proxy(
    const open_perception_kit::metadata::PersonPresencesT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::PersonPresencesT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b_n6f626a656374(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::SegmentationMaskT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::SegmentationMaskT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b_n6269746d6170(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::SegmentationMaskT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->bitmap.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::SegmentationMaskT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b_n6d65616e696e67(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::SegmentationMaskT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_enum(value->meaning);
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b_n6f626a656374),
         nullptr,
         "read-only object",
         nullptr},
        {"bitmap",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b_n6269746d6170),
         nullptr,
         "read-only bitmap",
         nullptr},
        {"meaning",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b_n6d65616e696e67),
         nullptr,
         "read-only meaning",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e7461"
                       "74696f6e4d61736b";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::SegmentationMaskT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::SegmentationMaskT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b_proxy(
    const open_perception_kit::metadata::SegmentationMaskT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::SegmentationMaskT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73_n736368656d615f6d616a6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::SegmentationMasksT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73_n736368656d615f6d696e6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::SegmentationMasksT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73_n6c61796572(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::SegmentationMasksT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::SegmentationMasksT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73_n6d61736b73(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::SegmentationMasksT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<open_perception_kit::metadata::SegmentationMaskT>>>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72(),
        std::addressof(value->masks),
        proxy_anchor<open_perception_kit::metadata::SegmentationMasksT>(self));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73_n736368656d615f6d616a6f72),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73_n736368656d615f6d696e6f72),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73_n6c61796572),
         nullptr,
         "read-only layer",
         nullptr},
        {"masks",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73_n6d61736b73),
         nullptr,
         "read-only masks",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e7461"
                       "74696f6e4d61736b73";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::SegmentationMasksT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::SegmentationMasksT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73_proxy(
    const open_perception_kit::metadata::SegmentationMasksT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::SegmentationMasksT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b_n6f626a656374(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::SemanticSegmentationMaskT> *>(
        self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::SemanticSegmentationMaskT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b_n6269746d6170(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::SemanticSegmentationMaskT> *>(
        self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->bitmap.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::SemanticSegmentationMaskT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b_n6c6162656c5f736574(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::SemanticSegmentationMaskT> *>(
        self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_string_from_std(value->label_set);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b_n766f69645f76616c7565(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::SemanticSegmentationMaskT> *>(
        self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->void_value) {
        Py_RETURN_NONE;
    }
    return py_long_from_unsigned(*(value->void_value));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b_n6f626a656374),
         nullptr,
         "read-only object",
         nullptr},
        {"bitmap",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b_n6269746d6170),
         nullptr,
         "read-only bitmap",
         nullptr},
        {"labelSet",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b_n6c6162656c5f736574),
         nullptr,
         "read-only label_set",
         nullptr},
        {"voidValue",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b_n766f69645f76616c7565),
         nullptr,
         "read-only void_value",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e746963"
                       "5365676d656e746174696f6e4d61736b";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::SemanticSegmentationMaskT>);
        type.tp_itemsize = 0;
        type.tp_dealloc =
            native_proxy_dealloc<open_perception_kit::metadata::SemanticSegmentationMaskT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b_proxy(
    const open_perception_kit::metadata::SemanticSegmentationMaskT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::SemanticSegmentationMaskT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73_n736368656d615f6d616a6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::SemanticSegmentationMasksT> *>(
        self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73_n736368656d615f6d696e6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::SemanticSegmentationMasksT> *>(
        self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73_n6c61796572(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::SemanticSegmentationMasksT> *>(
        self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::SemanticSegmentationMasksT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73_n6d61736b73(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::SemanticSegmentationMasksT> *>(
        self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<open_perception_kit::metadata::SemanticSegmentationMaskT>>>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72(),
        std::addressof(value->masks),
        proxy_anchor<open_perception_kit::metadata::SemanticSegmentationMasksT>(self));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73_n736368656d615f6d616a6f72),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73_n736368656d615f6d696e6f72),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73_n6c61796572),
         nullptr,
         "read-only layer",
         nullptr},
        {"masks",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73_n6d61736b73),
         nullptr,
         "read-only masks",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e746963"
                       "5365676d656e746174696f6e4d61736b73";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::SemanticSegmentationMasksT>);
        type.tp_itemsize = 0;
        type.tp_dealloc =
            native_proxy_dealloc<open_perception_kit::metadata::SemanticSegmentationMasksT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73_proxy(
    const open_perception_kit::metadata::SemanticSegmentationMasksT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::SemanticSegmentationMasksT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365_n6f626a656374(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::TrackTraceT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::TrackTraceT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365_n747261636b5f6964(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::TrackTraceT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->track_id);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365_n706f696e7473(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::TrackTraceT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<open_perception_kit::metadata::Point2fT>>>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b54726163652e706f696e74732e766563746f72(),
        std::addressof(value->points),
        proxy_anchor<open_perception_kit::metadata::TrackTraceT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365_n7370616365(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::TrackTraceT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->space.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::TrackTraceT>(self));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365_n6f626a656374),
         nullptr,
         "read-only object",
         nullptr},
        {"trackId",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365_n747261636b5f6964),
         nullptr,
         "read-only track_id",
         nullptr},
        {"points",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365_n706f696e7473),
         nullptr,
         "read-only points",
         nullptr},
        {"space",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365_n7370616365),
         nullptr,
         "read-only space",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365();
    if (type.tp_name == nullptr) {
        type.tp_name =
            "open_perception_kit_bridge."
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365";
        type.tp_basicsize = sizeof(native_proxy_object<open_perception_kit::metadata::TrackTraceT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::TrackTraceT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365_proxy(
    const open_perception_kit::metadata::TrackTraceT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::TrackTraceT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573_n736368656d615f6d616a6f72(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::TrackTracesT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573_n736368656d615f6d696e6f72(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::TrackTracesT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573_n6c61796572(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::TrackTracesT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::TrackTracesT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573_n747261636573(
    PyObject *self, void *) {
    const auto *proxy =
        reinterpret_cast<const native_proxy_object<open_perception_kit::metadata::TrackTracesT> *>(
            self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<open_perception_kit::metadata::TrackTraceT>>>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365732e7472616365732e766563746f72(),
        std::addressof(value->traces),
        proxy_anchor<open_perception_kit::metadata::TrackTracesT>(self));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573_n736368656d615f6d616a6f72),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573_n736368656d615f6d696e6f72),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573_n6c61796572),
         nullptr,
         "read-only layer",
         nullptr},
        {"traces",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573_n747261636573),
         nullptr,
         "read-only traces",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573();
    if (type.tp_name == nullptr) {
        type.tp_name =
            "open_perception_kit_bridge."
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::TrackTracesT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::TrackTracesT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573_proxy(
    const open_perception_kit::metadata::TrackTracesT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::TrackTracesT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_n6f626a656374(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::TurnDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::TurnDetectionT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_n7475726e5f70726f626162696c697479(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::TurnDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->turn_probability));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_n7475726e5f636f6d706c657465(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::TurnDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (value->turn_complete) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_n77696e646f775f73746172745f73616d706c65(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::TurnDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->window_start_sample);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_n77696e646f775f656e645f73616d706c65(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::TurnDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->window_end_sample);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_n77696e646f775f73746172745f74735f6e73(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::TurnDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->window_start_ts_ns) {
        Py_RETURN_NONE;
    }
    return py_long_from_unsigned(*(value->window_start_ts_ns));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_n77696e646f775f656e645f74735f6e73(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::TurnDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->window_end_ts_ns) {
        Py_RETURN_NONE;
    }
    return py_long_from_unsigned(*(value->window_end_ts_ns));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_n646973636f6e74696e75697479(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::TurnDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (value->discontinuity) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_n6f626a656374),
         nullptr,
         "read-only object",
         nullptr},
        {"turnProbability",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_n7475726e5f70726f626162696c697479),
         nullptr,
         "read-only turn_probability",
         nullptr},
        {"turnComplete",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_n7475726e5f636f6d706c657465),
         nullptr,
         "read-only turn_complete",
         nullptr},
        {"windowStartSample",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_n77696e646f775f73746172745f73616d706c65),
         nullptr,
         "read-only window_start_sample",
         nullptr},
        {"windowEndSample",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_n77696e646f775f656e645f73616d706c65),
         nullptr,
         "read-only window_end_sample",
         nullptr},
        {"windowStartTsNs",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_n77696e646f775f73746172745f74735f6e73),
         nullptr,
         "read-only window_start_ts_ns",
         nullptr},
        {"windowEndTsNs",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_n77696e646f775f656e645f74735f6e73),
         nullptr,
         "read-only window_end_ts_ns",
         nullptr},
        {"discontinuity",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_n646973636f6e74696e75697479),
         nullptr,
         "read-only discontinuity",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e();
    if (type.tp_name == nullptr) {
        type.tp_name =
            "open_perception_kit_bridge."
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::TurnDetectionT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::TurnDetectionT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_proxy(
    const open_perception_kit::metadata::TurnDetectionT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::TurnDetectionT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73_n736368656d615f6d616a6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::TurnDetectionsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73_n736368656d615f6d696e6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::TurnDetectionsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73_n6c61796572(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::TurnDetectionsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::TurnDetectionsT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73_n646574656374696f6e73(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::TurnDetectionsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<open_perception_kit::metadata::TurnDetectionT>>>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e732e646574656374696f6e732e766563746f72(),
        std::addressof(value->detections),
        proxy_anchor<open_perception_kit::metadata::TurnDetectionsT>(self));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73_n736368656d615f6d616a6f72),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73_n736368656d615f6d696e6f72),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73_n6c61796572),
         nullptr,
         "read-only layer",
         nullptr},
        {"detections",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73_n646574656374696f6e73),
         nullptr,
         "read-only detections",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e44657465"
                       "6374696f6e73";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::TurnDetectionsT>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<open_perception_kit::metadata::TurnDetectionsT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73_proxy(
    const open_perception_kit::metadata::TurnDetectionsT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::TurnDetectionsT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_n6f626a656374(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::VoiceActivityDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->object.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::VoiceActivityDetectionT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_n7370656563685f70726f626162696c697479(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::VoiceActivityDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return PyFloat_FromDouble(static_cast<double>(value->speech_probability));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_n7370656563685f616374697665(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::VoiceActivityDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (value->speech_active) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_n77696e646f775f73746172745f73616d706c65(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::VoiceActivityDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->window_start_sample);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_n77696e646f775f656e645f73616d706c65(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::VoiceActivityDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->window_end_sample);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_n77696e646f775f73746172745f74735f6e73(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::VoiceActivityDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->window_start_ts_ns) {
        Py_RETURN_NONE;
    }
    return py_long_from_unsigned(*(value->window_start_ts_ns));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_n77696e646f775f656e645f74735f6e73(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::VoiceActivityDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (!value->window_end_ts_ns) {
        Py_RETURN_NONE;
    }
    return py_long_from_unsigned(*(value->window_end_ts_ns));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_n646973636f6e74696e75697479(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::VoiceActivityDetectionT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    if (value->discontinuity) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e() {
    static PyGetSetDef definitions[] = {
        {"object",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_n6f626a656374),
         nullptr,
         "read-only object",
         nullptr},
        {"speechProbability",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_n7370656563685f70726f626162696c697479),
         nullptr,
         "read-only speech_probability",
         nullptr},
        {"speechActive",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_n7370656563685f616374697665),
         nullptr,
         "read-only speech_active",
         nullptr},
        {"windowStartSample",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_n77696e646f775f73746172745f73616d706c65),
         nullptr,
         "read-only window_start_sample",
         nullptr},
        {"windowEndSample",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_n77696e646f775f656e645f73616d706c65),
         nullptr,
         "read-only window_end_sample",
         nullptr},
        {"windowStartTsNs",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_n77696e646f775f73746172745f74735f6e73),
         nullptr,
         "read-only window_start_ts_ns",
         nullptr},
        {"windowEndTsNs",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_n77696e646f775f656e645f74735f6e73),
         nullptr,
         "read-only window_end_ts_ns",
         nullptr},
        {"discontinuity",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_n646973636f6e74696e75697479),
         nullptr,
         "read-only discontinuity",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f696365416374"
                       "6976697479446574656374696f6e";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::VoiceActivityDetectionT>);
        type.tp_itemsize = 0;
        type.tp_dealloc =
            native_proxy_dealloc<open_perception_kit::metadata::VoiceActivityDetectionT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_proxy(
    const open_perception_kit::metadata::VoiceActivityDetectionT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::VoiceActivityDetectionT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e(),
        value,
        std::move(anchor));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73_n736368656d615f6d616a6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::VoiceActivityDetectionsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_major);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73_n736368656d615f6d696e6f72(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::VoiceActivityDetectionsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return py_long_from_unsigned(value->schema_minor);
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73_n6c61796572(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::VoiceActivityDetectionsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    const auto *nested = value->layer.get();
    if (nested == nullptr) {
        Py_RETURN_NONE;
    }
    return make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_proxy(
        nested, proxy_anchor<open_perception_kit::metadata::VoiceActivityDetectionsT>(self));
}

PyObject *
get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73_n646574656374696f6e73(
    PyObject *self, void *) {
    const auto *proxy = reinterpret_cast<
        const native_proxy_object<open_perception_kit::metadata::VoiceActivityDetectionsT> *>(self);
    const auto *value = proxy->value;
    if (value == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "open_perception_kit payload proxy is invalid");
        return nullptr;
    }
    return make_native_vector_proxy<
        std::vector<std::unique_ptr<open_perception_kit::metadata::VoiceActivityDetectionT>>>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e732e646574656374696f6e732e766563746f72(),
        std::addressof(value->detections),
        proxy_anchor<open_perception_kit::metadata::VoiceActivityDetectionsT>(self));
}

PyGetSetDef *
getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73() {
    static PyGetSetDef definitions[] = {
        {"schemaMajor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73_n736368656d615f6d616a6f72),
         nullptr,
         "read-only schema_major",
         nullptr},
        {"schemaMinor",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73_n736368656d615f6d696e6f72),
         nullptr,
         "read-only schema_minor",
         nullptr},
        {"layer",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73_n6c61796572),
         nullptr,
         "read-only layer",
         nullptr},
        {"detections",
         reinterpret_cast<getter>(
             get_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73_n646574656374696f6e73),
         nullptr,
         "read-only detections",
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

bool ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73_type() {
    auto &type =
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73();
    if (type.tp_name == nullptr) {
        type.tp_name = "open_perception_kit_bridge."
                       "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f696365416374"
                       "6976697479446574656374696f6e73";
        type.tp_basicsize =
            sizeof(native_proxy_object<open_perception_kit::metadata::VoiceActivityDetectionsT>);
        type.tp_itemsize = 0;
        type.tp_dealloc =
            native_proxy_dealloc<open_perception_kit::metadata::VoiceActivityDetectionsT>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit read-only live payload proxy";
        type.tp_getset =
            getsets_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

PyObject *
make_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73_proxy(
    const open_perception_kit::metadata::VoiceActivityDetectionsT *value,
    std::shared_ptr<const native_proxy_anchor> anchor) {
    return make_native_proxy<open_perception_kit::metadata::VoiceActivityDetectionsT>(
        pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73(),
        value,
        std::move(anchor));
}

bool ensure_known_proxy_types() {
    if (!ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e732e646574656374696f6e732e766563746f72_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374732e636f6e74616374732e766563746f72_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e2e63616e646964617465732e766563746f72_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e732e636c617373696669636174696f6e732e766563746f72_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d6170446174612e706978656c732e766563746f72_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74732e6d6561737572656d656e74732e766563746f72_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e672e76616c7565732e766563746f72_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67732e656d62656464696e67732e766563746f72_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b732e747261636b732e766563746f72_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e74732e74696d696e67732e766563746f72_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365732e70726573656e6365732e766563746f72_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b732e6d61736b732e766563746f72_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b54726163652e706f696e74732e766563746f72_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365732e7472616365732e766563746f72_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e732e646574656374696f6e732e766563746f72_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e732e646574656374696f6e732e766563746f72_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374526573756c74_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e43616e646964617465_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e50726f6475636572496e666f_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506f696e743266_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f4672616d65436f6e74657874_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a65416e676c6573_type() || !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74_type() || !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473_type() || !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e456d62656464696e675370616365_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d6552617465_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365526573756c74_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e_type() ||
        !ensure_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73_type()) {
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
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6"
            "e")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e73(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f78446574656374696f6e7"
            "3")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374526573756c74(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e746163745"
            "26573756c74")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e74616374(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637"
            "4")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e7461637473(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e43616d657261436f6e746163747"
            "3")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e43616e646964617465(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6"
            "e43616e646964617465")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6"
            "e")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6e73(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e436c617373696669636174696f6"
            "e73")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a6563744d657461")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e50726f6475636572496e666f(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e50726f6475636572496e666"
            "f")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4c61796572496e666f")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496d6167655370616365")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e426f756e64696e67426f78")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506f696e743266(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506f696e743266")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4269746d617044617461")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f4672616d65436f6e74657874(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566964656f4672616d65436f6e7"
            "4657874")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e74657874(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e417564696f4672616d65436f6e7"
            "4657874")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e74657874(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d65436f6e7465787"
            "4")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a65416e676c6573(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a65416e676c6573")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e74(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656"
            "e74")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656e7473(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e47617a654d6561737572656d656"
            "e7473")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656"
            "e746174696f6e4d61736b")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656e746174696f6e4d61736b73(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e496e7374616e63655365676d656"
            "e746174696f6e4d61736b73")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e456d62656464696e675370616365(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e456d62656464696e67537061636"
            "5")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e67(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696"
            "e67")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696e6773(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374456d62656464696"
            "e6773")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b73(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4f626a656374547261636b7"
            "3")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53636f706554696d696e67")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d6552617465(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e4672616d6552617465")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d6561737572656d656e7473(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572666f726d616e63654d656"
            "1737572656d656e7473")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365526573756c74(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636"
            "5526573756c74")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e6365(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636"
            "5")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636573(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e506572736f6e50726573656e636"
            "573")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d6"
            "1736b")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d61736b73(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5365676d656e746174696f6e4d6"
            "1736b73")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656"
            "e746174696f6e4d61736b")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656e746174696f6e4d61736b73(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e53656d616e7469635365676d656"
            "e746174696f6e4d61736b73")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b5472616365")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e547261636b547261636573")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6"
            "e")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e73(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e5475726e446574656374696f6e7"
            "3")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f69636541637469766974794"
            "46574656374696f6e")) {
        return false;
    }
    if (!add_python_type(
            python_module,
            pytype_n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f6963654163746976697479446574656374696f6e73(),
            "n6f70656e5f70657263657074696f6e5f6b69742e6d657461646174612e566f69636541637469766974794"
            "46574656374696f6e73")) {
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
        type.tp_name = "open_perception_kit_bridge.Envelope";
        type.tp_basicsize = sizeof(live_envelope_object);
        type.tp_itemsize = 0;
        type.tp_dealloc = live_envelope_dealloc;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "open_perception_kit live C++ envelope wrapper";
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
        "open_perception_kit_bridge",
        "open_perception_kit embedded Python bridge for live C++ envelopes",
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
        PyErr_NewException("open_perception_kit_bridge.BridgeError", PyExc_RuntimeError, nullptr);
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
} // namespace open_perception_kit::python_bridge

extern "C" PyObject *PyInit_open_perception_kit_bridge() {
    return open_perception_kit::python_bridge::init_module_impl();
}

namespace open_perception_kit::python_bridge {

void append_inittab() {
    if (PyImport_AppendInittab("open_perception_kit_bridge", &PyInit_open_perception_kit_bridge) !=
        0) {
        throw std::runtime_error(
            "failed to register open_perception_kit_bridge Python bridge module");
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
        throw std::runtime_error(
            "failed to create open_perception_kit_bridge live envelope wrapper");
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

} // namespace open_perception_kit::python_bridge
