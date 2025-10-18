#include "basic_proc.h"

#include <cmath>
#include <vector>
#include <algorithm>

using namespace uflw;

// usually used inside "greedy" non max supression
float uflw::m::intersectionOverUnionXyxy(const DetectionBox& a, const DetectionBox& b) {
    float x1 = std::max(a.x1, b.x1);
    float y1 = std::max(a.y1, b.y1);
    float x2 = std::min(a.x2, b.x2);
    float y2 = std::min(a.y2, b.y2);
    float w = std::max(0.f, x2 - x1);
    float h = std::max(0.f, y2 - y1);

    float intersection = w * h;
    float aa=(a.x2-a.x1) * (a.y2-a.y1);
    float bb=(b.x2-b.x1) * (b.y2-b.y1);

    float denom = aa + bb - intersection;

    return denom > 0 ? intersection / denom : 0.f;
}

std::vector<DetectionBox> uflw::m::nonMaxSupression(const std::vector<DetectionBox>& originalValues, 
    float iouThreshold, int maxKeep) {
    
    auto values = originalValues;

    std::sort(values.begin(), values.end(), [](auto& A, auto& B) {
        return A.score > B.score; 
    });

    std::vector<char> sup(values.size(), 0);
  
  std::vector<DetectionBox> out; 
  out.reserve(std::min(maxKeep, (int)values.size()));
  
  for (size_t i = 0; i < values.size();++i) {
    if(sup[i]) continue; 
    out.push_back(values[i]);
    for(size_t j = i + 1; j < values.size(); ++j) 
        if (!sup[j] && intersectionOverUnionXyxy(values[i], values[j]) > iouThreshold) 
            sup[j] = 1;
    if ((int)out.size() >= maxKeep) break;
  }
  return out;
}

float uflw::m::sigmoid(float x) { 
    return 1.f / (1.f + std::exp(-x)); 
}

