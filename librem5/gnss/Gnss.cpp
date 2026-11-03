/*
 * Copyright (C) 2026 The Android Open Source Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "GnssLibrem5"

#include "Gnss.h"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <android-base/logging.h>
#include <utils/SystemClock.h>

#include "Extensions.h"

namespace aidl::android::hardware::gnss::librem5 {

namespace {
constexpr char kDevice[] = "/dev/gnss0";
constexpr auto kKeepPowered = std::chrono::seconds(60);
}  // namespace

Gnss::Gnss() {
    mThread = std::thread(&Gnss::loop, this);
}

Gnss::~Gnss() {
    {
        std::lock_guard l(mLock);
        mExit = true;
    }
    mCv.notify_all();
    if (mThread.joinable()) mThread.join();
}

std::shared_ptr<IGnssCallback> Gnss::callback() {
    std::lock_guard l(mLock);
    return mCallback;
}

ScopedAStatus Gnss::setCallback(const std::shared_ptr<IGnssCallback>& callback) {
    {
        std::lock_guard l(mLock);
        mCallback = callback;
    }
    if (!callback) return ScopedAStatus::ok();

    // A standalone receiver: no assistance, measurements or geofencing
    callback->gnssSetCapabilitiesCb(0);
    IGnssCallback::GnssSystemInfo info;
    info.yearOfHw = 2020;
    info.name = "Librem 5 GNSS (NMEA)";
    callback->gnssSetSystemInfoCb(info);
    return ScopedAStatus::ok();
}

ScopedAStatus Gnss::close() {
    std::lock_guard l(mLock);
    mCallback = nullptr;
    mActive = false;
    mPowerOffAt = std::chrono::steady_clock::now();
    mCv.notify_all();
    return ScopedAStatus::ok();
}

ScopedAStatus Gnss::start() {
    LOG(INFO) << "start";
    {
        std::lock_guard l(mLock);
        mActive = true;
    }
    mCv.notify_all();
    if (auto cb = callback()) {
        cb->gnssStatusCb(IGnssCallback::GnssStatusValue::SESSION_BEGIN);
        cb->gnssStatusCb(IGnssCallback::GnssStatusValue::ENGINE_ON);
    }
    return ScopedAStatus::ok();
}

ScopedAStatus Gnss::stop() {
    LOG(INFO) << "stop";
    {
        std::lock_guard l(mLock);
        mActive = false;
        mPowerOffAt = std::chrono::steady_clock::now() + kKeepPowered;
    }
    mCv.notify_all();
    if (auto cb = callback()) {
        cb->gnssStatusCb(IGnssCallback::GnssStatusValue::SESSION_END);
        cb->gnssStatusCb(IGnssCallback::GnssStatusValue::ENGINE_OFF);
    }
    return ScopedAStatus::ok();
}

ScopedAStatus Gnss::setPositionMode(const PositionModeOptions& options) {
    std::lock_guard l(mLock);
    mMinIntervalMs = std::max(options.minIntervalMs, 1000);
    return ScopedAStatus::ok();
}

ScopedAStatus Gnss::startSvStatus() { mSvStatus = true; return ScopedAStatus::ok(); }
ScopedAStatus Gnss::stopSvStatus() { mSvStatus = false; return ScopedAStatus::ok(); }
ScopedAStatus Gnss::startNmea() { mNmea = true; return ScopedAStatus::ok(); }
ScopedAStatus Gnss::stopNmea() { mNmea = false; return ScopedAStatus::ok(); }

// The receiver keeps its own time and has no assistance input
ScopedAStatus Gnss::injectTime(int64_t, int64_t, int32_t) { return ScopedAStatus::ok(); }
ScopedAStatus Gnss::injectLocation(const GnssLocation&) { return ScopedAStatus::ok(); }
ScopedAStatus Gnss::injectBestLocation(const GnssLocation&) { return ScopedAStatus::ok(); }
ScopedAStatus Gnss::deleteAidingData(GnssAidingData) { return ScopedAStatus::ok(); }

ScopedAStatus Gnss::getExtensionPsds(std::shared_ptr<IGnssPsds>* ret) {
    *ret = ndk::SharedRefBase::make<GnssPsds>();
    return ScopedAStatus::ok();
}
ScopedAStatus Gnss::getExtensionGnssConfiguration(std::shared_ptr<IGnssConfiguration>* ret) {
    *ret = ndk::SharedRefBase::make<GnssConfiguration>();
    return ScopedAStatus::ok();
}
ScopedAStatus Gnss::getExtensionGnssMeasurement(std::shared_ptr<IGnssMeasurementInterface>* ret) {
    *ret = ndk::SharedRefBase::make<GnssMeasurementInterface>();
    return ScopedAStatus::ok();
}
ScopedAStatus Gnss::getExtensionGnssPowerIndication(std::shared_ptr<IGnssPowerIndication>* ret) {
    *ret = ndk::SharedRefBase::make<GnssPowerIndication>();
    return ScopedAStatus::ok();
}
ScopedAStatus Gnss::getExtensionGnssBatching(std::shared_ptr<IGnssBatching>* ret) {
    *ret = ndk::SharedRefBase::make<GnssBatching>();
    return ScopedAStatus::ok();
}
ScopedAStatus Gnss::getExtensionGnssGeofence(std::shared_ptr<IGnssGeofence>* ret) {
    *ret = ndk::SharedRefBase::make<GnssGeofence>();
    return ScopedAStatus::ok();
}
ScopedAStatus Gnss::getExtensionGnssNavigationMessage(
        std::shared_ptr<IGnssNavigationMessageInterface>* ret) {
    *ret = ndk::SharedRefBase::make<GnssNavigationMessageInterface>();
    return ScopedAStatus::ok();
}
ScopedAStatus Gnss::getExtensionAGnss(std::shared_ptr<IAGnss>* ret) {
    *ret = ndk::SharedRefBase::make<AGnss>();
    return ScopedAStatus::ok();
}
ScopedAStatus Gnss::getExtensionAGnssRil(std::shared_ptr<IAGnssRil>* ret) {
    *ret = ndk::SharedRefBase::make<AGnssRil>();
    return ScopedAStatus::ok();
}
ScopedAStatus Gnss::getExtensionGnssDebug(std::shared_ptr<IGnssDebug>* ret) {
    *ret = ndk::SharedRefBase::make<GnssDebug>();
    return ScopedAStatus::ok();
}
ScopedAStatus Gnss::getExtensionGnssVisibilityControl(
        std::shared_ptr<visibility_control::IGnssVisibilityControl>* ret) {
    *ret = ndk::SharedRefBase::make<GnssVisibilityControl>();
    return ScopedAStatus::ok();
}
ScopedAStatus Gnss::getExtensionGnssAntennaInfo(std::shared_ptr<IGnssAntennaInfo>* ret) {
    *ret = ndk::SharedRefBase::make<GnssAntennaInfo>();
    return ScopedAStatus::ok();
}
ScopedAStatus Gnss::getExtensionMeasurementCorrections(
        std::shared_ptr<measurement_corrections::IMeasurementCorrectionsInterface>* ret) {
    *ret = ndk::SharedRefBase::make<MeasurementCorrectionsInterface>();
    return ScopedAStatus::ok();
}

bool Gnss::openDevice() {
    mFd = open(kDevice, O_RDONLY | O_NOCTTY | O_CLOEXEC);
    if (mFd < 0) {
        PLOG(ERROR) << "open " << kDevice;
        return false;
    }
    LOG(INFO) << "receiver powered on";
    mParser = Nmea();
    return true;
}

void Gnss::closeDevice() {
    if (mFd < 0) return;
    ::close(mFd);
    mFd = -1;
    LOG(INFO) << "receiver powered off";
}

void Gnss::handleSentence(const std::string& sentence) {
    auto cb = callback();
    if (!cb) return;

    bool active;
    {
        std::lock_guard l(mLock);
        active = mActive;
    }
    if (!active) {
        mParser.parse(sentence);  // keep the parser in step while the receiver stays on
        return;
    }

    if (mNmea && Nmea::checksumValid(sentence)) {
        // UTC milliseconds
        const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
        cb->gnssNmeaCb(now, sentence);
    }

    auto epoch = mParser.parse(sentence);
    if (!epoch) return;

    if (mSvStatus) cb->gnssSvStatusCb(epoch->satellites);

    if (epoch->location) {
        auto now = std::chrono::steady_clock::now();
        int32_t interval;
        {
            std::lock_guard l(mLock);
            interval = mMinIntervalMs;
        }
        if (now - mLastReport >= std::chrono::milliseconds(interval - 100)) {
            mLastReport = now;
            cb->gnssLocationCb(*epoch->location);
        }
    }
}

void Gnss::loop() {
    std::string line;
    char buf[512];

    while (true) {
        {
            std::unique_lock l(mLock);
            if (mExit) break;
            const bool wanted = mActive || std::chrono::steady_clock::now() < mPowerOffAt;
            if (!wanted) {
                l.unlock();
                closeDevice();
                l.lock();
                // Sleep until a session starts
                mCv.wait(l, [this] { return mExit || mActive; });
                continue;
            }
        }

        if (mFd < 0 && !openDevice()) {
            std::unique_lock l(mLock);
            mCv.wait_for(l, std::chrono::seconds(5));
            continue;
        }

        struct pollfd pfd = {.fd = mFd, .events = POLLIN};
        int ret = poll(&pfd, 1, 1000);
        if (ret <= 0) continue;

        ssize_t len = read(mFd, buf, sizeof(buf));
        if (len <= 0) {
            PLOG(WARNING) << "read " << kDevice;
            closeDevice();
            continue;
        }
        for (ssize_t i = 0; i < len; i++) {
            char c = buf[i];
            if (c == '\n' || c == '\r') {
                if (!line.empty()) handleSentence(line);
                line.clear();
            } else if (line.size() < 256) {
                line.push_back(c);
            } else {
                line.clear();  // garbage: resynchronise on the next line
            }
        }
    }
    closeDevice();
}

}  // namespace aidl::android::hardware::gnss::librem5
