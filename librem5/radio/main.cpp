/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include <string_view>
#include <memory>

#include <cutils/properties.h>
#include <fcntl.h>
#include <termios.h>
#include <atomic>
#include <chrono>
#include <thread>

#include <android-base/properties.h>

#include <android/binder_interface_utils.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "AtChannel.h"
#include "ImsMedia.h"
#include "RadioConfig.h"
#include "RadioData.h"
#include "RadioIms.h"
#include "RadioMessaging.h"
#include "RadioModem.h"
#include "RadioNetwork.h"
#include "RadioSim.h"
#include "RadioVoice.h"
#include "Sap.h"

#include "debug.h"

namespace {
using ::android::base::unique_fd;
namespace impl = ::aidl::android::hardware::radio::implementation;

std::string getInstanceName(const std::string_view descriptor,
                            const std::string_view slot) {
    std::string result(descriptor.data(), descriptor.size());
    result.append(1, '/');
    result.append(slot.data(), slot.size());
    return result;
}

template <class S> std::shared_ptr<S> registerService(
        const std::string_view instanceSuffix,
        std::shared_ptr<impl::AtChannel> atChannel) {
    auto serice = ndk::SharedRefBase::make<S>(std::move(atChannel));
    const std::string instanceName = getInstanceName(S::descriptor, instanceSuffix);

    if (AServiceManager_addService(serice->asBinder().get(),
                                   instanceName.c_str()) != STATUS_OK) {
        return FAILURE_V(nullptr, "Failed to register: '%s'",
                         instanceName.c_str());
    }

    return serice;
}

template <class T> void addResponseSink(impl::AtChannel& atChannel,
                                        const std::shared_ptr<T>& strongObject,
                                        void(T::*method)(const impl::AtResponsePtr&)) {
    std::weak_ptr<T> weakObject(strongObject);

    atChannel.addResponseSink([weakObject = std::move(weakObject), method]
                              (const impl::AtResponsePtr& response) -> bool {
        if (const auto strongObject = weakObject.lock()) {
            (*strongObject.*method)(response);
            return true;
        } else {
            return false;
        }
    });
}

// Notification and format setup; refused by the BM818 while no SIM is ready.
bool runOptionalInit(const impl::AtChannel::RequestPipe pipe,
                     impl::AtChannel::Conversation& conversation) {
    using namespace std::literals;
    using impl::AtResponse;

    static const std::string_view optionalCmds[] = {
        "AT+CREG=2"sv,
        "AT+CGREG=2"sv,
        "AT+CEREG=2"sv,
        "AT+CCWA=1"sv,
        "AT+CMUT=0"sv,
        "AT+CSSN=0,1"sv,
        "AT+COLP=0"sv,
        "AT+CSCS=\"HEX\""sv,
        "AT+CUSD=1"sv,
        "AT+CGEREP=1,0"sv,
        "AT+CMGF=0"sv,
        "AT+CNMI=2,2,0,1,0"sv,  // deliver SMS (+CMT) and status reports (+CDS) directly
    };

    bool all = true;
    for (const std::string_view& cmd : optionalCmds) {
        const auto response =
            conversation(pipe, cmd,
                         [](const AtResponse& response) -> bool {
                             return response.holds<AtResponse::OK>();
                         });
        if (!response) {
            all = false;
        }
    }
    return all;
}

// A real modem does not push signal strength or operator changes the way the emulator does:
// poll them. Replies are broadcast to the services like notifications. The optional setup is
// retried until the modem accepts it (SIM ready).
void startStatusPoller(const std::shared_ptr<impl::AtChannel>& atChannel) {
    using namespace std::literals;
    using impl::AtChannel;
    using impl::AtResponse;

    auto conversation = std::make_shared<AtChannel::Conversation>();
    atChannel->addResponseSink([conversation](const impl::AtResponsePtr& response) -> bool {
        conversation->send(response);
        return true;
    });

    std::thread([atChannel, conversation]() {
        auto optionalInitDone = std::make_shared<std::atomic<bool>>(false);
        while (true) {
            std::this_thread::sleep_for(10s);
            atChannel->queueRequester([conversation, optionalInitDone]
                                      (const AtChannel::RequestPipe pipe) -> bool {
                if (!optionalInitDone->load()) {
                    optionalInitDone->store(runOptionalInit(pipe, *conversation));
                }
                for (const std::string_view cmd : { "AT+CSQ"sv, "AT+CREG?"sv, "AT+CEREG?"sv,
                                                    "AT+COPS?"sv }) {
                    (*conversation)(pipe, cmd,
                                    [](const AtResponse& response) -> bool {
                                        return response.holds<AtResponse::OK>();
                                    });
                }
                return true;
            });
        }
    }).detach();
}

int mainImpl(impl::AtChannel::HostChannelFactory hostChannelFactory) {
    using impl::AtChannel;
    using impl::ImsMedia;
    using impl::RadioConfig;
    using impl::RadioData;
    using impl::RadioIms;
    using impl::RadioMessaging;
    using impl::RadioModem;
    using impl::RadioNetwork;
    using impl::RadioSim;
    using impl::RadioVoice;
    using impl::Sap;

    using namespace std::literals;

    // Librem 5 (BM818): only echo/verbose/error format are required. The rest set up
    // notifications and formats that the modem refuses until a SIM is ready, so failures are
    // logged and the poller below retries them.
    auto initSequence = [](const AtChannel::RequestPipe pipe,
                           AtChannel::Conversation& conversation) -> bool {
        static const std::string_view requiredCmds[] = {
            "ATE0Q0V1"sv,
            "AT+CMEE=1"sv,
            "AT+CFUN?"sv,
        };

        using impl::AtResponse;
        using impl::AtResponsePtr;
        using OK = AtResponse::OK;

        for (const std::string_view& cmd : requiredCmds) {
            const AtResponsePtr response =
                conversation(pipe, cmd,
                             [](const AtResponse& response) -> bool {
                                 return response.holds<OK>();
                             });
            if (!response || !response->isOK()) {
                RLOGE("init: '%*.*s' failed", int(cmd.size()), int(cmd.size()), cmd.data());
                return false;
            }
        }

        runOptionalInit(pipe, conversation);
        return true;
    };

    const auto atChannel = std::make_shared<AtChannel>(std::move(hostChannelFactory),
                                                       std::move(initSequence));

    static constexpr std::string_view kDefaultInstance = "default"sv;
    static constexpr std::string_view kSlot1Instance = "slot1"sv;

    ABinderProcess_setThreadPoolMaxThreadCount(2);
    ABinderProcess_startThreadPool();

    const auto imsMedia = registerService<ImsMedia>(kDefaultInstance, atChannel);
    if (!imsMedia) {
        return EXIT_FAILURE;
    }

    const auto radioConfig = registerService<RadioConfig>(kDefaultInstance, atChannel);
    if (!radioConfig) {
        return EXIT_FAILURE;
    }

    const auto radioData = registerService<RadioData>(kSlot1Instance, atChannel);
    if (!radioData) {
        return EXIT_FAILURE;
    }

    const auto radioIms = registerService<RadioIms>(kSlot1Instance, atChannel);
    if (!radioIms) {
        return EXIT_FAILURE;
    }

    const auto radioMessaging = registerService<RadioMessaging>(kSlot1Instance, atChannel);
    if (!radioMessaging) {
        return EXIT_FAILURE;
    }

    const auto radioModem = registerService<RadioModem>(kSlot1Instance, atChannel);
    if (!radioModem) {
        return EXIT_FAILURE;
    }

    const auto radioNetwork = registerService<RadioNetwork>(kSlot1Instance, atChannel);
    if (!radioNetwork) {
        return EXIT_FAILURE;
    }

    const auto radioSim = registerService<RadioSim>(kSlot1Instance, atChannel);
    if (!radioNetwork) {
        return EXIT_FAILURE;
    }

    const auto radioVoice = registerService<RadioVoice>(kSlot1Instance, atChannel);
    if (!radioVoice) {
        return EXIT_FAILURE;
    }

    const auto sap = registerService<Sap>(kSlot1Instance, atChannel);
    if (!sap) {
        return EXIT_FAILURE;
    }

    addResponseSink(*atChannel, imsMedia, &ImsMedia::atResponseSink);
    addResponseSink(*atChannel, radioConfig, &RadioConfig::atResponseSink);
    addResponseSink(*atChannel, radioData, &RadioData::atResponseSink);
    addResponseSink(*atChannel, radioIms, &RadioIms::atResponseSink);
    addResponseSink(*atChannel, radioMessaging, &RadioMessaging::atResponseSink);
    addResponseSink(*atChannel, radioModem, &RadioModem::atResponseSink);
    addResponseSink(*atChannel, radioNetwork, &RadioNetwork::atResponseSink);
    addResponseSink(*atChannel, radioSim, &RadioSim::atResponseSink);
    addResponseSink(*atChannel, radioVoice, &RadioVoice::atResponseSink);
    addResponseSink(*atChannel, sap, &Sap::atResponseSink);

    startStatusPoller(atChannel);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;    // joinThreadPool is not expected to return
}

// Librem 5: the BM818's AT port, a USB serial (option driver) tty. It appears a few seconds
// after boot and again after the modem re-enumerates, so wait for it.
unique_fd openHostChannel(const std::string& path) {
    for (int attempt = 0; attempt < 60; ++attempt) {
        const int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
        if (fd >= 0) {
            struct termios tio;
            if (::tcgetattr(fd, &tio) == 0) {
                ::cfmakeraw(&tio);
                tio.c_cflag |= CLOCAL | CREAD;
                tio.c_cc[VMIN] = 1;
                tio.c_cc[VTIME] = 0;
                ::cfsetspeed(&tio, B115200);
                ::tcsetattr(fd, TCSANOW, &tio);
            }
            ::tcflush(fd, TCIOFLUSH);
            return unique_fd(fd);
        }
        if (attempt == 0) {
            RLOGW("Waiting for '%s': %s", path.c_str(), strerror(errno));
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return FAILURE_V(unique_fd(), "Could not open '%s'", path.c_str());
}

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
    std::string channelName =
        android::base::GetProperty("ro.vendor.radio.at_port", "/dev/ttyUSB2");

    return mainImpl([channelName = std::move(channelName)](){
        return openHostChannel(channelName);
    });
}
