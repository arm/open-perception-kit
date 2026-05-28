/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

// attribute_set_extended.hpp
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace pek {

struct AttributeMap; // forward

// ---------------- AttributeValue ----------------

/**
 * @brief Variant-based dynamic attribute value.
 *
 * Supports scalar values, arrays, nested objects, and null.
 */
class AttributeValue {
  public:
    // forward-declare shared ptr to allow recursive structure
    /// Shared nested object pointer type.
    using MapPtr = std::shared_ptr<AttributeMap>;
    /// Array value type.
    using Array = std::vector<AttributeValue>;
    /// Internal storage variant type.
    using Storage =
        std::variant<std::int64_t, double, bool, std::string, Array, MapPtr, std::nullptr_t>;

    // ctors
    /** @brief Constructs a null value. */
    AttributeValue() : value_(std::nullptr_t{}) {}
    /** @brief Constructs a null value. */
    AttributeValue(std::nullptr_t) : value_(std::nullptr_t{}) {}
    /** @brief Constructs an integer value. */
    AttributeValue(std::int64_t v) : value_(v) {}
    /** @brief Constructs an integer value. */
    AttributeValue(int v) : value_(static_cast<std::int64_t>(v)) {}
    /** @brief Constructs a floating-point value. */
    AttributeValue(double v) : value_(v) {}
    /** @brief Constructs a floating-point value. */
    AttributeValue(float v) : value_(static_cast<double>(v)) {}
    /** @brief Constructs a boolean value. */
    AttributeValue(bool v) : value_(v) {}
    /** @brief Constructs a string value. */
    AttributeValue(std::string v) : value_(std::move(v)) {}
    /** @brief Constructs a string value from C string. */
    AttributeValue(const char *v) : value_(std::string{v}) {}
    /** @brief Constructs an array value. */
    AttributeValue(const Array &a) : value_(a) {}
    /** @brief Constructs an array value. */
    AttributeValue(Array &&a) : value_(std::move(a)) {}
    /** @brief Constructs an object value. */
    AttributeValue(const MapPtr &m) : value_(m) {}
    /** @brief Constructs an object value. */
    AttributeValue(MapPtr &&m) : value_(std::move(m)) {}

    // null
    /**
     * @brief Factory for explicit null value.
     */
    static AttributeValue make_null() {
        return AttributeValue(std::nullptr_t{});
    }

    // type predicates
    /** @brief Returns true when value is int64. */
    bool isInt() const {
        return std::holds_alternative<std::int64_t>(value_);
    }
    /** @brief Returns true when value is double. */
    bool isDouble() const {
        return std::holds_alternative<double>(value_);
    }
    /** @brief Returns true when value is bool. */
    bool isBool() const {
        return std::holds_alternative<bool>(value_);
    }
    /** @brief Returns true when value is string. */
    bool isString() const {
        return std::holds_alternative<std::string>(value_);
    }
    /** @brief Returns true when value is array. */
    bool isArray() const {
        return std::holds_alternative<Array>(value_);
    }
    /** @brief Returns true when value is non-null object pointer. */
    bool isObject() const {
        return std::holds_alternative<MapPtr>(value_) && std::get<MapPtr>(value_) != nullptr;
    }
    /** @brief Returns true when value is null. */
    bool isNull() const {
        return std::holds_alternative<std::nullptr_t>(value_);
    }

    // accessors (throws std::bad_variant_access if wrong type)
    /** @brief Returns value as int64. */
    std::int64_t asInt() const {
        return std::get<std::int64_t>(value_);
    }
    /** @brief Returns value as double. */
    double asDouble() const {
        return std::get<double>(value_);
    }
    /** @brief Returns value as float. */
    float asFloat() const {
        return static_cast<float>(std::get<double>(value_));
    }
    /** @brief Returns value as bool. */
    bool asBool() const {
        return std::get<bool>(value_);
    }
    /** @brief Returns value as immutable string reference. */
    const std::string &asString() const {
        return std::get<std::string>(value_);
    }
    /** @brief Returns value as immutable array reference. */
    const Array &asArray() const {
        return std::get<Array>(value_);
    }
    /** @brief Returns value as mutable array reference. */
    Array &asArray() {
        return std::get<Array>(value_);
    }
    /** @brief Returns value as immutable object pointer reference. */
    const MapPtr &asMapPtr() const {
        return std::get<MapPtr>(value_);
    }
    /** @brief Returns value as mutable object pointer reference. */
    MapPtr &asMapPtr() {
        return std::get<MapPtr>(value_);
    }

  private:
    Storage value_;

    friend struct nlohmann::adl_serializer<pek::AttributeValue>;
};

// ---------------- AttributeMap ----------------

/**
 * @brief Attribute access/validation error.
 */
struct AttributeError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/**
 * @brief String-keyed map of AttributeValue values.
 */
struct AttributeMap {

    using Map = std::map<std::string, AttributeValue>;

    /**
     * @brief Inserts/returns attribute by key.
     */
    AttributeValue &operator[](std::string key) {
        return values[std::move(key)];
    }

    /**
     * @brief Returns attribute by key.
     * @throws std::out_of_range if key is missing.
     */
    const AttributeValue &at(const std::string &key) const {
        return values.at(key);
    }

    /**
     * @brief Returns true if key exists.
     */
    bool contains(const std::string &key) const {
        return values.find(key) != values.end();
    }

    /**
     * @brief Removes all attributes.
     */
    void clear() {
        values.clear();
    }

    // typed setters (convenience)
    /** @brief Sets integer value. */
    AttributeMap &set(std::string key, std::int64_t v) {
        values[std::move(key)] = v;
        return *this;
    }
    /** @brief Sets double value. */
    AttributeMap &set(std::string key, double v) {
        values[std::move(key)] = v;
        return *this;
    }
    /** @brief Sets bool value. */
    AttributeMap &set(std::string key, bool v) {
        values[std::move(key)] = v;
        return *this;
    }
    /** @brief Sets string value. */
    AttributeMap &set(std::string key, std::string v) {
        values[std::move(key)] = std::move(v);
        return *this;
    }
    /** @brief Sets string value from C string. */
    AttributeMap &set(std::string key, const char *v) {
        values[std::move(key)] = std::string{v};
        return *this;
    }
    /** @brief Sets array value. */
    AttributeMap &setArray(std::string key, AttributeValue::Array a) {
        values[std::move(key)] = std::move(a);
        return *this;
    }
    /** @brief Sets object value. */
    AttributeMap &setObject(std::string key, std::shared_ptr<AttributeMap> m) {
        values[std::move(key)] = std::move(m);
        return *this;
    }
    /** @brief Sets null value. */
    AttributeMap &setNull(std::string key) {
        values[std::move(key)] = AttributeValue::make_null();
        return *this;
    }

    // typed getters
    /** @brief Returns integer value, throws on missing key/type mismatch. */
    int64_t getInt(const std::string &key) const {
        return require(key).asInt();
    }
    /** @brief Returns float value, throws on missing key/type mismatch. */
    float getFloat(const std::string &key) const {
        return require(key).asFloat();
    }
    /** @brief Returns double value, throws on missing key/type mismatch. */
    double getDouble(const std::string &key) const {
        return require(key).asDouble();
    }
    /** @brief Returns bool value, throws on missing key/type mismatch. */
    bool getBool(const std::string &key) const {
        return require(key).asBool();
    }
    /** @brief Returns string value, throws on missing key/type mismatch. */
    const std::string &getString(const std::string &key) const {
        return require(key).asString();
    }
    /** @brief Returns array value, throws on missing key/type mismatch. */
    const AttributeValue::Array &getArray(const std::string &key) const {
        return require(key).asArray();
    }

    /**
     * @brief Returns integer value for @p key, or @p defaultValue when key is missing.
     *
     * Returns default when key is missing or the stored type does not match.
     */
    int64_t getIntOrDefault(const std::string &key, int64_t defaultValue) const {
        try {
            return getInt(key);
        } catch (const AttributeError &error) {
            return defaultValue;
        } catch (const std::bad_variant_access &error) {
            return defaultValue;
        }
    }
    /**
     * @brief Returns float value for @p key, or @p defaultValue when key is missing.
     *
     * Returns default when key is missing or the stored type does not match.
     */
    float getFloatOrDefault(const std::string &key, float defaultValue) const {
        try {
            return getFloat(key);
        } catch (const AttributeError &error) {
            return defaultValue;
        } catch (const std::bad_variant_access &error) {
            return defaultValue;
        }
    }
    /**
     * @brief Returns double value for @p key, or @p defaultValue when key is missing.
     *
     * Returns default when key is missing or the stored type does not match.
     */
    double getDoubleOrDefault(const std::string &key, double defaultValue) const {
        try {
            return getDouble(key);
        } catch (const AttributeError &error) {
            return defaultValue;
        } catch (const std::bad_variant_access &error) {
            return defaultValue;
        }
    }
    /**
     * @brief Returns bool value for @p key, or @p defaultValue when key is missing.
     *
     * Returns default when key is missing or the stored type does not match.
     */
    bool getBoolOrDefault(const std::string &key, bool defaultValue) const {
        try {
            return getBool(key);
        } catch (const AttributeError &error) {
            return defaultValue;
        } catch (const std::bad_variant_access &error) {
            return defaultValue;
        }
    }
    /**
     * @brief Returns string value for @p key, or @p defaultValue when key is missing.
     *
     * Returns default when key is missing or the stored type does not match.
     */
    const std::string &getStringOrDefault(const std::string &key,
                                          const std::string &defaultValue) const {
        try {
            return getString(key);
        } catch (const AttributeError &error) {
            return defaultValue;
        } catch (const std::bad_variant_access &error) {
            return defaultValue;
        }
    }

    /**
     * @brief Returns object value for @p key, or nullptr when key is missing/non-object.
     */
    std::shared_ptr<AttributeMap> getObjectOrNull(const std::string &key) const {
        try {
            return getObject(key);
        } catch (const AttributeError &error) {
            return nullptr;
        }
    }

    /**
     * @brief Backwards-compatible alias for getObjectOrNull().
     */
    [[deprecated("Use getObjectOrNull")]] std::shared_ptr<AttributeMap>
    getObjectOrNUll(const std::string &key) const {
        return getObjectOrNull(key);
    }

    /**
     * @brief Returns object value for key.
     * @throws AttributeError when key is missing or value is not an object.
     */
    std::shared_ptr<AttributeMap> getObject(const std::string &key) const {
        const auto &v = require(key);
        if (!v.isObject())
            throw AttributeError("AttributeMap: value is not object: " + key);
        return v.asMapPtr();
    }

    /** @brief Returns immutable access to raw backing map. */
    const Map &raw() const {
        return values;
    }
    /** @brief Returns mutable access to raw backing map. */
    Map &raw() {
        return values;
    }

    /**
     * @brief Returns a deep copy of the full nested attribute map.
     */
    AttributeMap cloneDeep() const;

  private:
    const AttributeValue &require(const std::string &key) const {
        auto it = values.find(key);
        if (it == values.end()) {
            throw AttributeError("AttributeMap: missing key [" + key + "]");
        }
        return it->second;
    }

    Map values;
};

// --- clone

/**
 * @brief Deep-clones one AttributeValue.
 */
inline AttributeValue cloneAttributeValue(const AttributeValue &v); // fwd

/**
 * @brief Deep-clones one AttributeMap.
 */
inline AttributeMap cloneAttributeMap(const AttributeMap &src) {
    AttributeMap dst;
    for (const auto &kv : src.raw()) {
        dst.raw().emplace(kv.first, cloneAttributeValue(kv.second));
    }
    return dst;
}

inline AttributeValue cloneAttributeValue(const AttributeValue &v) {
    using Array = AttributeValue::Array;

    if (v.isNull()) {
        return AttributeValue::make_null();
    }
    if (v.isInt()) {
        return AttributeValue(v.asInt());
    }
    if (v.isDouble()) {
        return AttributeValue(v.asDouble());
    }
    if (v.isBool()) {
        return AttributeValue(v.asBool());
    }
    if (v.isString()) {
        return AttributeValue(v.asString());
    }
    if (v.isArray()) {
        const auto &srcArr = v.asArray();
        Array dstArr;
        dstArr.reserve(srcArr.size());
        for (const auto &elem : srcArr) {
            dstArr.emplace_back(cloneAttributeValue(elem));
        }
        return AttributeValue(std::move(dstArr));
    }
    if (v.isObject()) {
        const auto &mp = v.asMapPtr();
        if (!mp) {
            // preserve "null" object as null, or make it empty object if you prefer
            return AttributeValue::make_null();
        }
        auto clonedMap = std::make_shared<AttributeMap>(cloneAttributeMap(*mp));
        return AttributeValue(std::move(clonedMap));
    }

    // Should be unreachable with current Storage types
    return AttributeValue::make_null();
}

inline AttributeMap AttributeMap::cloneDeep() const {
    return cloneAttributeMap(*this);
}

} // namespace pek

// ---------------- nlohmann::json adapters ----------------

namespace nlohmann {

template <> struct adl_serializer<pek::AttributeValue> {
    static void to_json(json &j, const pek::AttributeValue &v) {
        if (v.isNull()) {
            j = nullptr;
            return;
        }
        if (v.isInt()) {
            j = v.asInt();
            return;
        }
        if (v.isDouble()) {
            j = v.asDouble();
            return;
        }
        if (v.isBool()) {
            j = v.asBool();
            return;
        }
        if (v.isString()) {
            j = v.asString();
            return;
        }
        if (v.isArray()) {
            j = json::array();
            for (const auto &elem : v.asArray()) {
                j.push_back(elem);
            }
            return;
        }
        if (v.isObject()) {
            const auto &mp = v.asMapPtr();
            if (!mp) {
                j = json::object(); // treat null shared_ptr as empty object
                return;
            }
            j = json::object();
            for (const auto &p : mp->raw()) {
                j[p.first] = p.second;
            }
            return;
        }
        throw json::type_error::create(302, "Unsupported AttributeValue type", &j);
    }

    static void from_json(const json &j, pek::AttributeValue &v) {
        if (j.is_null()) {
            v = pek::AttributeValue::make_null();
            return;
        }
        if (j.is_boolean()) {
            v = j.get<bool>();
            return;
        }
        if (j.is_string()) {
            v = j.get<std::string>();
            return;
        }
        if (j.is_number_integer() || j.is_number_unsigned()) {
            v = j.get<std::int64_t>();
            return;
        }
        if (j.is_number_float()) {
            v = j.get<double>();
            return;
        }
        if (j.is_array()) {
            pek::AttributeValue::Array arr;
            arr.reserve(j.size());
            for (const auto &elem : j) {
                arr.emplace_back(elem.get<pek::AttributeValue>());
            }
            v = std::move(arr);
            return;
        }
        if (j.is_object()) {
            auto map_ptr = std::make_shared<pek::AttributeMap>();
            for (auto it = j.begin(); it != j.end(); ++it) {
                map_ptr->raw().emplace(it.key(), it.value().get<pek::AttributeValue>());
            }
            v = std::move(map_ptr);
            return;
        }
        throw json::type_error::create(
            302, "AttributeValue must be bool/string/integer/float/array/object/null", &j);
    }
};

template <> struct adl_serializer<pek::AttributeMap> {
    static void to_json(json &j, const pek::AttributeMap &s) {
        j = json::object();
        for (const auto &kv : s.raw()) {
            j[kv.first] = kv.second;
        }
    }
    static void from_json(const json &j, pek::AttributeMap &s) {
        if (!j.is_object()) {
            throw json::type_error::create(302, "AttributeMap must be a JSON object", &j);
        }
        s.clear();
        for (auto it = j.begin(); it != j.end(); ++it) {
            s.raw().emplace(it.key(), it.value().get<pek::AttributeValue>());
        }
    }
};

} // namespace nlohmann