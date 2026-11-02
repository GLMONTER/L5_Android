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

#define LOG_TAG "PowerHalLibrem5"

#include "Booster.h"

#include <algorithm>
#include <sstream>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

namespace aidl::android::hardware::power::impl::example {

using ::android::base::ReadFileToString;
using ::android::base::Split;
using ::android::base::Trim;
using ::android::base::WriteStringToFile;

namespace {
constexpr char kCpuPolicy[] = "/sys/devices/system/cpu/cpufreq/policy0";
constexpr char kDdrDevfreq[] = "/sys/class/devfreq/3d400000.memory-controller";
constexpr long kInteractionCpuKhz = 1000000;  // 1.0 GHz: enough for first frames after a touch
constexpr int32_t kMinInteractionMs = 100;
constexpr int32_t kMaxInteractionMs = 1000;
constexpr auto kMaxLaunch = std::chrono::seconds(5);

std::vector<long> readList(const std::string& path) {
    std::string s;
    std::vector<long> out;
    if (!ReadFileToString(path, &s)) return out;
    for (const auto& t : Split(Trim(s), " ")) {
        if (!t.empty()) out.push_back(strtol(t.c_str(), nullptr, 10));
    }
    std::sort(out.begin(), out.end());
    return out;
}
}  // namespace

Booster& Booster::getInstance() {
    static Booster instance;
    return instance;
}

Booster::Booster() {
    const std::string cpu = kCpuPolicy;
    auto cpuFreqs = readList(cpu + "/scaling_available_frequencies");
    if (!cpuFreqs.empty()) {
        mCpuMinPath = cpu + "/scaling_min_freq";
        mCpuMinKhz = cpuFreqs.front();
        mCpuMaxKhz = cpuFreqs.back();
        mCpuInteractionKhz = mCpuMaxKhz;
        for (long f : cpuFreqs) {
            if (f >= kInteractionCpuKhz) {
                mCpuInteractionKhz = f;
                break;
            }
        }
    }
    const std::string ddr = kDdrDevfreq;
    auto ddrFreqs = readList(ddr + "/available_frequencies");
    if (!ddrFreqs.empty()) {
        mDdrMinPath = ddr + "/min_freq";
        mDdrMinHz = ddrFreqs.front();
        mDdrMaxHz = ddrFreqs.back();
    }
    LOG(INFO) << "cpu " << mCpuMinKhz << "-" << mCpuMaxKhz << " kHz (touch floor "
              << mCpuInteractionKhz << "), ddr " << mDdrMinHz << "-" << mDdrMaxHz << " Hz";
    mThread = std::thread(&Booster::loop, this);
}

void Booster::interaction(int32_t durationMs) {
    durationMs = std::clamp(durationMs, kMinInteractionMs, kMaxInteractionMs);
    std::lock_guard l(mLock);
    if (!mInteractive) return;
    mInteractionUntil = std::max(mInteractionUntil,
                                 Clock::now() + std::chrono::milliseconds(durationMs));
    applyLocked();
    mCv.notify_all();
}

void Booster::setLaunch(bool enabled) {
    std::lock_guard l(mLock);
    mLaunch = enabled && mInteractive;
    if (mLaunch) mLaunchUntil = Clock::now() + kMaxLaunch;
    applyLocked();
    mCv.notify_all();
}

void Booster::setInteractive(bool interactive) {
    std::lock_guard l(mLock);
    mInteractive = interactive;
    if (!interactive) {
        mLaunch = false;
        mInteractionUntil = {};
    }
    applyLocked();
    mCv.notify_all();
}

void Booster::applyLocked() {
    const auto now = Clock::now();
    const bool launch = mLaunch && now < mLaunchUntil;
    const bool touch = now < mInteractionUntil;
    long cpu = launch ? mCpuMaxKhz : touch ? mCpuInteractionKhz : mCpuMinKhz;
    long ddr = (launch || touch) ? mDdrMaxHz : mDdrMinHz;
    if (!mCpuMinPath.empty() && cpu != mAppliedCpu) {
        if (WriteStringToFile(std::to_string(cpu), mCpuMinPath)) {
            mAppliedCpu = cpu;
        } else {
            PLOG(WARNING) << "write " << mCpuMinPath;
        }
    }
    if (!mDdrMinPath.empty() && ddr != mAppliedDdr) {
        if (WriteStringToFile(std::to_string(ddr), mDdrMinPath)) {
            mAppliedDdr = ddr;
        } else {
            PLOG(WARNING) << "write " << mDdrMinPath;
        }
    }
}

void Booster::loop() {
    std::unique_lock l(mLock);
    while (true) {
        // Wake at the next expiry (or idle until a new hint arrives).
        auto next = Clock::time_point::max();
        const auto now = Clock::now();
        if (mInteractionUntil > now) next = std::min(next, mInteractionUntil);
        if (mLaunch && mLaunchUntil > now) next = std::min(next, mLaunchUntil);
        if (next == Clock::time_point::max()) {
            mCv.wait(l);
        } else {
            mCv.wait_until(l, next);
        }
        applyLocked();
    }
}

}  // namespace aidl::android::hardware::power::impl::example
