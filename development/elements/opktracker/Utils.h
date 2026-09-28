/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include <vector>

namespace opk::tracker {

float cosineSimilarity(const std::vector<float> &a, const std::vector<float> &b);
bool isValidEmbedding(const std::vector<float> &embedding);

} // namespace opk::tracker
