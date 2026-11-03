/*
 * Copyright (C) 2026 The Android Open Source Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * Librem 5 hardware kill switch monitor.
 *
 * The Wi-Fi/Bluetooth kill switch cuts the power of the BCM43752 combo chip.
 * Wi-Fi recovers by itself (the SDIO card is detected again), Bluetooth does
 * not: its UART protocol driver stays bound to a chip that lost its firmware
 * patch. This daemon follows the switch through /dev/rfkill and reports it in
 * vendor.librem5.wlan_hks ("blocked" when switched off, "unblocked" when
 * switched back on), for init.librem5.rc to stop Bluetooth and to bring it
 * back. The microphone and camera switches go to Android as input switches
 * (rfkill-hks), the modem switch needs nothing.
 */

#include <fcntl.h>
#include <linux/rfkill.h>
#include <unistd.h>

#include <map>
#include <string>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>

namespace {

constexpr char kSwitchName[] = "wlan-hks";
constexpr char kProperty[] = "vendor.librem5.wlan_hks";

std::string rfkillName(uint32_t idx) {
    std::string name;
    android::base::ReadFileToString("/sys/class/rfkill/rfkill" + std::to_string(idx) + "/name",
                                    &name);
    return android::base::Trim(name);
}

}  // namespace

int main() {
    android::base::InitLogging(nullptr, android::base::LogdLogger(android::base::SYSTEM));

    android::base::unique_fd fd(open("/dev/rfkill", O_RDONLY | O_CLOEXEC));
    if (fd < 0) {
        PLOG(ERROR) << "open /dev/rfkill";
        return 1;
    }

    std::map<uint32_t, bool> isSwitch;  // rfkill index -> is the Wi-Fi/BT kill switch
    int blocked = -1;                   // switch state, -1 until known

    while (true) {
        struct rfkill_event event = {};
        // Events can be longer than struct rfkill_event (rfkill_event_ext); the rest is dropped.
        ssize_t len = TEMP_FAILURE_RETRY(read(fd, &event, sizeof(event)));
        if (len < 0) {
            PLOG(ERROR) << "read /dev/rfkill";
            return 1;
        }
        if (len < RFKILL_EVENT_SIZE_V1) continue;

        if (event.op == RFKILL_OP_DEL) {
            isSwitch.erase(event.idx);
            continue;
        }
        auto it = isSwitch.find(event.idx);
        if (it == isSwitch.end()) it = isSwitch.emplace(event.idx, rfkillName(event.idx) == kSwitchName).first;
        if (!it->second) continue;

        const int now = event.hard ? 1 : 0;
        if (now == blocked) continue;

        // At startup only an engaged switch is reported: init already brought Bluetooth up.
        if (now) {
            LOG(INFO) << "Wi-Fi/Bluetooth kill switch engaged";
            android::base::SetProperty(kProperty, "blocked");
        } else if (blocked == 1) {
            LOG(INFO) << "Wi-Fi/Bluetooth kill switch released";
            android::base::SetProperty(kProperty, "unblocked");
        }
        blocked = now;
    }
}
