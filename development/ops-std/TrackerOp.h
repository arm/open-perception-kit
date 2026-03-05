/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "KalmanFilter.h"
#include "amp/Perception.h"
#include "amp/Result.h"
#include "op/Op.h"
#include "op/OpChainContext.h"

#include <deque>
#include <map>
#include <vector>

namespace amp {

/**
 * TrackerOp - Associates detections across frames with persistent track IDs
 *
 * Uses IoU-based matching to track objects detected by YOLO or similar detectors.
 * Maintains tracks across frames and assigns unique IDs to tracked objects.
 */
class TrackerOp : public amp::Op {
  public:
    TrackerOp();
    virtual ~TrackerOp();

    virtual Result<void> configure(const AttributeMap &attributes) override;
    virtual Result<void> bind(size_t index, const std::vector<amp::Op *> &ops) override;
    virtual Result<void> process(OpChainContext &opChainContext) override;

  private:
    using TrackKalman = KalmanFilter<4, 2, float>;

    struct Track {
        uint64_t trackId;
        Perception::Rect lastDetection;
        std::deque<Perception::TrackTrace::Point> tracePoints;
        bool kalmanInitialized = false;
        TrackKalman kalman;
        int missedFrames = 0;
        int hitStreak = 0;
        uint64_t lastUpdateFrame = 0;
    };

    struct Defaults {
        static constexpr const char *contentType = "genericObject";
        static constexpr float iouThreshold = 0.3f;
        static constexpr int64_t maxMissedFrames = 5;
        static constexpr int64_t minHitsToConfirm = 5;
        static constexpr bool appendTrackIdToText = true;
        static constexpr int64_t traceHistoryLength = 20;
        static constexpr float traceHistorySeconds = 0.0f;
        static constexpr const char *traceContentType = "trackTrace";
        static constexpr float kalmanDt = 1.0f / 30.0f;
        static constexpr float kalmanInitialCovariancePos = 100.0f;
        static constexpr float kalmanInitialCovarianceVel = 25.0f;
        static constexpr float kalmanProcessNoisePos = 0.1f;
        static constexpr float kalmanProcessNoiseVel = 0.05f;
        static constexpr float kalmanMeasurementNoisePos = 20.0f;
    };

    // Configuration parameters
    std::string contentType{Defaults::contentType};           // Type of detections to track
    float iouThreshold{Defaults::iouThreshold};               // IOU threshold for matching
    int64_t maxMissedFrames{Defaults::maxMissedFrames};       // Max frames before track deletion
    int64_t minHitsToConfirm{Defaults::minHitsToConfirm};     // Min hits before track is confirmed
    bool appendTrackIdToText{Defaults::appendTrackIdToText};  // Append track ID to text field
    int64_t traceHistoryLength{Defaults::traceHistoryLength}; // Number of history points per track
    float traceHistorySeconds{Defaults::traceHistorySeconds}; // Optional history window in seconds
    std::string traceContentType{
        Defaults::traceContentType}; // Perception layer contentType for traces
    float kalmanDt{Defaults::kalmanDt};
    float kalmanInitialCovariancePos{Defaults::kalmanInitialCovariancePos};
    float kalmanInitialCovarianceVel{Defaults::kalmanInitialCovarianceVel};
    float kalmanProcessNoisePos{Defaults::kalmanProcessNoisePos};
    float kalmanProcessNoiseVel{Defaults::kalmanProcessNoiseVel};
    float kalmanMeasurementNoisePos{Defaults::kalmanMeasurementNoisePos};

    // Tracking state
    std::map<uint64_t, Track> activeTracks;
    uint64_t nextTrackId = 1;
    uint64_t frameCounter = 0;

    // Helper methods
    float computeIOU(const Perception::Rect &a, const Perception::Rect &b) const;
    void matchDetectionsToTracks(const std::vector<Perception::Rect> &detections,
                                 std::vector<std::pair<size_t, uint64_t>> &matches,
                                 std::vector<size_t> &unmatchedDetections);
    void updateTracks(const std::vector<Perception::Rect> &detections,
                      const std::vector<std::pair<size_t, uint64_t>> &matches,
                      const std::vector<size_t> &unmatchedDetections,
                      std::map<size_t, uint64_t> &assignedTrackByDetection,
                      std::vector<uint64_t> &predictedOnlyTrackIds);
    Perception::TrackTrace::Point predictCenterPoint(Track &track);
    Perception::TrackTrace::Point
    updateCenterPointWithMeasurement(Track &track, const Perception::Rect &detection);
    void appendTracePoint(Track &track, const Perception::TrackTrace::Point &point);
};

} // namespace amp
