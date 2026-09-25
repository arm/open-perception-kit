/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "TrackState.h"
#include "opk/FrameResults.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace opk::tracker {

enum class AssociationMode { Hybrid, Iou, Embedding };

namespace Defaults {
inline constexpr const char *contentType = "genericObject";
inline constexpr float iouThreshold = 0.3f;
inline constexpr int maxMissedFrames = 5;
inline constexpr int minHitsToConfirm = 5;
inline constexpr bool appendIdentityIdToText = true;
inline constexpr bool useEmbeddings = true;
inline constexpr const char *embeddingContentType = "objectEmbedding";
inline constexpr float embeddingWeight = 0.5f;
inline constexpr float minCosineSimilarity = 0.0f;
inline constexpr float reidReassociateThreshold = 0.65f;
inline constexpr float dormantTrackHistorySeconds = 8.0f;
inline constexpr float traceHistorySeconds = 5.0f;
inline constexpr float kalmanDtFallback = 1.0f / 30.0f;
inline constexpr bool kalmanDtForceFallback = false;
inline constexpr float kalmanInitialCovariancePos = 100.0f;
inline constexpr float kalmanInitialCovarianceVel = 25.0f;
inline constexpr float kalmanProcessNoisePos = 0.1f;
inline constexpr float kalmanProcessNoiseVel = 0.05f;
inline constexpr float kalmanMeasurementNoisePos = 20.0f;
inline constexpr const char *inferId = "";
inline constexpr bool useKalman = true;
inline constexpr bool emitPredictedDetections = true;
inline constexpr bool emitTrace = true;
inline constexpr AssociationMode associationMode = AssociationMode::Hybrid;
} // namespace Defaults

struct Config {
    std::string contentType = Defaults::contentType;
    float iouThreshold = Defaults::iouThreshold;
    int maxMissedFrames = Defaults::maxMissedFrames;
    int minHitsToConfirm = Defaults::minHitsToConfirm;
    bool appendIdentityIdToText = Defaults::appendIdentityIdToText;
    bool useEmbeddings = Defaults::useEmbeddings;
    std::string embeddingContentType = Defaults::embeddingContentType;
    float embeddingWeight = Defaults::embeddingWeight;
    float minCosineSimilarity = Defaults::minCosineSimilarity;
    float reidReassociateThreshold = Defaults::reidReassociateThreshold;
    float dormantTrackHistorySeconds = Defaults::dormantTrackHistorySeconds;
    float traceHistorySeconds = Defaults::traceHistorySeconds;
    float kalmanDtFallback = Defaults::kalmanDtFallback;
    bool kalmanDtForceFallback = Defaults::kalmanDtForceFallback;
    float kalmanInitialCovariancePos = Defaults::kalmanInitialCovariancePos;
    float kalmanInitialCovarianceVel = Defaults::kalmanInitialCovarianceVel;
    float kalmanProcessNoisePos = Defaults::kalmanProcessNoisePos;
    float kalmanProcessNoiseVel = Defaults::kalmanProcessNoiseVel;
    float kalmanMeasurementNoisePos = Defaults::kalmanMeasurementNoisePos;
    std::string inferId = Defaults::inferId;
    std::string producerInstanceId;
    bool useKalman = Defaults::useKalman;
    bool emitPredictedDetections = Defaults::emitPredictedDetections;
    bool emitTrace = Defaults::emitTrace;
    AssociationMode associationMode = Defaults::associationMode;
};

class KalmanDeltaTimeTracking {
  public:
    void reset() {
        lastFrameRunningTimeMs.reset();
        resolvedKalmanDt = Defaults::kalmanDtFallback;
        elapsedTrackerTimeMs = 0.0;
        usingKalmanDtFallback = false;
        forcedKalmanDtFallback = false;
    }

    void update(std::optional<uint64_t> runningTimeMs, const Config &config) {
        const bool hasPreviousFrameTimestamp = lastFrameRunningTimeMs.has_value();
        const bool hasCurrentFrameTimestamp = runningTimeMs.has_value();
        const bool hasRunningTimeDelta = hasPreviousFrameTimestamp && hasCurrentFrameTimestamp &&
                                         *runningTimeMs > *lastFrameRunningTimeMs;
        const double runningTimeDeltaMs =
            hasRunningTimeDelta ? static_cast<double>(*runningTimeMs - *lastFrameRunningTimeMs)
                                : static_cast<double>(config.kalmanDtFallback) * 1'000.0;
        // Lifecycle time follows valid media timing even when Kalman fallback is forced.
        elapsedTrackerTimeMs += runningTimeDeltaMs;

        forcedKalmanDtFallback = config.kalmanDtForceFallback;
        usingKalmanDtFallback = forcedKalmanDtFallback || !hasRunningTimeDelta;
        resolvedKalmanDt = usingKalmanDtFallback
                               ? config.kalmanDtFallback
                               : static_cast<float>(runningTimeDeltaMs) / 1'000.0f;
        if (runningTimeMs.has_value()) {
            lastFrameRunningTimeMs = runningTimeMs;
        } else {
            lastFrameRunningTimeMs.reset();
        }
    }

    float effectiveKalmanDt() const {
        return resolvedKalmanDt;
    }

    double trackerTimeMs() const {
        return elapsedTrackerTimeMs;
    }

    bool usesFallback() const {
        return usingKalmanDtFallback;
    }

    bool fallbackForced() const {
        return forcedKalmanDtFallback;
    }

  private:
    std::optional<uint64_t> lastFrameRunningTimeMs;
    float resolvedKalmanDt = Defaults::kalmanDtFallback;
    double elapsedTrackerTimeMs = 0.0;
    bool usingKalmanDtFallback = false;
    bool forcedKalmanDtFallback = false;
};

using TrackId = uint64_t;
using DetectionIndex = size_t;
using DetectionTrackAssignments = std::map<DetectionIndex, TrackId>;
using TrackIdList = std::vector<TrackId>;
using ActiveTrackMap = std::map<TrackId, TrackState>;
using DormantTrackMap = std::map<TrackId, DormantTrackState>;

using EmbeddingBatch = std::map<uint64_t, const std::vector<float> *>;
using DetectionBatch = std::vector<const open_perception_kit::metadata::BoxDetectionT *>;
using TrackMatch = std::pair<DetectionIndex, TrackId>;

struct AssociationResult {
    std::vector<TrackMatch> matches;
    std::map<DetectionIndex, std::string> diagnosticsByDetection;
    std::vector<DetectionIndex> unmatchedDetections;
};

class Tracker {
  public:
    /**
     * @brief Resets tracker runtime state.
     */
    void reset();

    const KalmanDeltaTimeTracking &kalmanDeltaTimeTracking() const;

    /**
     * @brief Processes one FrameResults frame through the tracking pipeline.
     * @param frameResults Generated metadata for the current frame.
     * @param config Tracker runtime configuration.
     */
    void process(open_perception_kit::FrameResults &frameResults,
                 const Config &config,
                 std::optional<uint64_t> runningTimeMs);

  private:
    ActiveTrackMap activeTracks;
    DormantTrackMap inactiveTracks;
    TrackId nextTrackId = 1;
    uint64_t currentFrameIndex = 0;
    KalmanDeltaTimeTracking kalmanDeltaTime;
};

} // namespace opk::tracker
