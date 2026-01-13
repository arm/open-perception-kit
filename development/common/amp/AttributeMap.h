// attribute_set.hpp
#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include <nlohmann/json.hpp>

namespace amp {

// ---------------- AttributeValue ----------------

class AttributeValue {
  public:
    using Storage = std::variant<std::int64_t, double, bool, std::string>;

    AttributeValue() : value_(std::int64_t{0}) {}
    AttributeValue(std::int64_t v) : value_(v) {}
    AttributeValue(int v) : value_(static_cast<std::int64_t>(v)) {}
    AttributeValue(double v) : value_(v) {}
    AttributeValue(float v) : value_(static_cast<double>(v)) {}
    AttributeValue(bool v) : value_(v) {}
    AttributeValue(std::string v) : value_(std::move(v)) {}
    AttributeValue(const char *v) : value_(std::string{v}) {}

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

  private:
    Storage value_;

    friend struct nlohmann::adl_serializer<amp::AttributeValue>;
};

// ---------------- AttributeMap ----------------

struct AttributeError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class AttributeMap {
  public:
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

    // Typed setters
    AttributeMap &set(std::string key, std::int64_t v) {
        values_[std::move(key)] = v;
        return *this;
    }
    AttributeMap &set(std::string key, int v) {
        values_[std::move(key)] = v;
        return *this;
    }
    AttributeMap &set(std::string key, double v) {
        values_[std::move(key)] = v;
        return *this;
    }
    AttributeMap &set(std::string key, float v) {
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
        values_[std::move(key)] = v;
        return *this;
    }

    // Typed getters
    std::int64_t getInt(const std::string &key) const {
        return require(key).asInt();
    }

    double getDouble(const std::string &key) const {
        return require(key).asDouble();
    }

    float getFloat(const std::string &key) const {
        const auto &v = require(key);
        if (!v.isDouble()) {
            throw amp::AttributeError("AttributeMap: value is not floating-point: " + key);
        }
        return v.asFloat();
    }

    float getNumberAsFloat(const std::string &key) const {
        const auto &v = require(key);
        if (v.isDouble())
            return v.asFloat();
        if (v.isInt())
            return static_cast<float>(v.asInt());
        throw amp::AttributeError("AttributeMap: value is not numeric: " + key);
    }

    bool getBool(const std::string &key) const {
        return require(key).asBool();
    }

    const std::string &getString(const std::string &key) const {
        return require(key).asString();
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
            throw amp::AttributeError("AttributeMap: missing key '" + key + "'");
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
        std::visit([&](auto &&x) { j = x; }, v.value_);
    }

    static void from_json(const json &j, amp::AttributeValue &v) {
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

        throw json::type_error::create(302, "AttributeValue must be bool/string/integer/float", &j);
    }
};

template <> struct adl_serializer<amp::AttributeMap> {
    static void to_json(json &j, const amp::AttributeMap &s) {
        j = json::object();
        for (const auto &[k, v] : s.raw())
            j[k] = v;
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

// ---------------- example ----------------
/*
#include "attribute_set.hpp"
#include <iostream>

int main() {
  amp::AttributeMap attrs;
  amp.set("name", "imu")
       .set("enabled", true)
       .set("rateHz", 100)
       .set("scale", 0.5f);

  nlohmann::json j = attrs;
  std::cout << j.dump(2) << "\n";

  auto attrs2 = j.get<attrs::AttributeMap>();
  std::cout << attrs2.getFloat("scale") << "\n";
}
*/