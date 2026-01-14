#pragma once

namespace amp {

struct ModelInput {
    std::string name;
    TensorDataKind dataKind = TensorDataKind::Unknown;
    uflw::ValueType valueType = uflw::ValueType::f32;
    uflw::Shape shape{};
    int batch = 0;
    uflw::QuantizationArgs quantArguments;

    // should implement these 2 stuffz
    float scale = 1.0f, bias = 0.0f;

    bool tryGetImageTensorSize(size_t &outWidht, size_t &outHeight) {
        if (shape.dimensionCount == 4) {
            if (shape.valueCount[1] == 1 || shape.valueCount[1] == 3) {
                outWidht = shape.valueCount[3];
                outHeight = shape.valueCount[2];
                return true;
            }
            if (shape.valueCount[3] == 1 || shape.valueCount[3] == 3) {
                outWidht = shape.valueCount[2];
                outHeight = shape.valueCount[1];
                return true;
            }
        }
        return false;
    }
};

struct ModelOutput {
    std::string name;
    uflw::ValueType valueType = uflw::ValueType::f32;
    uflw::Shape shape;
    uflw::QuantizationArgs quantArguments;
};

struct Model {

    bool inputSizeAppliedByModel = false;
    bool nmsAppliedByModel = false;

    std::string modelFamily;

    size_t modelInputCount = 0;
    ModelInput inputs[4];

    size_t modelOutputCount = 0;
    ModelOutput outputs[4];
};

} // namespace amp