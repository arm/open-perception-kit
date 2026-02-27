/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

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
    struct Track {
        uint64_t trackId;
        Perception::Rect lastDetection;
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
    };

    // Configuration parameters
    std::string contentType{Defaults::contentType};          // Type of detections to track
    float iouThreshold{Defaults::iouThreshold};              // IOU threshold for matching
    int64_t maxMissedFrames{Defaults::maxMissedFrames};      // Max frames before track deletion
    int64_t minHitsToConfirm{Defaults::minHitsToConfirm};    // Min hits before track is confirmed
    bool appendTrackIdToText{Defaults::appendTrackIdToText}; // Append track ID to text field

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
                      const std::vector<size_t> &unmatchedDetections);
};

} // namespace amp
