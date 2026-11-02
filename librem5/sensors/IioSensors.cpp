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

#define LOG_TAG "SensorsHalLibrem5"

#include "IioSensors.h"

#include <dirent.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>

#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <log/log.h>

namespace aidl {
namespace android {
namespace hardware {
namespace sensors {

using ::android::base::ReadFileToString;
using ::android::base::Split;
using ::android::base::Trim;
using ::android::base::WriteStringToFile;

namespace {
constexpr char kIioDevices[] = "/sys/bus/iio/devices";
constexpr int32_t kMaxDelayUs = 1000 * 1000;
}  // namespace

// ---- IioDevice ----

IioDevice::IioDevice(const std::string& name) : mName(name) {
    DIR* dir = opendir(kIioDevices);
    if (dir == nullptr) return;
    while (struct dirent* de = readdir(dir)) {
        std::string entry = de->d_name;
        if (entry.rfind("iio:device", 0) != 0) continue;
        std::string devName;
        std::string path = std::string(kIioDevices) + "/" + entry;
        if (ReadFileToString(path + "/name", &devName) && Trim(devName) == name) {
            mPath = path;
            break;
        }
    }
    closedir(dir);
    if (mPath.empty()) {
        ALOGE("IIO device '%s' not found", name.c_str());
    } else {
        ALOGI("IIO device '%s' at %s", name.c_str(), mPath.c_str());
    }
}

std::string IioDevice::readString(const std::string& attr) const {
    std::string value;
    if (mPath.empty() || !ReadFileToString(mPath + "/" + attr, &value)) return "";
    return Trim(value);
}

bool IioDevice::readDouble(const std::string& attr, double* out) const {
    std::string value = readString(attr);
    if (value.empty()) return false;
    char* end = nullptr;
    double d = strtod(value.c_str(), &end);
    if (end == value.c_str()) return false;
    *out = d;
    return true;
}

bool IioDevice::writeString(const std::string& attr, const std::string& value) const {
    if (mPath.empty()) return false;
    return WriteStringToFile(value, mPath + "/" + attr);
}

void IioDevice::setSamplingFrequencyAtLeast(double hz) const {
    std::vector<double> rates;
    for (const auto& s : Split(readString("sampling_frequency_available"), " ")) {
        if (!s.empty()) rates.push_back(strtod(s.c_str(), nullptr));
    }
    if (rates.empty()) return;
    double chosen = rates.back();
    for (double r : rates) {
        if (r >= hz) {
            chosen = r;
            break;
        }
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%g", chosen);
    if (!writeString("sampling_frequency", buf)) {
        ALOGW("%s: could not set sampling_frequency=%s", mPath.c_str(), buf);
    }
}

std::array<double, 9> IioDevice::mountMatrix() const {
    std::array<double, 9> m = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    // Some device trees carry a wrong matrix (the Librem 5's LSM9DS1 one is a reflection,
    // det = -1, missing the X flip), so allow a per-device override.
    std::string text = ::android::base::GetProperty("ro.vendor.sensors." + mName + ".mount_matrix", "");
    const bool overridden = !text.empty();
    if (!overridden) text = readString("mount_matrix");
    if (text.empty()) return m;
    std::vector<double> values;
    for (auto& row : Split(text, ";")) {
        for (auto& v : Split(row, ",")) {
            std::string t = Trim(v);
            if (!t.empty()) values.push_back(strtod(t.c_str(), nullptr));
        }
    }
    if (values.size() == 9) {
        for (int i = 0; i < 9; i++) m[i] = values[i];
    }
    const double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                       m[2] * (m[3] * m[7] - m[4] * m[6]);
    ALOGI("%s: mount matrix (%s) [%s] det=%g%s", mName.c_str(), overridden ? "property" : "iio",
          text.c_str(), det, det < 0 ? " -- reflection, axes will be mirrored!" : "");
    return m;
}

// ---- IioVec3Sensor ----

IioVec3Sensor::IioVec3Sensor(int32_t sensorHandle, ISensorsEventCallback* callback,
                             const char* iioName, const char* channel, SensorType type,
                             const char* name, float maxRange, double deviceRateHz)
    : Sensor(callback), mDevice(iioName), mChannel(channel) {
    mMount = mDevice.mountMatrix();
    mDevice.setSamplingFrequencyAtLeast(deviceRateHz);
    double scale = 0;
    mDevice.readDouble("in_" + mChannel + "_scale", &scale);

    mSensorInfo.sensorHandle = sensorHandle;
    mSensorInfo.name = name;
    mSensorInfo.vendor = "STMicroelectronics";
    mSensorInfo.version = 1;
    mSensorInfo.type = type;
    mSensorInfo.typeAsString = "";
    mSensorInfo.maxRange = maxRange;
    mSensorInfo.resolution = scale > 0 ? static_cast<float>(scale) : 0.001f;
    mSensorInfo.power = 0.6f;            // mA (LSM9DS1 IMU, accel+gyro)
    // Poll at up to 50 Hz. Accel and gyro share one ODR on the LSM9DS1; at 119 Hz and above the
    // gyro x/y readings tear (+/-1.5 rad/s garbage at rest), so the chip runs at 50-59.5 Hz.
    mSensorInfo.minDelayUs = 20 * 1000;
    mSensorInfo.maxDelayUs = kMaxDelayUs;
    mSensorInfo.fifoReservedEventCount = 0;
    mSensorInfo.fifoMaxEventCount = 0;
    mSensorInfo.requiredPermission = "";
    mSensorInfo.flags = 0;  // continuous
}

bool IioVec3Sensor::readVec3(double out[3]) {
    static const char* kAxes[3] = {"x", "y", "z"};
    double scale = 0;
    if (!mDevice.readDouble("in_" + mChannel + "_scale", &scale)) return false;
    double raw[3];
    for (int i = 0; i < 3; i++) {
        if (!mDevice.readDouble("in_" + mChannel + "_" + kAxes[i] + "_raw", &raw[i])) return false;
        raw[i] *= scale;
    }
    for (int r = 0; r < 3; r++) {
        out[r] = mMount[r * 3 + 0] * raw[0] + mMount[r * 3 + 1] * raw[1] +
                 mMount[r * 3 + 2] * raw[2];
    }
    return true;
}

void IioVec3Sensor::readEventPayload(EventPayload& payload) {
    double v[3] = {0, 0, 0};
    SensorStatus status = readVec3(v) ? SensorStatus::ACCURACY_HIGH : SensorStatus::UNRELIABLE;
    EventPayload::Vec3 vec3 = {
            .x = static_cast<float>(v[0]),
            .y = static_cast<float>(v[1]),
            .z = static_cast<float>(v[2]),
            .status = status,
    };
    payload.set<EventPayload::Tag::vec3>(vec3);
}

IioAccelSensor::IioAccelSensor(int32_t sensorHandle, ISensorsEventCallback* callback)
    : IioVec3Sensor(sensorHandle, callback, "lsm9ds1-imu_accel", "accel",
                    SensorType::ACCELEROMETER, "LSM9DS1 Accelerometer", 19.6f /* +/-2g */,
                    50) {}

// ---- Gyro with zero-rate offset tracking ----

IioGyroSensor::IioGyroSensor(int32_t sensorHandle, ISensorsEventCallback* callback)
    : IioVec3Sensor(sensorHandle, callback, "lsm9ds1-imu_gyro", "anglvel", SensorType::GYROSCOPE,
                    "LSM9DS1 Gyroscope", 5.0f /* rad/s */, 59.5) {}

void IioGyroSensor::readEventPayload(EventPayload& payload) {
    constexpr double kMeanAlpha = 0.1;     // smoothing of the running mean
    constexpr double kStillLimit = 0.03;   // rad/s deviation from the mean counted as "still"
    constexpr int kStillNeeded = 25;       // ~0.5 s at 50 Hz before trusting the mean as bias
    constexpr double kBiasAlpha = 0.02;    // slow bias update once converged

    double w[3] = {0, 0, 0};
    bool ok = readVec3(w);
    if (ok) {
        bool still = true;
        for (int i = 0; i < 3; i++) {
            if (!mHaveMean) mMean[i] = w[i];
            if (std::fabs(w[i] - mMean[i]) > kStillLimit) still = false;
            mMean[i] += kMeanAlpha * (w[i] - mMean[i]);
        }
        mHaveMean = true;
        mStillSamples = still ? mStillSamples + 1 : 0;
        if (mStillSamples == kStillNeeded) {
            // First stretch of stillness after (re)activation: take the mean directly.
            for (int i = 0; i < 3; i++) mBias[i] = mMean[i];
        } else if (mStillSamples > kStillNeeded) {
            for (int i = 0; i < 3; i++) mBias[i] += kBiasAlpha * (mMean[i] - mBias[i]);
        }
    }
    EventPayload::Vec3 vec3 = {
            .x = static_cast<float>(w[0] - mBias[0]),
            .y = static_cast<float>(w[1] - mBias[1]),
            .z = static_cast<float>(w[2] - mBias[2]),
            .status = ok ? SensorStatus::ACCURACY_MEDIUM : SensorStatus::UNRELIABLE,
    };
    payload.set<EventPayload::Tag::vec3>(vec3);
}

// ---- VCNL4000 light ----

IioLightSensor::IioLightSensor(int32_t sensorHandle, ISensorsEventCallback* callback)
    : OnChangeSensor(callback), mDevice("vcnl4000") {
    // The VCNL4040 sits behind dark glass and reads 0 at the default 80 ms in room light; use the
    // longest integration time (most counts per lux). in_illuminance_scale follows automatically.
    std::string times = mDevice.readString("in_illuminance_integration_time_available");
    if (size_t pos = times.find_last_not_of(" "); pos != std::string::npos) {
        std::string longest = times.substr(times.find_last_of(" ", pos) + 1);
        if (!mDevice.writeString("in_illuminance_integration_time", longest)) {
            ALOGW("could not set light integration time %s", longest.c_str());
        }
    }
    mSensorInfo.sensorHandle = sensorHandle;
    mSensorInfo.name = "VCNL4040 Ambient Light";
    mSensorInfo.vendor = "Vishay";
    mSensorInfo.version = 1;
    mSensorInfo.type = SensorType::LIGHT;
    mSensorInfo.typeAsString = "";
    mSensorInfo.maxRange = 6553.5f;  // 16-bit count * 0.1 lux
    mSensorInfo.resolution = 0.1f;
    mSensorInfo.power = 0.25f;
    mSensorInfo.minDelayUs = 700 * 1000;  // >= one 640 ms integration
    mSensorInfo.maxDelayUs = kMaxDelayUs;
    mSensorInfo.fifoReservedEventCount = 0;
    mSensorInfo.fifoMaxEventCount = 0;
    mSensorInfo.requiredPermission = "";
    mSensorInfo.flags = static_cast<uint32_t>(SensorInfo::SENSOR_FLAG_BITS_ON_CHANGE_MODE);
}

void IioLightSensor::readEventPayload(EventPayload& payload) {
    double raw = 0, scale = 1;
    mDevice.readDouble("in_illuminance_raw", &raw);
    mDevice.readDouble("in_illuminance_scale", &scale);
    // Behind the dark glass a lit room is only a few lux, so keep 0.1 lux resolution; rounding
    // still stops sensor noise from turning every poll into an on-change event.
    payload.set<EventPayload::Tag::scalar>(static_cast<float>(std::round(raw * scale * 10) / 10));
}

// ---- VCNL4000 proximity (binary near/far) ----

IioProximitySensor::IioProximitySensor(int32_t sensorHandle, ISensorsEventCallback* callback)
    : OnChangeSensor(callback), mDevice("vcnl4000") {
    if (!mDevice.readDouble("in_proximity_nearlevel", &mNearLevel) || mNearLevel <= 0) {
        mNearLevel = 7;
    }
    ALOGI("proximity near level %g", mNearLevel);
    mSensorInfo.sensorHandle = sensorHandle;
    mSensorInfo.name = "VCNL4040 Proximity";
    mSensorInfo.vendor = "Vishay";
    mSensorInfo.version = 1;
    mSensorInfo.type = SensorType::PROXIMITY;
    mSensorInfo.typeAsString = "";
    mSensorInfo.maxRange = 5.0f;  // cm; binary sensor reports 0 (near) or maxRange (far)
    mSensorInfo.resolution = 5.0f;
    mSensorInfo.power = 0.25f;
    mSensorInfo.minDelayUs = 100 * 1000;
    mSensorInfo.maxDelayUs = kMaxDelayUs;
    mSensorInfo.fifoReservedEventCount = 0;
    mSensorInfo.fifoMaxEventCount = 0;
    mSensorInfo.requiredPermission = "";
    mSensorInfo.flags = static_cast<uint32_t>(SensorInfo::SENSOR_FLAG_BITS_ON_CHANGE_MODE |
                                              SensorInfo::SENSOR_FLAG_BITS_WAKE_UP);
}

void IioProximitySensor::readEventPayload(EventPayload& payload) {
    double raw = 0;
    mDevice.readDouble("in_proximity_raw", &raw);
    payload.set<EventPayload::Tag::scalar>(raw >= mNearLevel ? 0.0f : mSensorInfo.maxRange);
}

}  // namespace sensors
}  // namespace hardware
}  // namespace android
}  // namespace aidl
