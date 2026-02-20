#include "postproc/ImageNetClassificationParser.h"
#include "amp/Labels.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

using namespace amp;

// Softmax helper function
static void softmax(const std::span<float> input, std::span<float> output) {

    assert(input.size() == output.size());

    auto it = std::max_element(input.begin(), input.end());
    float max = *it;
    auto sum = 0.0f;
    for (auto i = 0U; i < output.size(); ++i) {
        output[i] = std::exp(input[i] - max);
        sum += output[i];
    }
    for (auto i = 0U; i < output.size(); ++i) {
        output[i] /= sum;
    }
}

amp::Result<void> ImageNetClassificationParser::parse(const amp::TensorParser::Input &input,
                                                      amp::Perception::Layer &detectionResult) {

    assert(input.tensors[0]);

    const auto shape = input.tensors[0]->getShape();
    assert(shape.dimensionCount == 2U);
    assert(shape.valueCount[0] == 1U);

    constexpr auto numClasses = Labels::getLabelCount(LabelType::ImageNet);
    assert(numClasses == shape.valueCount[1]);

    const int topK = input.attributes.getIntOrDefault("topK", 5);
    const float confidenceThreshold =
        input.attributes.getDoubleOrDefault("confidenceThreshold", 0.01);

    std::array<float, numClasses> probabilities;
    std::array<float, numClasses> logits;

    for (size_t i = 0U; i < numClasses; ++i) {
        logits[i] = input.tensors[0]->get(i);
    }

    softmax(logits, probabilities);

    std::vector<std::pair<float, int>> scoredIndices;
    scoredIndices.reserve(numClasses);

    for (size_t i = 0; i < numClasses; ++i) {
        if (probabilities[i] >= confidenceThreshold) {
            scoredIndices.push_back({probabilities[i], static_cast<int>(i)});
        }
    }

    const auto numResults = std::min(topK, static_cast<int>(scoredIndices.size()));

    if (0 < numResults) {
        // Partial sort with std::greater<> - only sorts top-K
        std::partial_sort(scoredIndices.begin(),
                          scoredIndices.begin() + numResults,
                          scoredIndices.end(),
                          std::greater<>());

        amp::Perception::Classification classification;

        detectionResult.contentType = "classification";

        classification.candidates.reserve(numResults);
        for (int i = 0; i < numResults; ++i) {
            const auto &[confidence, classIdx] = scoredIndices[i];

            // Store classification result as DetectionRect
            // x,y will be used to position the label in lower-right corner
            // w,h are not used for classification (no actual bounding box)
            amp::Perception::Classification::Candidate candidate;
            candidate.x = 0.0f;                  // Position will be calculated by renderer
            candidate.y = static_cast<float>(i); // Store index for rendering
            candidate.w = 0.0f;                  // Not used
            candidate.h = 0.0f;                  // Not used
            candidate.confidence = confidence;
            candidate.text = theImageNetLabels[classIdx];

            classification.candidates.push_back(candidate);
        }

        detectionResult.detections.push_back(classification);
    }

    return {};
}
