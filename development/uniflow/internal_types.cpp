#include "internal_types.h"

using namespace uflw;

void uflw::updateRange(QuantizationArgs& qantArgs, ValueType valueType) {
    float rmin = 0.0f, rmax = 1.0f;

    switch (valueType) {
        case ValueType::u8: {
            // u8 integers in [0, 255]
            rmin = qantArgs.scale * (0 - qantArgs.zeroPoint);
            rmax = qantArgs.scale * (255 - qantArgs.zeroPoint);
            break;
        }
        case ValueType::i8: {
            // i8 integers in [-128, 127]
            rmin = qantArgs.scale * (-128 - qantArgs.zeroPoint);
            rmax = qantArgs.scale * (127 - qantArgs.zeroPoint);
            break;
        }
        case ValueType::f16:
        case ValueType::f32: {
            // No quantization implied by type, using a neutral default
            rmin = 0.0f;
            rmax = 1.0f;
            break;
        }
        default:
            rmin = 0.0f; rmax = 1.0f;
    }

    qantArgs.trainRangeMin = rmin;
    qantArgs.trainRangeMax = rmax;
}

