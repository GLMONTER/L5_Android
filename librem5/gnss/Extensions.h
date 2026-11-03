/*
 * Copyright (C) 2026 The Android Open Source Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * IGnss V3 extensions that an NMEA-only receiver has nothing to offer for: they accept their
 * calls and report nothing. Unlike the AOSP default HAL's versions they never produce simulated
 * data. The nullable ones are provided too: the framework's GNSS JNI (Android 17) wraps a null
 * extension in a non-null wrapper and calls it, crashing system_server. Without the capability
 * bits (none are reported) the framework doesn't use batching, geofencing or navigation messages.
 */

#pragma once

#include <aidl/android/hardware/gnss/BnAGnss.h>
#include <aidl/android/hardware/gnss/BnAGnssRil.h>
#include <aidl/android/hardware/gnss/BnGnssAntennaInfo.h>
#include <aidl/android/hardware/gnss/BnGnssConfiguration.h>
#include <aidl/android/hardware/gnss/BnGnssDebug.h>
#include <aidl/android/hardware/gnss/BnGnssMeasurementInterface.h>
#include <aidl/android/hardware/gnss/BnGnssBatching.h>
#include <aidl/android/hardware/gnss/BnGnssGeofence.h>
#include <aidl/android/hardware/gnss/BnGnssNavigationMessageInterface.h>
#include <aidl/android/hardware/gnss/BnGnssPowerIndication.h>
#include <aidl/android/hardware/gnss/BnGnssPsds.h>
#include <aidl/android/hardware/gnss/measurement_corrections/BnMeasurementCorrectionsInterface.h>
#include <aidl/android/hardware/gnss/visibility_control/BnGnssVisibilityControl.h>

namespace aidl::android::hardware::gnss::librem5 {

using ndk::ScopedAStatus;

class GnssConfiguration : public BnGnssConfiguration {
  public:
    ScopedAStatus setSuplVersion(int32_t) override { return ScopedAStatus::ok(); }
    ScopedAStatus setSuplMode(int32_t) override { return ScopedAStatus::ok(); }
    ScopedAStatus setLppProfile(int32_t) override { return ScopedAStatus::ok(); }
    ScopedAStatus setGlonassPositioningProtocol(int32_t) override { return ScopedAStatus::ok(); }
    ScopedAStatus setEmergencySuplPdn(bool) override { return ScopedAStatus::ok(); }
    ScopedAStatus setEsExtensionSec(int32_t) override { return ScopedAStatus::ok(); }
    ScopedAStatus setBlocklist(const std::vector<BlocklistedSource>&) override {
        return ScopedAStatus::ok();
    }
};

// No raw measurements from an NMEA receiver (CAPABILITY_MEASUREMENTS is not reported)
class GnssMeasurementInterface : public BnGnssMeasurementInterface {
  public:
    ScopedAStatus setCallback(const std::shared_ptr<IGnssMeasurementCallback>&, bool,
                              bool) override {
        return ScopedAStatus::ok();
    }
    ScopedAStatus close() override { return ScopedAStatus::ok(); }
    ScopedAStatus setCallbackWithOptions(const std::shared_ptr<IGnssMeasurementCallback>&,
                                         const Options&) override {
        return ScopedAStatus::ok();
    }
};

class GnssPowerIndication : public BnGnssPowerIndication {
  public:
    ScopedAStatus setCallback(const std::shared_ptr<IGnssPowerIndicationCallback>& cb) override {
        // No power stats capabilities
        if (cb) cb->setCapabilitiesCb(0);
        return ScopedAStatus::ok();
    }
    ScopedAStatus requestGnssPowerStats() override { return ScopedAStatus::ok(); }
};

class AGnss : public BnAGnss {
  public:
    ScopedAStatus setCallback(const std::shared_ptr<IAGnssCallback>&) override {
        return ScopedAStatus::ok();
    }
    ScopedAStatus dataConnClosed() override { return ScopedAStatus::ok(); }
    ScopedAStatus dataConnFailed() override { return ScopedAStatus::ok(); }
    ScopedAStatus setServer(IAGnssCallback::AGnssType, const std::string&, int32_t) override {
        return ScopedAStatus::ok();
    }
    ScopedAStatus dataConnOpen(int64_t, const std::string&, IAGnss::ApnIpType) override {
        return ScopedAStatus::ok();
    }
};

class AGnssRil : public BnAGnssRil {
  public:
    ScopedAStatus setCallback(const std::shared_ptr<IAGnssRilCallback>&) override {
        return ScopedAStatus::ok();
    }
    ScopedAStatus setRefLocation(const AGnssRefLocation&) override { return ScopedAStatus::ok(); }
    ScopedAStatus setSetId(SetIdType, const std::string&) override { return ScopedAStatus::ok(); }
    ScopedAStatus updateNetworkState(const NetworkAttributes&) override {
        return ScopedAStatus::ok();
    }
    ScopedAStatus injectNiSuplMessageData(const std::vector<uint8_t>&, int32_t) override {
        return ScopedAStatus::ok();
    }
};

class GnssDebug : public BnGnssDebug {
  public:
    ScopedAStatus getDebugData(DebugData* data) override {
        *data = {};
        return ScopedAStatus::ok();
    }
};

class GnssAntennaInfo : public BnGnssAntennaInfo {
  public:
    ScopedAStatus setCallback(const std::shared_ptr<IGnssAntennaInfoCallback>&) override {
        return ScopedAStatus::ok();
    }
    ScopedAStatus close() override { return ScopedAStatus::ok(); }
};

class GnssVisibilityControl : public visibility_control::BnGnssVisibilityControl {
  public:
    ScopedAStatus enableNfwLocationAccess(const std::vector<std::string>&) override {
        return ScopedAStatus::ok();
    }
    ScopedAStatus setCallback(
            const std::shared_ptr<visibility_control::IGnssVisibilityControlCallback>&) override {
        return ScopedAStatus::ok();
    }
};

class GnssPsds : public BnGnssPsds {
  public:
    ScopedAStatus injectPsdsData(PsdsType, const std::vector<uint8_t>&) override {
        return ScopedAStatus::ok();
    }
    ScopedAStatus setCallback(const std::shared_ptr<IGnssPsdsCallback>&) override {
        return ScopedAStatus::ok();
    }
};

class GnssBatching : public BnGnssBatching {
  public:
    ScopedAStatus init(const std::shared_ptr<IGnssBatchingCallback>&) override {
        return ScopedAStatus::ok();
    }
    // No batching buffer: the framework treats a size of 0 as unsupported
    ScopedAStatus getBatchSize(int32_t* size) override {
        *size = 0;
        return ScopedAStatus::ok();
    }
    ScopedAStatus start(const Options&) override { return ScopedAStatus::ok(); }
    ScopedAStatus flush() override { return ScopedAStatus::ok(); }
    ScopedAStatus stop() override { return ScopedAStatus::ok(); }
    ScopedAStatus cleanup() override { return ScopedAStatus::ok(); }
};

class GnssGeofence : public BnGnssGeofence {
  public:
    ScopedAStatus setCallback(const std::shared_ptr<IGnssGeofenceCallback>&) override {
        return ScopedAStatus::ok();
    }
    ScopedAStatus addGeofence(int32_t, double, double, double, int32_t, int32_t, int32_t,
                              int32_t) override {
        return ScopedAStatus::fromServiceSpecificError(IGnssGeofenceCallback::ERROR_GENERIC);
    }
    ScopedAStatus pauseGeofence(int32_t) override { return ScopedAStatus::ok(); }
    ScopedAStatus resumeGeofence(int32_t, int32_t) override { return ScopedAStatus::ok(); }
    ScopedAStatus removeGeofence(int32_t) override { return ScopedAStatus::ok(); }
};

class GnssNavigationMessageInterface : public BnGnssNavigationMessageInterface {
  public:
    ScopedAStatus setCallback(const std::shared_ptr<IGnssNavigationMessageCallback>&) override {
        return ScopedAStatus::ok();
    }
    ScopedAStatus close() override { return ScopedAStatus::ok(); }
};

class MeasurementCorrectionsInterface
    : public measurement_corrections::BnMeasurementCorrectionsInterface {
  public:
    ScopedAStatus setCorrections(const measurement_corrections::MeasurementCorrections&) override {
        return ScopedAStatus::ok();
    }
    ScopedAStatus setCallback(
            const std::shared_ptr<measurement_corrections::IMeasurementCorrectionsCallback>& cb)
            override {
        if (cb) cb->setCapabilitiesCb(0);
        return ScopedAStatus::ok();
    }
};

}  // namespace aidl::android::hardware::gnss::librem5
