#include "basic_proc.h"

#include <cmath>
#include <vector>
#include <algorithm>

struct Det {
  float x1, y1, x2, y2;  // absolute pixels
  float score;           // confidence
  int   cls;             // class index
};

// IoU and NMS (greedy)
float iou_xyxy(const Det& a, const Det& b) {
  float x1 = std::max(a.x1, b.x1), y1 = std::max(a.y1, b.y1);
  float x2 = std::min(a.x2, b.x2), y2 = std::min(a.y2, b.y2);
  float w = std::max(0.f, x2 - x1), h = std::max(0.f, y2 - y1);
  float inter = w*h, aa=(a.x2-a.x1)*(a.y2-a.y1), bb=(b.x2-b.x1)*(b.y2-b.y1);
  float denom = aa + bb - inter;
  return denom > 0 ? inter / denom : 0.f;
}

std::vector<Det> nms(std::vector<Det> v, float iou_thr, int max_keep=100) {
  std::sort(v.begin(), v.end(), [](auto& A, auto& B){ return A.score > B.score; });
  std::vector<char> sup(v.size(), 0);
  std::vector<Det> out; out.reserve(std::min(max_keep,(int)v.size()));
  for (size_t i=0;i<v.size();++i) {
    if (sup[i]) continue; 
    out.push_back(v[i]);
    for (size_t j=i+1;j<v.size();++j) if (!sup[j] && iou_xyxy(v[i], v[j]) > iou_thr) sup[j]=1;
    if ((int)out.size() >= max_keep) break;
  }
  return out;
}

float uflw::m::sigmoid(float x) { 
    return 1.f / (1.f + std::exp(-x)); 
}

