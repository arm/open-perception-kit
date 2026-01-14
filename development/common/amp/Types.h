#pragma once

#include <cstdint>

#if defined(__arm__) || defined(__aarch64__)
using f16_type = __fp16;
#elif defined(__x86_64__)
using f16_type = _Float16;
#else
#error Unsupported architecture
#endif

namespace amp {

using u8 = unsigned char;
using i8 = signed char;
using f16 = f16_type;
using f32 = float;
using i64 = int64_t;

enum class ValueType { u8, i8, f16, f32, i64 };

using ValuePointer = void *;

inline size_t getValueTypeByteSize(ValueType type) {
    switch (type) {
    case ValueType::i8:
        return 1;
    case ValueType::u8:
        return 1;
    case ValueType::f16:
        return 2;
    case ValueType::f32:
        return 4;
    case ValueType::i64:
        return 8;
    }
    return 0;
}

struct QuantizationArgs {
    float scale = 1.0f;
    float zeroPoint = 0.0f;
};

// ---

enum class TensorInOut { In, Out };

enum class TensorDataKind {
    Unknown = 0,
    ImageRgbChw,
    ImageRgbHwc,
    ImageGray,

    Value,
    Vector2,
    Vector3,
    Vector4,

    AudioDUMMY,
    TextDUMMY,
};

inline bool isScalarDataKind(TensorDataKind kind) {
    if (kind == TensorDataKind::Value)
        return true;
    if (kind == TensorDataKind::Vector2)
        return true;
    if (kind == TensorDataKind::Vector3)
        return true;
    if (kind == TensorDataKind::Vector4)
        return true;
    return false;
}

} // namespace amp
