/*
 * Copyright (C) 2026 The Android Open Source Project
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

// Librem 5 (i.MX8MQ) performance floors driven by power hints:
//   INTERACTION boost -> CPU floor kInteractionCpuKhz + DRAM at max, for the hinted duration
//   LAUNCH mode       -> CPU floor at max + DRAM at max while the launch is in progress
//   LOW_POWER (battery saver) -> CPU capped at kInteractionCpuKhz; floors never exceed the cap
//   INTERACTIVE=false -> touch/launch floors released
// schedutil keeps scaling above the floor; the floor only removes ramp-up latency on touch.
// No camera streaming floor: the CPU debayer saturates schedutil and the DRAM devfreq governor by
// itself (measured: 34.5 ms/frame either way).

#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace aidl::android::hardware::power::impl::example {

class Booster {
  public:
    static Booster& getInstance();

    void interaction(int32_t durationMs);
    void setLaunch(bool enabled);
    void setInteractive(bool interactive);
    void setLowPower(bool enabled);

  private:
    using Clock = std::chrono::steady_clock;
    Booster();
    void loop();
    void applyLocked();

    std::mutex mLock;
    std::condition_variable mCv;
    std::thread mThread;

    Clock::time_point mInteractionUntil{};
    Clock::time_point mLaunchUntil{};
    bool mLaunch = false;
    bool mInteractive = true;
    bool mLowPower = false;

    // Discovered at startup
    std::string mCpuMinPath;     // cpufreq policy scaling_min_freq
    std::string mCpuMaxPath;     // cpufreq policy scaling_max_freq
    std::string mDdrMinPath;     // devfreq min_freq of the DRAM controller
    long mCpuMinKhz = 0, mCpuMaxKhz = 0, mCpuInteractionKhz = 0;
    long mDdrMinHz = 0, mDdrMaxHz = 0;
    long mAppliedCpu = -1, mAppliedDdr = -1, mAppliedCpuMax = -1;
};

}  // namespace aidl::android::hardware::power::impl::example
