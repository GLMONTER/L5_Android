/*
 * Copyright (C) 2026 The Android Open Source Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * Librem 5 GNSS HAL (IGnss V3): the receiver on UART2 (DT "globaltop,pa6h", kernel gnss-mtk
 * driver) streams NMEA through /dev/gnss0, which powers it while open.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <aidl/android/hardware/gnss/BnGnss.h>

#include "Nmea.h"

namespace aidl::android::hardware::gnss::librem5 {

using ndk::ScopedAStatus;

class Gnss : public BnGnss {
  public:
    Gnss();
    ~Gnss();

    ScopedAStatus setCallback(const std::shared_ptr<IGnssCallback>& callback) override;
    ScopedAStatus close() override;
    ScopedAStatus getExtensionPsds(std::shared_ptr<IGnssPsds>* ret) override;
    ScopedAStatus getExtensionGnssConfiguration(std::shared_ptr<IGnssConfiguration>* ret) override;
    ScopedAStatus getExtensionGnssMeasurement(std::shared_ptr<IGnssMeasurementInterface>* ret) override;
    ScopedAStatus getExtensionGnssPowerIndication(std::shared_ptr<IGnssPowerIndication>* ret) override;
    ScopedAStatus getExtensionGnssBatching(std::shared_ptr<IGnssBatching>* ret) override;
    ScopedAStatus getExtensionGnssGeofence(std::shared_ptr<IGnssGeofence>* ret) override;
    ScopedAStatus getExtensionGnssNavigationMessage(
            std::shared_ptr<IGnssNavigationMessageInterface>* ret) override;
    ScopedAStatus getExtensionAGnss(std::shared_ptr<IAGnss>* ret) override;
    ScopedAStatus getExtensionAGnssRil(std::shared_ptr<IAGnssRil>* ret) override;
    ScopedAStatus getExtensionGnssDebug(std::shared_ptr<IGnssDebug>* ret) override;
    ScopedAStatus getExtensionGnssVisibilityControl(
            std::shared_ptr<visibility_control::IGnssVisibilityControl>* ret) override;
    ScopedAStatus start() override;
    ScopedAStatus stop() override;
    ScopedAStatus injectTime(int64_t timeMs, int64_t timeReferenceMs,
                             int32_t uncertaintyMs) override;
    ScopedAStatus injectLocation(const GnssLocation& location) override;
    ScopedAStatus injectBestLocation(const GnssLocation& location) override;
    ScopedAStatus deleteAidingData(GnssAidingData aidingDataFlags) override;
    ScopedAStatus setPositionMode(const PositionModeOptions& options) override;
    ScopedAStatus getExtensionGnssAntennaInfo(std::shared_ptr<IGnssAntennaInfo>* ret) override;
    ScopedAStatus getExtensionMeasurementCorrections(
            std::shared_ptr<measurement_corrections::IMeasurementCorrectionsInterface>* ret)
            override;
    ScopedAStatus startSvStatus() override;
    ScopedAStatus stopSvStatus() override;
    ScopedAStatus startNmea() override;
    ScopedAStatus stopNmea() override;

  private:
    void loop();
    bool openDevice();
    void closeDevice();
    void handleSentence(const std::string& sentence);
    std::shared_ptr<IGnssCallback> callback();

    std::mutex mLock;
    std::condition_variable mCv;
    std::shared_ptr<IGnssCallback> mCallback;
    int mFd = -1;
    bool mActive = false;  // a session is running (start() .. stop())
    // The receiver stays powered this long after stop(), to keep its almanac/ephemeris for a
    // quick restart
    std::chrono::steady_clock::time_point mPowerOffAt{};
    std::atomic<bool> mSvStatus{true};
    std::atomic<bool> mNmea{true};
    std::chrono::steady_clock::time_point mLastReport{};
    int32_t mMinIntervalMs = 1000;
    Nmea mParser;
    std::thread mThread;
    bool mExit = false;
};

}  // namespace aidl::android::hardware::gnss::librem5
