/*
 * Copyright (C) 2026 The Android Open Source Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "Nmea.h"

#include <stdlib.h>
#include <time.h>

#include <android-base/strings.h>
#include <utils/SystemClock.h>

namespace aidl::android::hardware::gnss::librem5 {

namespace {

// User equivalent range error: accuracy estimates are DOP x UERE
constexpr double kUereHorizontalMeters = 5.0;
constexpr double kUereVerticalMeters = 8.0;
constexpr double kKnotsToMps = 0.514444;

double toDouble(const std::string& s, double fallback = 0) {
    if (s.empty()) return fallback;
    char* end;
    double v = strtod(s.c_str(), &end);
    return end == s.c_str() ? fallback : v;
}

int toInt(const std::string& s, int fallback = 0) {
    if (s.empty()) return fallback;
    char* end;
    long v = strtol(s.c_str(), &end, 10);
    return end == s.c_str() ? fallback : static_cast<int>(v);
}

// ddmm.mmmm / dddmm.mmmm with hemisphere -> signed degrees
std::optional<double> toDegrees(const std::string& value, const std::string& hemisphere) {
    if (value.empty() || hemisphere.empty()) return std::nullopt;
    double v = toDouble(value);
    double degrees = static_cast<int>(v / 100);
    degrees += (v - degrees * 100) / 60.0;
    if (hemisphere == "S" || hemisphere == "W") degrees = -degrees;
    return degrees;
}

GnssConstellationType talkerConstellation(const std::string& talker) {
    if (talker == "GP") return GnssConstellationType::GPS;
    if (talker == "GL") return GnssConstellationType::GLONASS;
    if (talker == "GA") return GnssConstellationType::GALILEO;
    if (talker == "GB" || talker == "BD") return GnssConstellationType::BEIDOU;
    if (talker == "GQ" || talker == "QZ") return GnssConstellationType::QZSS;
    if (talker == "GI") return GnssConstellationType::IRNSS;
    return GnssConstellationType::UNKNOWN;
}

// NMEA 4.10 GNSS system id (GSA/GSV)
GnssConstellationType systemIdConstellation(int id) {
    switch (id) {
        case 1: return GnssConstellationType::GPS;
        case 2: return GnssConstellationType::GLONASS;
        case 3: return GnssConstellationType::GALILEO;
        case 4: return GnssConstellationType::BEIDOU;
        case 5: return GnssConstellationType::QZSS;
        case 6: return GnssConstellationType::IRNSS;
        default: return GnssConstellationType::UNKNOWN;
    }
}

// NMEA satellite id -> Android constellation + svid. Talkers like GP also carry SBAS (33-64) and,
// on some receivers, GLONASS (65-96) and QZSS (193-200) numbers.
std::pair<GnssConstellationType, int> androidSv(GnssConstellationType constellation, int prn) {
    if (constellation == GnssConstellationType::GPS || constellation == GnssConstellationType::UNKNOWN) {
        if (prn >= 1 && prn <= 32) return {GnssConstellationType::GPS, prn};
        if (prn >= 33 && prn <= 64) return {GnssConstellationType::SBAS, prn + 87};
        if (prn >= 65 && prn <= 96) return {GnssConstellationType::GLONASS, prn - 64};
        if (prn >= 193 && prn <= 200) return {GnssConstellationType::QZSS, prn};
        return {GnssConstellationType::UNKNOWN, prn};
    }
    if (constellation == GnssConstellationType::GLONASS && prn >= 65 && prn <= 96)
        return {constellation, prn - 64};
    if (constellation == GnssConstellationType::QZSS && prn >= 1 && prn <= 10)
        return {constellation, prn + 192};
    return {constellation, prn};
}

}  // namespace

bool Nmea::checksumValid(const std::string& s) {
    size_t star = s.rfind('*');
    if (s.size() < 4 || s[0] != '$' || star == std::string::npos || star + 3 > s.size())
        return false;
    uint8_t sum = 0;
    for (size_t i = 1; i < star; i++) sum ^= static_cast<uint8_t>(s[i]);
    return sum == strtoul(s.substr(star + 1, 2).c_str(), nullptr, 16);
}

std::optional<Nmea::Epoch> Nmea::parse(const std::string& sentence) {
    if (!checksumValid(sentence)) return std::nullopt;

    const std::string body = sentence.substr(1, sentence.rfind('*') - 1);
    const std::vector<std::string> f = ::android::base::Split(body, ",");
    if (f.empty() || f[0].size() != 5) return std::nullopt;
    const std::string talker = f[0].substr(0, 2);
    const std::string type = f[0].substr(2);

    std::optional<Epoch> finished;
    const bool timed = (type == "RMC" && f.size() > 9) || (type == "GGA" && f.size() >= 12);
    if (timed && !f[1].empty()) {
        // A new UTC time starts a new epoch: report the previous one
        if (!mTime.empty() && f[1] != mTime) finished = finishEpoch();
        mTime = f[1];
    }

    if (type == "RMC" && f.size() > 9) {
        parseRmc(f);
    } else if (type == "GGA" && f.size() >= 12) {
        parseGga(f);
    } else if (type == "GSA") {
        parseGsa(talker, f);
    } else if (type == "GSV") {
        parseGsv(talker, f);
    }
    return finished;
}

void Nmea::parseRmc(const std::vector<std::string>& f) {
    // $xxRMC,time,status,lat,N/S,lon,E/W,speed(kn),course,date,...
    mValid = f[2] == "A";
    mLocation = {};
    if (!mValid) return;

    auto lat = toDegrees(f[3], f[4]);
    auto lon = toDegrees(f[5], f[6]);
    if (!lat || !lon) {
        mValid = false;
        return;
    }
    mLocation.latitudeDegrees = *lat;
    mLocation.longitudeDegrees = *lon;
    mLocation.gnssLocationFlags = GnssLocation::HAS_LAT_LONG;
    if (!f[7].empty()) {
        mLocation.speedMetersPerSec = toDouble(f[7]) * kKnotsToMps;
        mLocation.gnssLocationFlags |= GnssLocation::HAS_SPEED;
    }
    if (!f[8].empty()) {
        mLocation.bearingDegrees = toDouble(f[8]);
        mLocation.gnssLocationFlags |= GnssLocation::HAS_BEARING;
    }

    // UTC from hhmmss.ss and ddmmyy
    if (f[1].size() >= 6 && f[9].size() == 6) {
        struct tm tm = {};
        tm.tm_hour = toInt(f[1].substr(0, 2));
        tm.tm_min = toInt(f[1].substr(2, 2));
        tm.tm_sec = toInt(f[1].substr(4, 2));
        tm.tm_mday = toInt(f[9].substr(0, 2));
        tm.tm_mon = toInt(f[9].substr(2, 2)) - 1;
        tm.tm_year = toInt(f[9].substr(4, 2)) + 100;
        double fraction = f[1].size() > 6 ? toDouble(f[1].substr(6)) : 0;
        mLocation.timestampMillis =
                static_cast<int64_t>(timegm(&tm)) * 1000 + static_cast<int64_t>(fraction * 1000);
    }

    mLocation.elapsedRealtime.flags =
            ElapsedRealtime::HAS_TIMESTAMP_NS | ElapsedRealtime::HAS_TIME_UNCERTAINTY_NS;
    mLocation.elapsedRealtime.timestampNs = ::android::elapsedRealtimeNano();
    // The sentence arrives some time after the measurement, at 9600 baud
    mLocation.elapsedRealtime.timeUncertaintyNs = 500 * 1000 * 1000.0;
}

void Nmea::parseGga(const std::vector<std::string>& f) {
    // $xxGGA,time,lat,N/S,lon,E/W,quality,numSV,HDOP,alt,M,geoidSep,M,...
    if (toInt(f[6]) == 0) return;  // no fix
    if (!f[8].empty() && mHdop == 0) mHdop = toDouble(f[8]);
    // Android wants the altitude above the WGS84 ellipsoid: MSL + geoid separation
    if (!f[9].empty()) mAltitude = toDouble(f[9]) + toDouble(f[11]);
}

void Nmea::parseGsa(const std::string& talker, const std::vector<std::string>& f) {
    // $xxGSA,mode,fix,sv1..sv12,PDOP,HDOP,VDOP[,systemId]
    if (f.size() < 18) return;
    GnssConstellationType constellation = talkerConstellation(talker);
    if (f.size() > 18 && !f[18].empty()) constellation = systemIdConstellation(toInt(f[18]));
    for (int i = 3; i <= 14; i++) {
        if (f[i].empty()) continue;
        auto [c, svid] = androidSv(constellation, toInt(f[i]));
        mUsed[c].insert(svid);
    }
    if (!f[16].empty()) mHdop = toDouble(f[16]);
    if (!f[17].empty()) mVdop = toDouble(f[17]);
}

void Nmea::parseGsv(const std::string& talker, const std::vector<std::string>& f) {
    // $xxGSV,numMsg,msgNum,numSV,{prn,elev,azim,snr}x(1..4)[,signalId]
    if (f.size() < 4) return;
    const int numMsg = toInt(f[1]);
    const int msgNum = toInt(f[2]);
    GnssConstellationType constellation = talkerConstellation(talker);

    auto& pending = mInViewPending[talker];
    if (msgNum == 1) pending.clear();
    // Groups of 4 fields; NMEA 4.10 appends a signal id, which never completes a group
    for (size_t i = 4; i + 3 < f.size(); i += 4) {
        if (f[i].empty()) continue;
        auto [c, svid] = androidSv(constellation, toInt(f[i]));
        pending.push_back({svid, c, static_cast<float>(toDouble(f[i + 3])),
                           static_cast<float>(toDouble(f[i + 1])),
                           static_cast<float>(toDouble(f[i + 2]))});
    }
    if (msgNum == numMsg) mInView[talker] = pending;
}

Nmea::Epoch Nmea::finishEpoch() {
    Epoch epoch;

    if (mValid) {
        if (mAltitude) {
            mLocation.altitudeMeters = *mAltitude;
            mLocation.gnssLocationFlags |= GnssLocation::HAS_ALTITUDE;
        }
        if (mHdop > 0) {
            mLocation.horizontalAccuracyMeters = mHdop * kUereHorizontalMeters;
            mLocation.gnssLocationFlags |= GnssLocation::HAS_HORIZONTAL_ACCURACY;
        }
        if (mVdop > 0 && (mLocation.gnssLocationFlags & GnssLocation::HAS_ALTITUDE)) {
            mLocation.verticalAccuracyMeters = mVdop * kUereVerticalMeters;
            mLocation.gnssLocationFlags |= GnssLocation::HAS_VERTICAL_ACCURACY;
        }
        epoch.location = mLocation;
    }

    for (const auto& [talker, svs] : mInView) {
        for (const auto& sv : svs) {
            IGnssCallback::GnssSvInfo info;
            info.svid = sv.svid;
            info.constellation = sv.constellation;
            info.cN0Dbhz = sv.cn0;
            info.basebandCN0DbHz = 0;
            info.elevationDegrees = sv.elevation;
            info.azimuthDegrees = sv.azimuth;
            info.carrierFrequencyHz = 0;
            info.svFlag = 0;
            auto used = mUsed.find(sv.constellation);
            if (used != mUsed.end() && used->second.count(sv.svid))
                info.svFlag |= static_cast<int>(IGnssCallback::GnssSvFlags::USED_IN_FIX);
            epoch.satellites.push_back(info);
        }
    }

    mUsed.clear();
    mValid = false;
    mAltitude.reset();
    mHdop = mVdop = 0;
    return epoch;
}

}  // namespace aidl::android::hardware::gnss::librem5
