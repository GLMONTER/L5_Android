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

#define FAILURE_DEBUG_PREFIX "AtChannel"

#include <cstring>
#include <format>
#include <charconv>
#include <exception>
#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <thread>

#include "AtChannel.h"
#include "debug.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {
namespace {
int sendRequestImpl(const int fd, const char* data, size_t size) {
    while (size > 0) {
        const ssize_t written = ::write(fd, data, size);
        if (written >= 0) {
            data += written;
            size -= written;
        } else if (errno == EINTR) {
            continue;
        } else {
            return FAILURE(errno);
        }
    }

    return 0;
}
}  // namespace

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "AtChannel::RequestPipe"

bool AtChannel::RequestPipe::operator()(const std::string_view request) const {
    if (mChannel) {
        if (const std::optional<std::string> reply = mChannel->emulatedReply(request)) {
            mChannel->injectResponses(*reply);
            return true;
        }
    }

    int err = sendRequestImpl(mFd, request.data(), request.size());
    if (err == 0) {
        const char kCR = 0x0D;
        err = sendRequestImpl(mFd, &kCR, 1);
    }

    return err == 0;
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "AtChannel"

AtChannel::AtChannel(HostChannelFactory hostChannelFactory,
                     InitSequence initSequence)
        : mHostChannelFactory(std::move(hostChannelFactory))
        , mInitSequence(std::move(initSequence)) {
    mRequestThread = std::thread(&AtChannel::requestLoop, this);
}

AtChannel::~AtChannel() {
    queueRequester({});
    mRequestThread.join();
    mReaderThread.join();
}

void AtChannel::queueRequester(Requester requester) {
    std::lock_guard<std::mutex> lock(mRequestQueueMtx);
    mRequesterQueue.push_back(std::move(requester));
    mRequesterAvailable.notify_one();
}

void AtChannel::addResponseSink(ResponseSink responseSink) {
    std::lock_guard<std::mutex> lock(mResponseSinksMtx);
    mResponseSinks.push_back(std::move(responseSink));
}

void AtChannel::requestLoop() {
    while (true) {
        const Requester requester = getRequester();
        if (requester) {
            // Librem 5: a failed request is usually just the modem's ERROR (no SIM, no
            // network), not a broken link; the reader thread resets the channel if the tty
            // itself fails. Unexpected replies throw (AtResponse::unexpected).
            try {
                if (!requester(getHostChannelPipe())) {
                    RLOGW("%s", "request failed");
                }
            } catch (const std::exception& e) {
                RLOGE("request aborted: %s", e.what());
            }
        } else {
            break;
        }
    }

    resetHostChannel();
}

void AtChannel::readingLoop(const int hostChannelFd) {
    std::vector<char> unconsumed;
    mLastWasCR = true;
    // Librem 5: poll with a timeout so the thread notices when the channel is closed or
    // replaced (a blocked read() on a closed fd never returns, and the next
    // getHostChannelPipe() joins this thread).
    while (getHostChannelFd() == hostChannelFd) {
        struct pollfd pfd = { .fd = hostChannelFd, .events = POLLIN };
        const int n = ::poll(&pfd, 1, 500);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        } else if (n == 0) {
            continue;
        } else if (pfd.revents & (POLLNVAL | POLLERR | POLLHUP)) {
            RLOGE("AT channel fd=%d: revents 0x%x", hostChannelFd, pfd.revents);
            break;
        } else if (!receiveResponses(hostChannelFd, &unconsumed)) {
            break;
        }
    }
}

AtChannel::Requester AtChannel::getRequester() {
    std::unique_lock<std::mutex> lock(mRequestQueueMtx);
    while (true) {
        if (!mRequesterQueue.empty()) {
            Requester requester(std::move(mRequesterQueue.front()));
            mRequesterQueue.pop_front();
            return requester;
        } else {
            mRequesterAvailable.wait(lock);
        }
    }
}

void AtChannel::broadcastResponse(const AtResponsePtr& response) {
    mConversation.send(response);

    std::lock_guard<std::mutex> lock(mResponseSinksMtx);

    const auto newEnd = std::remove_if(mResponseSinks.begin(), mResponseSinks.end(),
        [&response](const ResponseSink& responseSink) -> bool {
            return !responseSink(response);
        });

    mResponseSinks.erase(newEnd, mResponseSinks.end());
}

void AtChannel::resetHostChannel(unique_fd fd) {
    std::lock_guard<std::mutex> lock(mHostChannelMtx);
    mHostChannel = std::move(fd);
}

int AtChannel::getHostChannelFd() const {
    std::lock_guard<std::mutex> lock(mHostChannelMtx);
    return mHostChannel.get();
}

AtChannel::RequestPipe AtChannel::getHostChannelPipe() {
    int hostChannelFd = getHostChannelFd();
    if (hostChannelFd < 0) {
        if (mReaderThread.joinable()) {
            mReaderThread.join();
        }

        unique_fd newHostChannel = mHostChannelFactory();
        if (!newHostChannel.ok()) {
            return RequestPipe(FAILURE_V(-1, "%s", "mHostChannelFactory failed"));
        }

        hostChannelFd = newHostChannel.get();
        resetHostChannel(std::move(newHostChannel));

        mReaderThread = std::thread([this, hostChannelFd](){
            readingLoop(hostChannelFd);
            // Librem 5: only drop the channel if it is still ours
            std::lock_guard<std::mutex> lock(mHostChannelMtx);
            if (mHostChannel.get() == hostChannelFd) {
                mHostChannel.reset();
            }
        });

        // Librem 5: the modem's AT port shows up on USB before its firmware answers; give
        // it a few tries. If it never answers (kill switch off), drop the channel and retry on
        // the next request instead of aborting.
        bool initialized = false;
        for (int attempt = 0; attempt < 5 && !initialized; ++attempt) {
            if (attempt > 0) {
                RLOGW("init attempt %d failed, retrying", attempt);
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
            initialized = mInitSequence(RequestPipe(hostChannelFd, this), mConversation);
        }
        if (!initialized) {
            RLOGE("%s", "Can't init the host channel, will retry");
            resetHostChannel();  // the reader sees POLLNVAL and exits
            return RequestPipe(-1);
        }
    }

    return RequestPipe(hostChannelFd, this);
}

bool AtChannel::receiveResponses(const int hostChannelFd,
                                 std::vector<char>* unconsumed) {
    char buf[1024];
    const int len = ::read(hostChannelFd, buf, sizeof(buf));
    if (len < 0) {
        const int err = errno;
        if (err == EINTR) {
            return true;
        } else {
            return FAILURE_V(false, "fd=%d, err=%s (%d)",
                             hostChannelFd, ::strerror(err), err);
        }
    } else if (len == 0) {
        // tty hangup: the modem went away (USB re-enumeration, kill switch)
        return FAILURE_V(false, "fd=%d: EOF", hostChannelFd);
    }

    // Librem 5: a real modem frames lines as <CR><LF>text<CR><LF> with blank lines in
    // between; the parser expects the emulator's bare <CR> framing. Drop LFs and
    // collapse runs of CRs.
    for (int i = 0; i < len; ++i) {
        const char c = buf[i];
        if (c == '\n') {
            continue;
        } else if (c == '\r') {
            if (mLastWasCR) {
                continue;
            }
            mLastWasCR = true;
        } else {
            mLastWasCR = false;
        }
        unconsumed->push_back(c);
    }

    std::vector<char> pending;
    pending.swap(*unconsumed);
    return receiveResponsesImpl(pending.data(), pending.data() + pending.size(), unconsumed);
}

// NOTE: [begin, end) could contain one or more requests,
// the last one might be incomplete
bool AtChannel::receiveResponsesImpl(const char* begin, const char* const end,
                                     std::vector<char>* unconsumed) {
    while (begin < end) {
        const char* next = receiveOneResponse(begin, end);
        if (next == begin) {
            unconsumed->assign(begin, end);
            return true;
        } else if (next == nullptr) {
            RLOGE("Can't parse '%*.*s'", int(end - begin), int(end - begin), begin);
            return false;
        } else {
            begin = next;
        }
    }

    unconsumed->clear();
    return true;
}

std::optional<std::string> AtChannel::emulatedReply(const std::string_view request) const {
    using namespace std::literals;
    // ratUtils::ModemTechnology: GSM 0, WCDMA 1, LTE 5 (what the BM818 supports)
    if (request == "AT+CTEC=?"sv) {
        return "+CTEC: 0,1,5\rOK\r";
    } else if (request == "AT+CTEC?"sv) {
        return std::format("+CTEC: {0:d},{1:x}\rOK\r", 1U << mModemTech.load(),
                           (1U << 0) | (1U << 1) | (1U << 5));
    } else if (request.starts_with("AT+CTEC="sv)) {
        return "+CTEC: DONE\rOK\r";  // preferred network type: modem picks automatically
    } else if (request == "AT+WRMP?"sv) {
        return "+WRMP: 0\rOK\r";
    } else if (request == "AT+CCSS?"sv) {
        return "+CCSS: 0\rOK\r";
    } else if (request.starts_with("AT+WSOS="sv)) {
        return "+WSOS: 0\rOK\r";
    } else if (request.starts_with("AT+WRMP="sv) || request.starts_with("AT+CCSS="sv) ||
               request.starts_with("AT+CMOD="sv)) {
        return "OK\r";
    }
    return std::nullopt;
}

void AtChannel::injectResponses(std::string_view text) {
    while (!text.empty()) {
        auto [consumed, response] = AtResponse::parse(text);
        if (response) {
            broadcastResponse(response);
        }
        if (consumed <= 0) {
            break;
        }
        text.remove_prefix(consumed);
    }
}

// +COPS: <mode>[,<format>,<oper>[,<AcT>]] -> the serving radio technology for +CTEC
void AtChannel::noteAccessTechnology(const std::string_view line) {
    const size_t comma = line.rfind(',');
    const size_t quote = line.rfind('"');
    if (comma == line.npos || quote == line.npos || comma < quote) {
        return;  // no <AcT> after the operator name
    }
    int act = -1;
    const std::string_view v = line.substr(comma + 1);
    std::from_chars(v.data(), v.data() + v.size(), act);
    int tech;
    switch (act) {
    case 0: case 1: case 3: tech = 0; break;                  // GSM, GSM compact, EGPRS
    case 2: case 4: case 5: case 6: tech = 1; break;          // UTRAN, HSDPA/HSUPA/HSPA
    case 7: case 9: tech = 5; break;                          // E-UTRAN
    case 10: case 11: case 12: case 13: tech = 6; break;      // NR
    default: return;
    }
    mModemTech.store(tech);
}

const char* AtChannel::receiveOneResponse(const char* const begin, const char* const end) {
    switch (*begin) {
    case '\r':
    case '\n':
        return begin + 1;
    }

    {
        using namespace std::literals;
        const std::string_view rest(begin, end - begin);
        const size_t eol = rest.find('\r');
        if (rest.starts_with("+COPS: "sv) && eol != rest.npos) {
            noteAccessTechnology(rest.substr(0, eol));
        }
    }

    auto [consumed, response] = AtResponse::parse(std::string_view(begin, end - begin));
    if (response) {
        try {
            broadcastResponse(response);
        } catch (const std::exception& e) {
            RLOGE("response handler aborted: %s", e.what());
        }
    }

    if (consumed >= 0) {
        return begin + consumed;
    }

    // Librem 5: skip lines the parser does not know (vendor notifications such as
    // $QCSIMSTAT, +CIEV, +QIND) instead of dropping the channel.
    const char* const eol = static_cast<const char*>(::memchr(begin, '\r', end - begin));
    if (!eol) {
        return begin;  // wait for the rest of the line
    }
    RLOGD("ignoring '%*.*s'", int(eol - begin), int(eol - begin), begin);
    return eol + 1;
}

AtResponsePtr AtChannel::Conversation::operator()(
        const RequestPipe requestPipe,
        const std::string_view request,
        const AtChannel::Conversation::FilterFunc& filter,
        const AtChannel::Conversation::Duration timeout) {
    std::future<AtResponsePtr> futureResponse;

    {
        std::lock_guard<std::mutex> lock(mMtx);
        mFilter = &filter;
        mSink = decltype(mSink)();
        futureResponse = mSink.get_future();
    }

    if (!requestPipe(request)) {
        std::lock_guard<std::mutex> lock(mMtx);
        mFilter = nullptr;
        return nullptr;
    } else if (futureResponse.wait_for(timeout) == std::future_status::ready) {
        return futureResponse.get();
    } else {
        {
            std::lock_guard<std::mutex> lock(mMtx);
            mFilter = nullptr;
        }

        const int requestLen = request.size();
        return FAILURE_V(nullptr, "Timeout for '%*.*s'",
                         requestLen, requestLen, request.data());
    }
}

AtResponsePtr AtChannel::Conversation::operator()(
        const RequestPipe requestPipe,
        const std::string_view request,
        const FilterFunc& filter) {
    using namespace std::chrono_literals;
    return (*this)(requestPipe, request, filter, 3s);
}

bool AtChannel::Conversation::send(const AtResponsePtr& response) {
    std::lock_guard<std::mutex> lock(mMtx);
    if (mFilter && (*mFilter)(*response)) {
        mFilter = nullptr;
        mSink.set_value(response);
        return true;
    } else if (mFilter && (response->isERROR() ||
                           response->holds<AtResponse::CmeError>() ||
                           response->holds<AtResponse::CmsError>())) {
        // Librem 5: the request failed and the caller does not handle this error; fail it
        // now (callers treat nullptr as a failed request) instead of after the timeout.
        mFilter = nullptr;
        mSink.set_value(nullptr);
        return true;
    } else {
        return false;
    }
}

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
