/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <vector>

namespace opk::tracker {

float cosineSimilarity(const std::vector<float> &a, const std::vector<float> &b);
bool isValidEmbedding(const std::vector<float> &embedding);

} // namespace opk::tracker
