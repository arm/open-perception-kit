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

namespace amp {

struct AttributeMap; // forward

// ---------------- AttributeValue ----------------

class AttributeValue {
  public:
    // forward-declare shared ptr to allow recursive structure
    using MapPtr = std::shared_ptr<AttributeMap>;
    using Array = std::vector<AttributeValue>;
    using Storage =
        std::variant<std::int64_t, double, bool, std::string, Array, MapPtr, std::nullptr_t>;

    // ctors
    AttributeValue() : value_(std::int64_t{0}) {}
    AttributeValue(std::int64_t v) : value_(v) {}
    AttributeValue(int v) : value_(static_cast<std::int64_t>(v)) {}
    AttributeValue(double v) : value_(v) {}
    AttributeValue(float v) : value_(static_cast<double>(v)) {}
    AttributeValue(bool v) : value_(v) {}
    AttributeValue(std::string v) : value_(std::move(v)) {}
    AttributeValue(const char *v) : value_(std::string{v}) {}
    AttributeValue(const Array &a) : value_(a) {}
    AttributeValue(Array &&a) : value_(std::move(a)) {}
    AttributeValue(const MapPtr &m) : value_(m) {}
    AttributeValue(MapPtr &&m) : value_(std::move(m)) {}
    // null
    static AttributeValue make_null() {
        return AttributeValue(std::nullptr_t{});
    }

    // type predicates
    bool isInt() const {
        return std::holds_alternative<std::int64_t>(value_);
    }
    bool isDouble() const {
        return std::holds_alternative<double>(value_);
    }
    bool isBool() const {
        return std::holds_alternative<bool>(value_);
    }
    bool isString() const {
        return std::holds_alternative<std::string>(value_);
    }
    bool isArray() const {
        return std::holds_alternative<Array>(value_);
    }
    bool isObject() const {
        return std::holds_alternative<MapPtr>(value_) && std::get<MapPtr>(value_) != nullptr;
    }
    bool isNull() const {
        return std::holds_alternative<std::nullptr_t>(value_);
    }

    // accessors (throws std::bad_variant_access if wrong type)
    std::int64_t asInt() const {
        return std::get<std::int64_t>(value_);
    }
    double asDouble() const {
        return std::get<double>(value_);
    }
    float asFloat() const {
        return static_cast<float>(std::get<double>(value_));
    }
    bool asBool() const {
        return std::get<bool>(value_);
    }
    const std::string &asString() const {
        return std::get<std::string>(value_);
    }
    const Array &asArray() const {
        return std::get<Array>(value_);
    }
    Array &asArray() {
        return std::get<Array>(value_);
    }
    const MapPtr &asMapPtr() const {
        return std::get<MapPtr>(value_);
    }
    MapPtr &asMapPtr() {
        return std::get<MapPtr>(value_);
    }

  private:
    Storage value_;

    friend struct nlohmann::adl_serializer<amp::AttributeValue>;
};

// ---------------- AttributeMap ----------------

struct AttributeError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct AttributeMap {
    using Map = std::map<std::string, AttributeValue>;
    AttributeValue &operator[](std::string key) {
        return values_[std::move(key)];
    }

    const AttributeValue &at(const std::string &key) const {
        return values_.at(key);
    }

    bool contains(const std::string &key) const {
        return values_.find(key) != values_.end();
    }

    void clear() {
        values_.clear();
    }

    // typed setters (convenience)
    AttributeMap &set(std::string key, std::int64_t v) {
        values_[std::move(key)] = v;
        return *this;
    }
    AttributeMap &set(std::string key, double v) {
        values_[std::move(key)] = v;
        return *this;
    }
    AttributeMap &set(std::string key, bool v) {
        values_[std::move(key)] = v;
        return *this;
    }
    AttributeMap &set(std::string key, std::string v) {
        values_[std::move(key)] = std::move(v);
        return *this;
    }
    AttributeMap &set(std::string key, const char *v) {
        values_[std::move(key)] = std::string{v};
        return *this;
    }
    AttributeMap &setArray(std::string key, AttributeValue::Array a) {
        values_[std::move(key)] = std::move(a);
        return *this;
    }
    AttributeMap &setObject(std::string key, std::shared_ptr<AttributeMap> m) {
        values_[std::move(key)] = std::move(m);
        return *this;
    }
    AttributeMap &setNull(std::string key) {
        values_[std::move(key)] = AttributeValue::make_null();
        return *this;
    }

    // typed getters
    int64_t getInt(const std::string &key) const {
        return require(key).asInt();
    }
    double getDouble(const std::string &key) const {
        return require(key).asDouble();
    }
    bool getBool(const std::string &key) const {
        return require(key).asBool();
    }
    const std::string &getString(const std::string &key) const {
        return require(key).asString();
    }
    const AttributeValue::Array &getArray(const std::string &key) const {
        return require(key).asArray();
    }

    int64_t getIntOrDefaultOrDefault(const std::string &key, int64_t defaultValue) const {
        try {
            return getInt(key);
        } catch (const AttributeError &error) {
            return defaultValue;
        }
    }
    double getDoubleOrDefault(const std::string &key, double defaultValue) const {
        try {
            return getDouble(key);
        } catch (const AttributeError &error) {
            return defaultValue;
        }
    }
    bool getBoolOrDefault(const std::string &key, bool defaultValue) const {
        try {
            return getDouble(key);
        } catch (const AttributeError &error) {
            return defaultValue;
        }
    }
    const std::string &getStringOrDefault(const std::string &key,
                                          const std::string &defaultValue) const {
        try {
            return getString(key);
        } catch (const AttributeError &error) {
            return defaultValue;
        }
    }
    std::shared_ptr<AttributeMap> getObjectOrNUll(const std::string &key) const {
        try {
            return getObject(key);
        } catch (const AttributeError &error) {
            return nullptr;
        }
    }

    std::shared_ptr<AttributeMap> getObject(const std::string &key) const {
        const auto &v = require(key);
        if (!v.isObject())
            throw AttributeError("AttributeMap: value is not object: " + key);
        return v.asMapPtr();
    }

    const Map &raw() const {
        return values_;
    }
    Map &raw() {
        return values_;
    }

  private:
    const AttributeValue &require(const std::string &key) const {
        auto it = values_.find(key);
        if (it == values_.end()) {
            throw AttributeError("AttributeMap: missing key [" + key + "]");
        }
        return it->second;
    }

    Map values_;
};

} // namespace amp

// ---------------- nlohmann::json adapters ----------------

namespace nlohmann {

template <> struct adl_serializer<amp::AttributeValue> {
    static void to_json(json &j, const amp::AttributeValue &v) {
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

    static void from_json(const json &j, amp::AttributeValue &v) {
        if (j.is_null()) {
            v = amp::AttributeValue::make_null();
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
            amp::AttributeValue::Array arr;
            arr.reserve(j.size());
            for (const auto &elem : j) {
                arr.emplace_back(elem.get<amp::AttributeValue>());
            }
            v = std::move(arr);
            return;
        }
        if (j.is_object()) {
            auto map_ptr = std::make_shared<amp::AttributeMap>();
            for (auto it = j.begin(); it != j.end(); ++it) {
                map_ptr->raw().emplace(it.key(), it.value().get<amp::AttributeValue>());
            }
            v = std::move(map_ptr);
            return;
        }
        throw json::type_error::create(
            302, "AttributeValue must be bool/string/integer/float/array/object/null", &j);
    }
};

template <> struct adl_serializer<amp::AttributeMap> {
    static void to_json(json &j, const amp::AttributeMap &s) {
        j = json::object();
        for (const auto &kv : s.raw()) {
            j[kv.first] = kv.second;
        }
    }
    static void from_json(const json &j, amp::AttributeMap &s) {
        if (!j.is_object()) {
            throw json::type_error::create(302, "AttributeMap must be a JSON object", &j);
        }
        s.clear();
        for (auto it = j.begin(); it != j.end(); ++it) {
            s.raw().emplace(it.key(), it.value().get<amp::AttributeValue>());
        }
    }
};

} // namespace nlohmann