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

// Librem 5 lights HAL: the single RGB status LED (leds-pwm-multicolor "rgb:status") serves the
// battery, notification and attention lights; the highest-priority lit one wins
// (attention > notifications > battery). TIMED flash mode blinks it from a thread.
// The panel backlight is not here: Android 17 sets it through the composer.

#define LOG_TAG "LightsHalLibrem5"

#include <aidl/android/hardware/light/BnLights.h>
#include <android-base/file.h>
#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

using aidl::android::hardware::light::BnLights;
using aidl::android::hardware::light::FlashMode;
using aidl::android::hardware::light::HwLight;
using aidl::android::hardware::light::HwLightState;
using aidl::android::hardware::light::LightType;
using android::base::ReadFileToString;
using android::base::WriteStringToFile;
using ndk::ScopedAStatus;

namespace {

constexpr char kLed[] = "/sys/class/leds/rgb:status";

// Light ids double as priority: higher id wins.
enum Id : int32_t { kBattery = 0, kNotifications = 1, kAttention = 2, kCount = 3 };

class Lights : public BnLights {
  public:
    Lights() {
        std::string max;
        if (ReadFileToString(std::string(kLed) + "/max_brightness", &max)) {
            mMax = std::max(1, atoi(max.c_str()));
        }
        // multi_index tells the channel order, e.g. "blue green red".
        std::string index;
        ReadFileToString(std::string(kLed) + "/multi_index", &index);
        int slot = 0;
        size_t pos = 0;
        while (slot < 3 && pos < index.size()) {
            size_t end = index.find_first_of(" \n", pos);
            std::string ch = index.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            if (ch == "red") mOrder[slot] = 0;
            if (ch == "green") mOrder[slot] = 1;
            if (ch == "blue") mOrder[slot] = 2;
            slot++;
            if (end == std::string::npos) break;
            pos = end + 1;
        }
        LOG(INFO) << "RGB status LED, max brightness " << mMax;
        mThread = std::thread(&Lights::loop, this);
    }

    ScopedAStatus setLightState(int32_t id, const HwLightState& state) override {
        if (id < 0 || id >= kCount) {
            return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        }
        std::lock_guard l(mLock);
        mStates[id] = state;
        mPhase = true;
        mCv.notify_all();
        return ScopedAStatus::ok();
    }

    ScopedAStatus getLights(std::vector<HwLight>* lights) override {
        lights->push_back({.id = kBattery, .ordinal = 0, .type = LightType::BATTERY});
        lights->push_back({.id = kNotifications, .ordinal = 0, .type = LightType::NOTIFICATIONS});
        lights->push_back({.id = kAttention, .ordinal = 0, .type = LightType::ATTENTION});
        return ScopedAStatus::ok();
    }

  private:
    // The lit state with the highest priority, or nullptr if everything is off.
    const HwLightState* activeLocked() const {
        for (int id = kCount - 1; id >= 0; id--) {
            if (mStates[id].color & 0x00ffffff) return &mStates[id];
        }
        return nullptr;
    }

    void write(uint32_t argb) {
        const int rgb[3] = {static_cast<int>((argb >> 16) & 0xff),
                            static_cast<int>((argb >> 8) & 0xff), static_cast<int>(argb & 0xff)};
        char intensity[48];
        int v[3];
        for (int slot = 0; slot < 3; slot++) v[slot] = rgb[mOrder[slot]] * mMax / 255;
        snprintf(intensity, sizeof(intensity), "%d %d %d", v[0], v[1], v[2]);
        const bool on = (argb & 0x00ffffff) != 0;
        if (on) WriteStringToFile(intensity, std::string(kLed) + "/multi_intensity");
        WriteStringToFile(on ? std::to_string(mMax) : "0", std::string(kLed) + "/brightness");
    }

    void loop() {
        std::unique_lock l(mLock);
        uint32_t written = 0xffffffff;
        while (true) {
            const HwLightState* s = activeLocked();
            uint32_t color = s ? static_cast<uint32_t>(s->color) : 0;
            const bool blink = s && s->flashMode == FlashMode::TIMED && s->flashOnMs > 0 &&
                               s->flashOffMs > 0;
            uint32_t now = blink && !mPhase ? 0 : color;
            if (now != written) {
                write(now);
                written = now;
            }
            if (blink) {
                auto ms = std::chrono::milliseconds(mPhase ? s->flashOnMs : s->flashOffMs);
                if (mCv.wait_for(l, ms) == std::cv_status::timeout) mPhase = !mPhase;
            } else {
                mCv.wait(l);
            }
        }
    }

    std::mutex mLock;
    std::condition_variable mCv;
    std::thread mThread;
    std::array<HwLightState, kCount> mStates{};
    bool mPhase = true;
    int mMax = 255;
    int mOrder[3] = {2, 1, 0};  // multi_intensity slot -> 0=R,1=G,2=B (default "blue green red")
};

}  // namespace

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    auto lights = ndk::SharedRefBase::make<Lights>();
    const std::string instance = std::string(Lights::descriptor) + "/default";
    CHECK_EQ(AServiceManager_addService(lights->asBinder().get(), instance.c_str()), STATUS_OK);
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
