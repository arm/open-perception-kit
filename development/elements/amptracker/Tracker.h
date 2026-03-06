/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "KalmanFilter.h"
#include "amp/Perception.h"

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace amp::tracker {

namespace Defaults {
inline constexpr const char *contentType = "genericObject";
inline constexpr float iouThreshold = 0.3f;
inline constexpr int maxMissedFrames = 5;
inline constexpr int minHitsToConfirm = 5;
inline constexpr bool appendTrackIdToText = true;
inline constexpr int traceHistoryLength = 20;
inline constexpr float traceHistorySeconds = 5.0f;
inline constexpr const char *traceContentType = "trackTrace";
inline constexpr float kalmanDt = 1.0f / 30.0f;
inline constexpr float kalmanInitialCovariancePos = 100.0f;
inline constexpr float kalmanInitialCovarianceVel = 25.0f;
inline constexpr float kalmanProcessNoisePos = 0.1f;
inline constexpr float kalmanProcessNoiseVel = 0.05f;
inline constexpr float kalmanMeasurementNoisePos = 20.0f;
} // namespace Defaults

struct Config {
    std::string contentType = Defaults::contentType;
    float iouThreshold = Defaults::iouThreshold;
    int maxMissedFrames = Defaults::maxMissedFrames;
    int minHitsToConfirm = Defaults::minHitsToConfirm;
    bool appendTrackIdToText = Defaults::appendTrackIdToText;
    int traceHistoryLength = Defaults::traceHistoryLength;
    float traceHistorySeconds = Defaults::traceHistorySeconds;
    std::string traceContentType = Defaults::traceContentType;
    float kalmanDt = Defaults::kalmanDt;
    float kalmanInitialCovariancePos = Defaults::kalmanInitialCovariancePos;
    float kalmanInitialCovarianceVel = Defaults::kalmanInitialCovarianceVel;
    float kalmanProcessNoisePos = Defaults::kalmanProcessNoisePos;
    float kalmanProcessNoiseVel = Defaults::kalmanProcessNoiseVel;
    float kalmanMeasurementNoisePos = Defaults::kalmanMeasurementNoisePos;
};

class Processor {
  public:
    void reset();
    void process(amp::Perception &perception, const Config &config);

  private:
    using TrackKalman = KalmanFilter<4, 2, float>;

    struct Track {
        uint64_t trackId = 0;
        amp::Perception::Rect lastDetection;
        std::deque<amp::Perception::TrackTrace::Point> tracePoints;
        bool kalmanInitialized = false;
        bool predictedThisFrame = false;
        TrackKalman kalman;
        int missedFrames = 0;
        int hitStreak = 0;
        uint64_t lastUpdateFrame = 0;
    };

    std::map<uint64_t, Track> activeTracks;
    uint64_t nextTrackId = 1;
    uint64_t frameCounter = 0;

    amp::Perception::TrackTrace::Point predictCenterPoint(Track &track, const Config &config);
    amp::Perception::TrackTrace::Point updateCenterPointWithMeasurement(
        Track &track, const amp::Perception::Rect &detection, const Config &config);
    void appendTracePoint(Track &track,
                          const amp::Perception::TrackTrace::Point &point,
                          const Config &config);
    void matchDetectionsToTracks(const std::vector<amp::Perception::Rect> &detections,
                                 std::vector<std::pair<size_t, uint64_t>> &matches,
                                 std::vector<size_t> &unmatchedDetections,
                                 const Config &config);
    void updateTracks(const std::vector<amp::Perception::Rect> &detections,
                      const std::vector<std::pair<size_t, uint64_t>> &matches,
                      const std::vector<size_t> &unmatchedDetections,
                      std::map<size_t, uint64_t> &assignedTrackByDetection,
                      std::vector<uint64_t> &predictedOnlyTrackIds,
                      const Config &config);
};

} // namespace amp::tracker
