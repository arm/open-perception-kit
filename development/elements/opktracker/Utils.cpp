/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Utils.h"

#include <cmath>

namespace opk::tracker {

float cosineSimilarity(const std::vector<float> &a, const std::vector<float> &b) {
    if (a.empty() || b.empty() || a.size() != b.size()) {
        return 0.0f;
    }

    float dot = 0.0f;
    float normA = 0.0f;
    float normB = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }

    if (normA <= 1e-12f || normB <= 1e-12f) {
        return 0.0f;
    }

    return dot / (std::sqrt(normA) * std::sqrt(normB));
}

bool isValidEmbedding(const std::vector<float> &embedding) {
    if (embedding.empty()) {
        return false;
    }

    float normSq = 0.0f;
    for (const float value : embedding) {
        if (!std::isfinite(value)) {
            return false;
        }
        normSq += value * value;
    }

    return normSq > 1e-12f;
}

} // namespace opk::tracker
