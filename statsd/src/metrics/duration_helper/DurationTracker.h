/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef DURATION_TRACKER_H
#define DURATION_TRACKER_H

#include "anomaly/DurationAnomalyTracker.h"
#include "condition/ConditionWizard.h"
#include "config/ConfigKey.h"
#include "metrics/parsing_utils/config_update_utils.h"
#include "stats_util.h"

namespace android {
namespace os {
namespace statsd {

enum DurationState {
    kStopped = 0,  // The event is stopped.
    kStarted = 1,  // The event is on going.
    kPaused = 2,   // The event is started, but condition is false, clock is paused. When condition
                   // turns to true, kPaused will become kStarted.
};

// Hold duration information for one atom level duration in current on-going bucket.
struct DurationInfo {
    DurationState state;

    // the number of starts seen.
    int32_t startCount;

    // most recent start time.
    int64_t lastStartTime;
    // existing duration in current bucket.
    int64_t lastDuration;
    // cache the HashableDimensionKeys we need to query the condition for this duration event.
    ConditionKey conditionKeys;

    DurationInfo() : state(kStopped), startCount(0), lastStartTime(0), lastDuration(0){};
};

struct DurationBucket {
    int64_t mBucketStartNs;
    int64_t mBucketEndNs;
    int64_t mDuration;
    int64_t mConditionTrueNs;

    DurationBucket() : mBucketStartNs(0), mBucketEndNs(0), mDuration(0), mConditionTrueNs(0){};
};

struct DurationValues {
    // Recorded duration for current partial bucket.
    int64_t mDuration;

    // Sum of past partial bucket durations in current full bucket.
    // Used for anomaly detection.
    int64_t mDurationFullBucket;

    DurationValues() : mDuration(0), mDurationFullBucket(0){};
};

class DurationTracker {
public:
    DurationTracker(const ConfigKey& key, const int64_t id, const MetricDimensionKey& eventKey,
                    const sp<ConditionWizard>& wizard, int conditionIndex, bool nesting,
                    int64_t currentBucketStartNs, int64_t currentBucketNum, int64_t startTimeNs,
                    int64_t bucketSizeNs, bool conditionSliced, bool fullLink,
                    const std::vector<sp<AnomalyTracker>>& anomalyTrackers)
        : mConfigKey(key),
          mTrackerId(id),
          mEventKey(eventKey),
          mWizard(wizard),
          mConditionTrackerIndex(conditionIndex),
          mBucketSizeNs(bucketSizeNs),
          mNested(nesting),
          mCurrentBucketStartTimeNs(currentBucketStartNs),
          mCurrentBucketNum(currentBucketNum),
          mStartTimeNs(startTimeNs),
          mConditionSliced(conditionSliced),
          mHasLinksToAllConditionDimensionsInTracker(fullLink),
          mAnomalyTrackers(anomalyTrackers),
          mHasHitGuardrail(false){};

    virtual ~DurationTracker(){};

    void onConfigUpdated(const sp<ConditionWizard>& wizard, int conditionTrackerIndex) {
        sp<ConditionWizard> tmpWizard = mWizard;
        mWizard = wizard;
        mConditionTrackerIndex = conditionTrackerIndex;
        mAnomalyTrackers.clear();
    };

    virtual void noteStart(const HashableDimensionKey& key, bool condition, int64_t eventTime,
                           const ConditionKey& conditionKey, size_t dimensionHardLimit) = 0;
    virtual void noteStop(const HashableDimensionKey& key, int64_t eventTime,
                          const bool stopAll) = 0;
    virtual void noteStopAll(const int64_t eventTime) = 0;

    virtual void onSlicedConditionMayChange(const int64_t timestamp) = 0;
    virtual void onConditionChanged(bool condition, int64_t timestamp) = 0;

    virtual void onStateChanged(const int64_t timestamp, const int32_t atomId,
                                const FieldValue& newState) = 0;

    // Flush stale buckets if needed, and return true if the tracker has no on-going duration
    // events, so that the owner can safely remove the tracker.
    virtual bool flushIfNeeded(
            int64_t timestampNs, const optional<UploadThreshold>& uploadThreshold,
            std::unordered_map<MetricDimensionKey, std::vector<DurationBucket>>* output) = 0;

    // Should only be called during an app upgrade or from this tracker's flushIfNeeded. If from
    // an app upgrade, we assume that we're trying to form a partial bucket.
    virtual bool flushCurrentBucket(
            int64_t eventTimeNs, const optional<UploadThreshold>& uploadThreshold,
            const int64_t globalConditionTrueNs,
            std::unordered_map<MetricDimensionKey, std::vector<DurationBucket>>* output) = 0;

    // Predict the anomaly timestamp given the current status.
    virtual int64_t predictAnomalyTimestampNs(const AnomalyTracker& anomalyTracker,
                                              const int64_t currentTimestamp) const = 0;
    // Dump internal states for debugging
    virtual void dumpStates(int out, bool verbose) const = 0;

    virtual int64_t getCurrentStateKeyDuration() const = 0;

    virtual int64_t getCurrentStateKeyFullBucketDuration() const = 0;

    // Replace old value with new value for the given state atom.
    virtual void updateCurrentStateKey(int32_t atomId, const FieldValue& newState) = 0;

    virtual bool hasAccumulatedDuration() const = 0;

    void addAnomalyTracker(sp<AnomalyTracker>& anomalyTracker, const UpdateStatus& updateStatus,
                           const int64_t updateTimeNs) {
        mAnomalyTrackers.push_back(anomalyTracker);
        // Preserved anomaly trackers will have the correct alarm times.
        // New/replaced alerts will need to set alarms for pending durations, or may have already
        // fired if the full bucket duration is high enough.
        // NB: this depends on a config updating that splits a partial bucket having just happened.
        // If this constraint changes, predict will return the wrong timestamp.
        if (updateStatus == UpdateStatus::UPDATE_NEW ||
            updateStatus == UpdateStatus::UPDATE_PRESERVE) {
            const int64_t alarmTimeNs = predictAnomalyTimestampNs(*anomalyTracker, updateTimeNs);
            if (alarmTimeNs <= updateTimeNs || hasStartedDuration()) {
                anomalyTracker->startAlarm(mEventKey, std::max(alarmTimeNs, updateTimeNs));
            }
        }
    }

protected:
    virtual bool hasStartedDuration() const = 0;

    int64_t getCurrentBucketEndTimeNs() const {
        return mStartTimeNs + (mCurrentBucketNum + 1) * mBucketSizeNs;
    }

    // Starts the anomaly alarm.
    void startAnomalyAlarm(const int64_t eventTime) {
        for (auto& anomalyTracker : mAnomalyTrackers) {
            if (anomalyTracker != nullptr) {
                const int64_t alarmTimestampNs =
                    predictAnomalyTimestampNs(*anomalyTracker, eventTime);
                if (alarmTimestampNs > 0) {
                    anomalyTracker->startAlarm(mEventKey, alarmTimestampNs);
                }
            }
        }
    }

    // Stops the anomaly alarm. If it should have already fired, declare the anomaly now.
    void stopAnomalyAlarm(const int64_t timestamp) {
        for (auto& anomalyTracker : mAnomalyTrackers) {
            if (anomalyTracker != nullptr) {
                anomalyTracker->stopAlarm(mEventKey, timestamp);
            }
        }
    }

    void addPastBucketToAnomalyTrackers(const MetricDimensionKey& eventKey, int64_t bucketValue,
                                        int64_t bucketNum) {
        for (auto& anomalyTracker : mAnomalyTrackers) {
            if (anomalyTracker != nullptr) {
                anomalyTracker->addPastBucket(eventKey, bucketValue, bucketNum);
            }
        }
    }

    void detectAndDeclareAnomaly(int64_t timestamp, int64_t currBucketNum,
                                 int64_t currentBucketValue) {
        for (auto& anomalyTracker : mAnomalyTrackers) {
            if (anomalyTracker != nullptr) {
                anomalyTracker->detectAndDeclareAnomaly(timestamp, currBucketNum, mTrackerId,
                                                        mEventKey, currentBucketValue);
            }
        }
    }

    // Convenience to compute the current bucket's end time, which is always aligned with the
    // start time of the metric.
    int64_t getCurrentBucketEndTimeNs() {
        return mStartTimeNs + (mCurrentBucketNum + 1) * mBucketSizeNs;
    }

    void setEventKey(const MetricDimensionKey& eventKey) {
        mEventKey = eventKey;
    }

    bool durationPassesThreshold(const optional<UploadThreshold>& uploadThreshold,
                                 int64_t duration) {
        if (duration <= 0) {
            return false;
        }

        if (uploadThreshold == nullopt) {
            return true;
        }

        switch (uploadThreshold->value_comparison_case()) {
            case UploadThreshold::kLtInt:
                return duration < uploadThreshold->lt_int();
            case UploadThreshold::kGtInt:
                return duration > uploadThreshold->gt_int();
            case UploadThreshold::kLteInt:
                return duration <= uploadThreshold->lte_int();
            case UploadThreshold::kGteInt:
                return duration >= uploadThreshold->gte_int();
            default:
                ALOGE("Duration metric incorrect upload threshold type used");
                return false;
        }
    }

    // A reference to the DurationMetricProducer's config key.
    const ConfigKey& mConfigKey;

    const int64_t mTrackerId;

    MetricDimensionKey mEventKey;

    sp<ConditionWizard> mWizard;

    int mConditionTrackerIndex;

    const int64_t mBucketSizeNs;

    const bool mNested;

    int64_t mCurrentBucketStartTimeNs;

    // Recorded duration results for each state key in the current partial bucket.
    std::unordered_map<HashableDimensionKey, DurationValues> mStateKeyDurationMap;

    int64_t mCurrentBucketNum;

    const int64_t mStartTimeNs;

    const bool mConditionSliced;

    bool mHasLinksToAllConditionDimensionsInTracker;

    std::vector<sp<AnomalyTracker>> mAnomalyTrackers;

    mutable bool mHasHitGuardrail;

    FRIEND_TEST(OringDurationTrackerTest, TestPredictAnomalyTimestamp);
    FRIEND_TEST(OringDurationTrackerTest, TestAnomalyDetectionExpiredAlarm);
    FRIEND_TEST(OringDurationTrackerTest, TestAnomalyDetectionFiredAlarm);

    FRIEND_TEST(OringDurationTrackerTest_DimLimit, TestDimLimit);

    FRIEND_TEST(MaxDurationTrackerTest_DimLimit, TestDimLimit);

    FRIEND_TEST(ConfigUpdateTest, TestUpdateDurationMetrics);
    FRIEND_TEST(ConfigUpdateTest, TestUpdateAlerts);
};

}  // namespace statsd
}  // namespace os
}  // namespace android

#endif  // DURATION_TRACKER_H
