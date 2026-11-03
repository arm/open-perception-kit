# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Literal C++ SDK source. Rendering and compiler execution live in cpp_sdk_codegen.

The envelope stays together here because its locking, ownership and lazy decoding
share invariants. Double braces are intentional: the existing renderer unescapes
them only after inserting schema-specific fragments.
"""

from ..wire_schema import ENVELOPE_SCHEMA_TEMPLATE

HEADER_TEMPLATE = """// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include \"flatbuffers/flatbuffers.h\"
#include \"fb/internal/wire_envelope_generated.h\"
{includes}

namespace __SDK_NAME__ {{

inline constexpr std::string_view __SDK_NAME_UPPER___VERSION = "__SDK_VERSION__";
inline constexpr std::string_view __SDK_NAME_UPPER___NAME = "__SDK_NAME__";
inline constexpr std::string_view __SDK_NAME_UPPER___SCHEMA_SET_SHA256 = "__SCHEMA_SET_SHA256__";
inline constexpr std::string_view __SDK_NAME_UPPER___FLATBUFFERS_VERSION_REQUIREMENT =
    "__FLATBUFFERS_VERSION_REQUIREMENT__";

namespace detail {{

using id_t = std::uint64_t;

template <typename T>
struct native_traits;

{trait_specializations}

struct encoded_payload {{
    std::vector<std::uint8_t> blob;
}};

using payload_data = std::variant<
    encoded_payload{payload_variant_types}
>;

struct payload_entry {{
    payload_entry(id_t payload_id, payload_data payload_data_in)
        : id(payload_id),
          data(std::move(payload_data_in)) {{
    }}

    id_t id;
    payload_data data;
}};

}} // namespace detail

namespace python_bridge {{
struct external_key_access;
}} // namespace python_bridge

namespace container {{

enum class producer_identity_status {{
    name_mismatch,
    exact_match,
    compatible_match,
    version_mismatch,
}};

class external_payload_ref;
class envelope;

inline constexpr __SDK_NAME__::detail::id_t external_key_min = {external_key_min}ULL;
inline constexpr __SDK_NAME__::detail::id_t external_key_mask = external_key_min - 1ULL;
inline constexpr __SDK_NAME__::detail::id_t external_hash_offset = 14695981039346656037ULL;
inline constexpr __SDK_NAME__::detail::id_t external_hash_prime = 1099511628211ULL;

class external_key_t {{
public:
    external_key_t(const external_key_t&) noexcept = default;
    external_key_t(external_key_t&&) noexcept = default;
    external_key_t& operator=(const external_key_t&) noexcept = default;
    external_key_t& operator=(external_key_t&&) noexcept = default;

    [[nodiscard]] constexpr __SDK_NAME__::detail::id_t value() const noexcept {{
        return value_;
    }}

    [[nodiscard]] friend constexpr bool operator==(
        external_key_t lhs,
        external_key_t rhs
    ) noexcept = default;

private:
    friend class external_payload_ref;
    friend class envelope;
    friend struct __SDK_NAME__::python_bridge::external_key_access;
    friend constexpr external_key_t external_key(std::string_view key) noexcept;
    friend constexpr bool is_external_key(external_key_t key) noexcept;

    constexpr explicit external_key_t(__SDK_NAME__::detail::id_t value) noexcept
        : value_(value) {{
    }}

    __SDK_NAME__::detail::id_t value_;
}};

[[nodiscard]] constexpr bool is_external_key(external_key_t key) noexcept {{
    return key.value_ >= external_key_min;
}}

[[nodiscard]] constexpr external_key_t external_key(std::string_view key) noexcept {{
    auto hash = external_hash_offset;
    for (const char byte : key) {{
        hash ^= static_cast<__SDK_NAME__::detail::id_t>(static_cast<unsigned char>(byte));
        hash *= external_hash_prime;
    }}
    return external_key_t((hash & external_key_mask) | external_key_min);
}}

template <typename T>
concept native_payload =
    requires {{
        typename __SDK_NAME__::detail::native_traits<std::remove_cvref_t<T>>::native_type;
        typename __SDK_NAME__::detail::native_traits<std::remove_cvref_t<T>>::table_type;
        __SDK_NAME__::detail::native_traits<std::remove_cvref_t<T>>::id;
    }};

template <native_payload T>
class payload_ref {{
public:
    using native_type = std::remove_cvref_t<T>;

    payload_ref(const payload_ref&) noexcept = default;
    payload_ref(payload_ref&&) noexcept = default;
    payload_ref& operator=(const payload_ref&) noexcept = default;
    payload_ref& operator=(payload_ref&&) noexcept = default;

    [[nodiscard]] const native_type& value() const noexcept {{
        return *native_;
    }}

    [[nodiscard]] const native_type& operator*() const noexcept {{
        return value();
    }}

    [[nodiscard]] const native_type* operator->() const noexcept {{
        return native_;
    }}

    [[nodiscard]] const native_type* get() const noexcept {{
        return native_;
    }}

private:
    friend class envelope;

    payload_ref(
        std::shared_ptr<const __SDK_NAME__::detail::payload_entry> entry,
        const native_type* native
    ) noexcept
        : entry_(std::move(entry)),
          native_(native) {{
    }}

    std::shared_ptr<const __SDK_NAME__::detail::payload_entry> entry_;
    const native_type* native_;
}};

class external_payload_ref {{
public:
    external_payload_ref(const external_payload_ref&) noexcept = default;
    external_payload_ref(external_payload_ref&&) noexcept = default;
    external_payload_ref& operator=(const external_payload_ref&) noexcept = default;
    external_payload_ref& operator=(external_payload_ref&&) noexcept = default;

    [[nodiscard]] external_key_t key() const noexcept {{
        return key_;
    }}

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept {{
        return std::span<const std::uint8_t>(blob_->data(), blob_->size());
    }}

private:
    friend class envelope;

    external_payload_ref(
        std::shared_ptr<const __SDK_NAME__::detail::payload_entry> entry,
        const std::vector<std::uint8_t>* blob
    ) noexcept
        : entry_(std::move(entry)),
          blob_(blob),
          key_(entry_->id) {{
    }}

    std::shared_ptr<const __SDK_NAME__::detail::payload_entry> entry_;
    const std::vector<std::uint8_t>* blob_;
    external_key_t key_;
}};

class envelope {{
public:
    envelope() = default;
    ~envelope() = default;

    envelope(const envelope& other) {{
        std::scoped_lock lock(other.mutex());
        payloads_ = clone_payloads(other.payloads_);
        valid_ = other.valid_;
        error_ = other.error_;
        producer_sdk_name_ = other.producer_sdk_name_;
        producer_sdk_version_ = other.producer_sdk_version_;
        producer_schema_set_sha256_ = other.producer_schema_set_sha256_;
    }}

    envelope(envelope&& other) noexcept
        : payloads_(std::move(other.payloads_)),
          valid_(other.valid_),
          error_(std::move(other.error_)),
          producer_sdk_name_(std::move(other.producer_sdk_name_)),
          producer_sdk_version_(std::move(other.producer_sdk_version_)),
          producer_schema_set_sha256_(std::move(other.producer_schema_set_sha256_)) {{
    }}

    envelope& operator=(const envelope& other) {{
        if (this == &other) {{
            return *this;
        }}

        std::scoped_lock lock(mutex(), other.mutex());
        payloads_ = clone_payloads(other.payloads_);
        valid_ = other.valid_;
        error_ = other.error_;
        producer_sdk_name_ = other.producer_sdk_name_;
        producer_sdk_version_ = other.producer_sdk_version_;
        producer_schema_set_sha256_ = other.producer_schema_set_sha256_;
        return *this;
    }}

    envelope& operator=(envelope&& other) noexcept {{
        if (this == &other) {{
            return *this;
        }}

        payloads_.swap(other.payloads_);
        std::swap(valid_, other.valid_);
        error_.swap(other.error_);
        producer_sdk_name_.swap(other.producer_sdk_name_);
        producer_sdk_version_.swap(other.producer_sdk_version_);
        producer_schema_set_sha256_.swap(other.producer_schema_set_sha256_);
        return *this;
    }}

    explicit envelope(std::span<const std::uint8_t> packet) {{
        valid_ = false;
        producer_sdk_name_.clear();
        producer_sdk_version_.clear();
        producer_schema_set_sha256_.clear();
        if (flatbuffers::Verifier verifier(packet.data(), packet.size());
            !verifier.VerifyBuffer<__SDK_NAME__::internalfb::WireEnvelope>("FLWD")) {{
            error_ = "invalid __SDK_NAME__ envelope";
            return;
        }}

        const auto* envelope = flatbuffers::GetRoot<__SDK_NAME__::internalfb::WireEnvelope>(packet.data());
        if (!envelope) {{
            error_ = "invalid __SDK_NAME__ envelope root";
            return;
        }}

        if (const auto* payloads = envelope->payloads()) {{
            payloads_.reserve(payloads->size());
            for (const auto* entry : *payloads) {{
                if (!entry) {{
                    continue;
                }}
                std::vector<std::uint8_t> blob;
                if (const auto* bytes = entry->blob()) {{
                    blob.assign(bytes->Data(), bytes->Data() + bytes->size());
                }}
                payloads_.push_back(std::make_shared<__SDK_NAME__::detail::payload_entry>(
                    entry->id(),
                    __SDK_NAME__::detail::payload_data{{
                        __SDK_NAME__::detail::encoded_payload{{
                            std::move(blob)
                        }}
                    }}
                ));
            }}
        }}

        if (const auto* value = envelope->producer_sdk_name()) {{
            producer_sdk_name_ = value->str();
        }}
        if (const auto* value = envelope->producer_sdk_version()) {{
            producer_sdk_version_ = value->str();
        }}
        if (const auto* value = envelope->producer_schema_set_sha256()) {{
            producer_schema_set_sha256_ = value->str();
        }}

        valid_ = true;
    }}

    [[nodiscard]] bool valid() const noexcept {{
        return valid_;
    }}

    [[nodiscard]] const std::string& error() const noexcept {{
        return error_;
    }}

    [[nodiscard]] std::string producer_sdk_name() const {{
        std::scoped_lock lock(mutex());
        return producer_sdk_name_;
    }}

    [[nodiscard]] std::string producer_sdk_version() const {{
        std::scoped_lock lock(mutex());
        return producer_sdk_version_;
    }}

    [[nodiscard]] std::string producer_schema_set_sha256() const {{
        std::scoped_lock lock(mutex());
        return producer_schema_set_sha256_;
    }}

    [[nodiscard]] producer_identity_status producer_identity() const {{
        std::scoped_lock lock(mutex());
        if (producer_sdk_name_ != __SDK_NAME_UPPER___NAME) {{
            return producer_identity_status::name_mismatch;
        }}
        if (!valid_semantic_version(producer_sdk_version_)) {{
            return producer_identity_status::version_mismatch;
        }}
        if (producer_sdk_version_ == __SDK_NAME_UPPER___VERSION) {{
            return producer_identity_status::exact_match;
        }}
        const auto producer_major = std::string_view(producer_sdk_version_).substr(
            0, producer_sdk_version_.find('.'));
        const auto sdk_major = __SDK_NAME_UPPER___VERSION.substr(
            0, __SDK_NAME_UPPER___VERSION.find('.'));
        return producer_major == sdk_major
            ? producer_identity_status::compatible_match
            : producer_identity_status::version_mismatch;
    }}

    [[nodiscard]] bool empty() const noexcept {{
        return size() == 0;
    }}

    [[nodiscard]] std::size_t size() const noexcept {{
        std::scoped_lock lock(mutex());
        return valid_ ? payloads_.size() : 0;
    }}

    void reserve(std::size_t payload_count) {{
        std::scoped_lock lock(mutex());
        if (!valid_) {{
            throw std::logic_error("cannot reserve invalid __SDK_NAME__ envelope");
        }}
        payloads_.reserve(payload_count);
    }}

    template <native_payload T>
    void add(T&& value) {{
        using native_type = std::remove_cvref_t<T>;
        using traits = __SDK_NAME__::detail::native_traits<native_type>;

        std::scoped_lock lock(mutex());
        if (!valid_) {{
            throw std::logic_error("cannot add to invalid __SDK_NAME__ envelope");
        }}
        payloads_.push_back(std::make_shared<__SDK_NAME__::detail::payload_entry>(
            traits::id,
            __SDK_NAME__::detail::payload_data{{
                native_type(std::forward<T>(value))
            }}
        ));
    }}

    void add(external_key_t key, std::span<const std::uint8_t> blob) {{
        std::vector<std::uint8_t> owned(blob.begin(), blob.end());
        add_keyed_blob_entry(key.value(), std::move(owned));
    }}

    void add(external_key_t key, std::vector<std::uint8_t> blob) {{
        add_keyed_blob_entry(key.value(), std::move(blob));
    }}

    template <native_payload T>
    [[nodiscard]] std::size_t count() const {{
        using native_type = std::remove_cvref_t<T>;
        using traits = __SDK_NAME__::detail::native_traits<native_type>;
        std::scoped_lock lock(mutex());
        if (!valid_) {{
            return 0;
        }}

        std::size_t matches = 0;
        for (const auto& entry : payloads_) {{
            if (entry && entry->id == traits::id && ensure_native<native_type>(*entry)) {{
                ++matches;
            }}
        }}
        return matches;
    }}

    template <native_payload T>
    [[nodiscard]] bool contains() const {{
        return count<T>() > 0;
    }}

    [[nodiscard]] std::size_t count(external_key_t key) const {{
        std::scoped_lock lock(mutex());
        if (!valid_) {{
            return 0;
        }}

        std::size_t matches = 0;
        for (const auto& entry : payloads_) {{
            if (entry && entry->id == key.value() && external_blob(*entry)) {{
                ++matches;
            }}
        }}
        return matches;
    }}

    [[nodiscard]] bool contains(external_key_t key) const {{
        return count(key) > 0;
    }}

    template <native_payload T>
    [[nodiscard]] std::optional<payload_ref<std::remove_cvref_t<T>>> get(std::size_t index = 0) const {{
        using native_type = std::remove_cvref_t<T>;
        std::scoped_lock lock(mutex());
        return ref_at<native_type>(index);
    }}

    [[nodiscard]] std::optional<external_payload_ref> get(
        external_key_t key,
        std::size_t index = 0
    ) const {{
        std::scoped_lock lock(mutex());
        return external_ref_at(key, index);
    }}

    template <native_payload T, typename Fn>
    void for_each(Fn&& fn) const {{
        using native_type = std::remove_cvref_t<T>;
        using traits = __SDK_NAME__::detail::native_traits<native_type>;
        std::vector<payload_ref<native_type>> refs;

        {{
            std::scoped_lock lock(mutex());
            if (valid_) {{
                refs.reserve(payloads_.size());
                for (const auto& entry : payloads_) {{
                    if (!entry || entry->id != traits::id) {{
                        continue;
                    }}
                    const auto* native = ensure_native<native_type>(*entry);
                    if (native) {{
                        refs.push_back(payload_ref<native_type>(entry, native));
                    }}
                }}
            }}
        }}

        for (const auto& ref : refs) {{
            fn(ref.value());
        }}
    }}

    template <typename Fn>
    void for_each(external_key_t key, Fn&& fn) const {{
        std::vector<external_payload_ref> refs;

        {{
            std::scoped_lock lock(mutex());
            if (valid_) {{
                refs.reserve(payloads_.size());
                for (const auto& entry : payloads_) {{
                    if (!entry || entry->id != key.value()) {{
                        continue;
                    }}
                    const auto* blob = external_blob(*entry);
                    if (blob) {{
                        refs.push_back(external_payload_ref(entry, blob));
                    }}
                }}
            }}
        }}

        for (const auto& ref : refs) {{
            fn(ref.bytes());
        }}
    }}

    [[nodiscard]] flatbuffers::DetachedBuffer serialize() const {{
        std::scoped_lock lock(mutex());
        if (!valid_) {{
            throw std::logic_error("cannot serialize invalid __SDK_NAME__ envelope");
        }}
        flatbuffers::FlatBufferBuilder builder;
        std::vector<flatbuffers::Offset<__SDK_NAME__::internalfb::WirePayload>> payload_offsets;
        payload_offsets.reserve(payloads_.size());

        for (const auto& p : payloads_) {{
            if (!p) {{
                continue;
            }}
            const auto blob = make_payload_blob(builder, p->data);
            payload_offsets.push_back(__SDK_NAME__::internalfb::CreateWirePayload(builder, p->id, blob));
        }}

        const auto payloads_vec = builder.CreateVector(payload_offsets);
        const auto producer_sdk_name = builder.CreateString(__SDK_NAME_UPPER___NAME);
        const auto producer_sdk_version = builder.CreateString(__SDK_NAME_UPPER___VERSION);
        const auto producer_schema_set_sha256 =
            builder.CreateString(__SDK_NAME_UPPER___SCHEMA_SET_SHA256);
        const auto envelope = __SDK_NAME__::internalfb::CreateWireEnvelope(
            builder,
            payloads_vec,
            producer_sdk_name,
            producer_sdk_version,
            producer_schema_set_sha256
        );
        builder.Finish(envelope, "FLWD");

        return builder.Release();
    }}

private:
    using entry_ptr = std::shared_ptr<__SDK_NAME__::detail::payload_entry>;
    using entry_list = std::vector<entry_ptr>;

    [[nodiscard]] static bool valid_semantic_version(std::string_view value) {{
        std::size_t component_start = 0;
        int component_count = 0;
        for (std::size_t index = 0; index <= value.size(); ++index) {{
            if (index != value.size() && value[index] != '.') {{
                if (value[index] < '0' || value[index] > '9') {{
                    return false;
                }}
                continue;
            }}
            if (const auto length = index - component_start;
                length == 0 || (length > 1 && value[component_start] == '0')) {{
                return false;
            }}
            ++component_count;
            component_start = index + 1;
        }}
        return component_count == 3;
    }}

    void add_keyed_blob_entry(__SDK_NAME__::detail::id_t key, std::vector<std::uint8_t> blob) {{
        std::scoped_lock lock(mutex());
        if (!valid_) {{
            throw std::logic_error("cannot add to invalid __SDK_NAME__ envelope");
        }}
        payloads_.push_back(std::make_shared<__SDK_NAME__::detail::payload_entry>(
            key,
            __SDK_NAME__::detail::payload_data{{
                __SDK_NAME__::detail::encoded_payload{{
                    std::move(blob)
                }}
            }}
        ));
    }}

    template <native_payload T>
    [[nodiscard]] std::optional<payload_ref<std::remove_cvref_t<T>>> ref_at(std::size_t index) const {{
        using native_type = std::remove_cvref_t<T>;
        using traits = __SDK_NAME__::detail::native_traits<native_type>;
        std::size_t seen = 0;
        for (const auto& entry : payloads_) {{
            if (!entry || entry->id != traits::id) {{
                continue;
            }}

            const auto* native = ensure_native<native_type>(*entry);
            if (!native) {{
                continue;
            }}

            if (seen == index) {{
                return payload_ref<native_type>(entry, native);
            }}
            ++seen;
        }}

        return std::nullopt;
    }}

    [[nodiscard]] std::optional<external_payload_ref> external_ref_at(
        external_key_t key,
        std::size_t index
    ) const {{
        std::size_t seen = 0;
        for (const auto& entry : payloads_) {{
            if (!entry || entry->id != key.value()) {{
                continue;
            }}

            const auto* blob = external_blob(*entry);
            if (!blob) {{
                continue;
            }}

            if (seen == index) {{
                return external_payload_ref(entry, blob);
            }}
            ++seen;
        }}

        return std::nullopt;
    }}

    template <native_payload T>
    [[nodiscard]] const std::remove_cvref_t<T>* ensure_native(
        __SDK_NAME__::detail::payload_entry& entry
    ) const {{
        using native_type = std::remove_cvref_t<T>;
        if (const auto* native = std::get_if<native_type>(&entry.data)) {{
            return native;
        }}

        const auto* encoded = std::get_if<__SDK_NAME__::detail::encoded_payload>(&entry.data);
        if (!encoded) {{
            return nullptr;
        }}

        const auto* root = decode_blob<native_type>(encoded->blob);
        if (!root) {{
            return nullptr;
        }}

        native_type decoded{{}};
        root->UnPackTo(&decoded);
        entry.data = std::move(decoded);
        return std::get_if<native_type>(&entry.data);
    }}

    [[nodiscard]] static const std::vector<std::uint8_t>* external_blob(
        const __SDK_NAME__::detail::payload_entry& entry
    ) {{
        const auto* encoded = std::get_if<__SDK_NAME__::detail::encoded_payload>(&entry.data);
        if (!encoded) {{
            return nullptr;
        }}
        return &encoded->blob;
    }}

    [[nodiscard]] static flatbuffers::Offset<flatbuffers::Vector<std::uint8_t>> make_payload_blob(
        flatbuffers::FlatBufferBuilder& builder,
        const __SDK_NAME__::detail::payload_data& data
    ) {{
        return std::visit(
            [&]<typename Value>(const Value& value) -> flatbuffers::Offset<flatbuffers::Vector<std::uint8_t>> {{
                using value_type = std::remove_cvref_t<Value>;
                if constexpr (std::is_same_v<value_type, __SDK_NAME__::detail::encoded_payload>) {{
                    return builder.CreateVector(value.blob);
                }} else {{
                    using traits = __SDK_NAME__::detail::native_traits<value_type>;

                    flatbuffers::FlatBufferBuilder payload_builder;
                    const auto offset = traits::pack(payload_builder, std::addressof(value));
                    payload_builder.Finish(offset, traits::file_identifier());

                    const auto* payload_data = payload_builder.GetBufferPointer();
                    const auto payload_size = payload_builder.GetSize();
                    return builder.CreateVector(payload_data, payload_size);
                }}
            }},
            data
        );
    }}

    template <native_payload T>
    [[nodiscard]] static const typename detail::native_traits<std::remove_cvref_t<T>>::table_type*
    decode_blob(const std::vector<std::uint8_t>& blob) {{
        using native_type = std::remove_cvref_t<T>;
        using traits = __SDK_NAME__::detail::native_traits<native_type>;
        if (flatbuffers::Verifier verifier(blob.data(), blob.size());
            !verifier.VerifyBuffer<typename traits::table_type>(traits::file_identifier())) {{
            return {{}};
        }}

        return flatbuffers::GetRoot<typename traits::table_type>(blob.data());
    }}

    [[nodiscard]] static entry_list clone_payloads(const entry_list& payloads) {{
        entry_list cloned;
        cloned.reserve(payloads.size());
        for (const auto& payload : payloads) {{
            if (payload) {{
                cloned.push_back(std::make_shared<__SDK_NAME__::detail::payload_entry>(*payload));
            }}
        }}
        return cloned;
    }}

    [[nodiscard]] std::mutex& mutex() const noexcept {{
        return mutex_;
    }}

    mutable std::mutex mutex_;
    mutable entry_list payloads_;
    bool valid_ = true;
    std::string error_;
    std::string producer_sdk_name_{{__SDK_NAME_UPPER___NAME}};
    std::string producer_sdk_version_{{__SDK_NAME_UPPER___VERSION}};
    std::string producer_schema_set_sha256_{{__SDK_NAME_UPPER___SCHEMA_SET_SHA256}};
}};

}} // namespace container
}} // namespace __SDK_NAME__
"""
