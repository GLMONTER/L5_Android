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

// Librem 5 sensors backed by Linux IIO sysfs (polled):
//   LSM9DS1 accelerometer + gyroscope (lsm9ds1-imu_accel / lsm9ds1-imu_gyro),
//   VCNL4000 ambient light + proximity (vcnl4000).
// Raw values are scaled with in_<chan>_scale and rotated into the Android device frame with
// the IIO mount_matrix (IIO and Android share the same frame: X right, Y up, Z out of screen).

#pragma once

#include <array>
#include <string>

#include "Sensor.h"

namespace aidl {
namespace android {
namespace hardware {
namespace sensors {

class IioDevice {
  public:
    // Finds /sys/bus/iio/devices/iio:deviceN whose "name" matches.
    explicit IioDevice(const std::string& name);

    bool found() const { return !mPath.empty(); }
    bool readDouble(const std::string& attr, double* out) const;
    bool writeString(const std::string& attr, const std::string& value) const;
    std::string readString(const std::string& attr) const;
    // Sets sampling_frequency to the lowest available rate >= hz (or the highest available).
    void setSamplingFrequencyAtLeast(double hz) const;
    // Row-major 3x3 matrix: ro.vendor.sensors.<name>.mount_matrix if set (same "a, b, c; d, e, f;
    // g, h, i" syntax as IIO), else the IIO mount_matrix attribute, else identity.
    std::array<double, 9> mountMatrix() const;

  private:
    std::string mName;
    std::string mPath;
};

// 3-axis sensor read from in_<chan>_{x,y,z}_raw * in_<chan>_scale.
class IioVec3Sensor : public Sensor {
  public:
    IioVec3Sensor(int32_t sensorHandle, ISensorsEventCallback* callback, const char* iioName,
                  const char* channel, SensorType type, const char* name, float maxRange,
                  double deviceRateHz);

  protected:
    bool readVec3(double out[3]);
    void readEventPayload(EventPayload& payload) override;

    IioDevice mDevice;
    std::string mChannel;
    std::array<double, 9> mMount;
};

class IioAccelSensor : public IioVec3Sensor {
  public:
    IioAccelSensor(int32_t sensorHandle, ISensorsEventCallback* callback);
};

// The LSM9DS1 gyro has a noticeable zero-rate offset; track it while the device is still.
class IioGyroSensor : public IioVec3Sensor {
  public:
    IioGyroSensor(int32_t sensorHandle, ISensorsEventCallback* callback);

  protected:
    void readEventPayload(EventPayload& payload) override;

  private:
    double mBias[3] = {0, 0, 0};
    double mMean[3] = {0, 0, 0};
    bool mHaveMean = false;
    int mStillSamples = 0;
};

class IioLightSensor : public OnChangeSensor {
  public:
    IioLightSensor(int32_t sensorHandle, ISensorsEventCallback* callback);

  protected:
    void readEventPayload(EventPayload& payload) override;

  private:
    IioDevice mDevice;
};

class IioProximitySensor : public OnChangeSensor {
  public:
    IioProximitySensor(int32_t sensorHandle, ISensorsEventCallback* callback);

  protected:
    void readEventPayload(EventPayload& payload) override;

  private:
    IioDevice mDevice;
    double mNearLevel = 0;
};

}  // namespace sensors
}  // namespace hardware
}  // namespace android
}  // namespace aidl
