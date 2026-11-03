/*
 * Copyright (C) 2026 The Android Open Source Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * NMEA 0183 parser for the Librem 5 GNSS receiver: RMC, GGA, GSA and GSV of any constellation
 * talker (GP, GL, GA, GB/BD, GQ/QZ, GN), producing one fix and one satellite list per epoch.
 */

#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <aidl/android/hardware/gnss/GnssLocation.h>
#include <aidl/android/hardware/gnss/IGnssCallback.h>

namespace aidl::android::hardware::gnss::librem5 {

class Nmea {
  public:
    struct Epoch {
        std::optional<GnssLocation> location;      // set when the receiver has a fix
        std::vector<IGnssCallback::GnssSvInfo> satellites;
    };

    // Feeds one sentence (without line ending). Returns the finished epoch when a timed
    // sentence (RMC, GGA) starts a new one, i.e. once per fix interval. Within an epoch the
    // sentences may come in any order.
    std::optional<Epoch> parse(const std::string& sentence);

    static bool checksumValid(const std::string& sentence);

  private:
    struct Sv {
        int svid;
        GnssConstellationType constellation;
        float cn0;
        float elevation;
        float azimuth;
    };

    void parseRmc(const std::vector<std::string>& f);
    void parseGga(const std::vector<std::string>& f);
    void parseGsa(const std::string& talker, const std::vector<std::string>& f);
    void parseGsv(const std::string& talker, const std::vector<std::string>& f);
    Epoch finishEpoch();

    // Current epoch
    std::string mTime;          // hhmmss.ss of the epoch (RMC/GGA)
    bool mValid = false;        // RMC status A
    GnssLocation mLocation;     // from RMC
    std::optional<double> mAltitude;  // from GGA, above the WGS84 ellipsoid
    double mHdop = 0, mVdop = 0;
    // Satellites used in the fix, keyed by constellation, and in view (GSV) per talker
    std::map<GnssConstellationType, std::set<int>> mUsed;
    std::map<std::string, std::vector<Sv>> mInView;
    std::map<std::string, std::vector<Sv>> mInViewPending;
};

}  // namespace aidl::android::hardware::gnss::librem5
