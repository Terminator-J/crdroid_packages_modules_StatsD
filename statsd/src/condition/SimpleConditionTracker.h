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

#ifndef SIMPLE_CONDITION_TRACKER_H
#define SIMPLE_CONDITION_TRACKER_H

#include <gtest/gtest_prod.h>
#include "ConditionTracker.h"
#include "config/ConfigKey.h"
#include "src/statsd_config.pb.h"
#include "stats_util.h"

namespace android {
namespace os {
namespace statsd {

class SimpleConditionTracker : public ConditionTracker {
public:
    SimpleConditionTracker(const ConfigKey& key, int64_t id, const uint64_t protoHash,
                           const int index, const SimplePredicate& simplePredicate,
                           const std::unordered_map<int64_t, int>& atomMatchingTrackerMap);

    ~SimpleConditionTracker();

    optional<InvalidConfigReason> init(
            const std::vector<Predicate>& allConditionConfig,
            const std::vector<sp<ConditionTracker>>& allConditionTrackers,
            const std::unordered_map<int64_t, int>& conditionIdIndexMap,
            std::vector<uint8_t>& stack, std::vector<ConditionState>& conditionCache) override;

    optional<InvalidConfigReason> onConfigUpdated(
            const std::vector<Predicate>& allConditionProtos, int index,
            const std::vector<sp<ConditionTracker>>& allConditionTrackers,
            const std::unordered_map<int64_t, int>& atomMatchingTrackerMap,
            const std::unordered_map<int64_t, int>& conditionTrackerMap) override;

    void evaluateCondition(const LogEvent& event,
                           const std::vector<MatchingState>& eventMatcherValues,
                           const std::vector<sp<ConditionTracker>>& mAllConditions,
                           std::vector<ConditionState>& conditionCache,
                           std::vector<uint8_t>& changedCache) override;

    void isConditionMet(const ConditionKey& conditionParameters,
                        const std::vector<sp<ConditionTracker>>& allConditions,
                        const bool isPartialLink,
                        std::vector<ConditionState>& conditionCache) const override;

    virtual const std::set<HashableDimensionKey>* getChangedToTrueDimensions(
            const std::vector<sp<ConditionTracker>>& allConditions) const {
        if (mSliced) {
            return &mLastChangedToTrueDimensions;
        } else {
            return nullptr;
        }
    }

    virtual const std::set<HashableDimensionKey>* getChangedToFalseDimensions(
            const std::vector<sp<ConditionTracker>>& allConditions) const {
        if (mSliced) {
            return &mLastChangedToFalseDimensions;
        } else {
            return nullptr;
        }
    }

    const std::map<HashableDimensionKey, int>* getSlicedDimensionMap(
            const std::vector<sp<ConditionTracker>>& allConditions) const override {
        return &mSlicedConditionState;
    }

    bool IsChangedDimensionTrackable() const  override { return true; }

    bool IsSimpleCondition() const  override { return true; }

    bool equalOutputDimensions(
        const std::vector<sp<ConditionTracker>>& allConditions,
        const vector<Matcher>& dimensions) const override {
            return equalDimensions(mOutputDimensions, dimensions);
    }

private:
    const ConfigKey mConfigKey;
    // The index of the LogEventMatcher which defines the start.
    int mStartLogMatcherIndex;

    // The index of the LogEventMatcher which defines the end.
    int mStopLogMatcherIndex;

    // if the start end needs to be nested.
    bool mCountNesting;

    // The index of the LogEventMatcher which defines the stop all.
    int mStopAllLogMatcherIndex;

    ConditionState mInitialValue;

    std::vector<Matcher> mOutputDimensions;

    bool mContainANYPositionInInternalDimensions;

    std::set<HashableDimensionKey> mLastChangedToTrueDimensions;
    std::set<HashableDimensionKey> mLastChangedToFalseDimensions;

    std::map<HashableDimensionKey, int> mSlicedConditionState;

    void setMatcherIndices(const SimplePredicate& predicate,
                           const std::unordered_map<int64_t, int>& logTrackerMap);

    void handleStopAll(std::vector<ConditionState>& conditionCache,
                       std::vector<uint8_t>& changedCache);

    void handleConditionEvent(const HashableDimensionKey& outputKey, bool matchStart,
                              ConditionState* conditionCache, bool* changedCache);

    bool hitGuardRail(const HashableDimensionKey& newKey) const;

    void dumpState();

    FRIEND_TEST(SimpleConditionTrackerTest, TestSlicedCondition);
    FRIEND_TEST(SimpleConditionTrackerTest, TestSlicedWithNoOutputDim);
    FRIEND_TEST(SimpleConditionTrackerTest, TestStopAll);
    FRIEND_TEST(SimpleConditionTrackerTest, TestGuardrailNotHitWhenDefaultFalse);
    FRIEND_TEST(SimpleConditionTrackerTest, TestGuardrailHitWhenDefaultUnknown);
    FRIEND_TEST(ConfigUpdateTest, TestUpdateConditions);
};

}  // namespace statsd
}  // namespace os
}  // namespace android

#endif  // SIMPLE_CONDITION_TRACKER_H
