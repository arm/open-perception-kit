#pragma once

#include "amp/PerceptionContext.h"
#include "amp/Result.h"
#include "amp/TensorReader.h"

namespace amp {

struct NetworkOutputParser {

    struct Settings {
        bool normalizedCoordinates = false;
        float confidenceThreshold = 0.0f;
        float iouThreshold = 0.0f;
        bool fixedAnchorSizes = false;
        bool applyNms = true;
        size_t maxDetectionCount = 0;
    };

    struct ImageImferenceMetadata {
        size_t width = 0, height = 0;
        size_t modelWidth = 0, modelHeight = 0;
    };

    struct InferenceMetadata {
        ImageImferenceMetadata image;
    };

    virtual amp::Result<void> parse(const amp::TensorReader *tensorReades[4],
                                    const NetworkOutputParser::Settings &settings,
                                    const NetworkOutputParser::InferenceMetadata &metaData,
                                    amp::DetectionResult &detectionResult) = 0;
};

struct NetworkInputBuilder {

    struct Original {

        const uint8_t *data = nullptr;
        size_t byteCount = 0;

        size_t width = 0;
        size_t height = 0;

        TensorDataKind kind = TensorDataKind::Unknown;
        amp::ValueType type = amp::ValueType::f32;
    };

    struct Target {
        uint8_t *data = nullptr;
        size_t byteCount = 0;

        size_t width = 0;
        size_t height = 0;

        TensorDataKind kind = TensorDataKind::Unknown;
        amp::ValueType type = amp::ValueType::f32;
    };

    struct Setup {

        Original original;
        Target target;

        QuantizationArgs quantizationArgs;
        bool quantizeValues = false;
        bool quantizeVectors = false;
    };

    virtual amp::Result<void> build(const NetworkInputBuilder::Setup &setup) = 0;
};

} // namespace amp