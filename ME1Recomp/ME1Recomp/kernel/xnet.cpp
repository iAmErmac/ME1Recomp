// xnet: offline XNet / Winsock stack. Startup succeeds and stream sockets fail with
// WSAENETDOWN, so the game sees no network; (SoD) datagram sockets work inside the
// process only (see "sockets"). WSA events are real kernel
// events, so the game's network thread blocks in WSAWaitForMultipleEvents instead of spinning.
#include "stdafx.h"
#include "function.h"
#include "thread.h"

#include <random>

namespace me1::imp
{
    using namespace me1::kernel;

    // Winsock error codes. Named k* so they don't collide with winsock.h macros.
    constexpr uint32_t kWsaEnetdown = 10050;
    constexpr uint32_t kWsaInvalidParameter = 87;
    constexpr uint32_t kWsaWaitFailed = 0xFFFFFFFF;
    constexpr uint32_t kWsaWaitEvent0 = 0;
    constexpr uint32_t kWsaWaitIoCompletion = 0xC0;
    constexpr uint32_t kWsaWaitTimeout = 0x102;
    constexpr uint32_t kWsaMaxEvents = 64;

    constexpr int32_t kSocketError = -1;
    constexpr uint32_t kInvalidSocket = 0xFFFFFFFF;
    constexpr uint32_t kInaddrNone = 0xFFFFFFFF;
    constexpr uint32_t kWaitInfinite = 0xFFFFFFFF;

    constexpr uint32_t kXnAddrNone = 0x00000001;      // XNET_GET_XNADDR_NONE
    constexpr uint32_t kConnectStatusIdle = 0x00000000;

    // Per-thread WSA last error.
    static thread_local uint32_t t_wsaLastError = 0;

    static void SetWsaError(uint32_t error)
    {
        t_wsaLastError = error;
    }

    // Returns true for the first few uses of an import, so logs show it was reached without spamming loops.
    static bool ShouldLog(const char* name)
    {
        static std::mutex mutex;
        static std::unordered_map<std::string, uint32_t> counts;
        std::lock_guard lock(mutex);
        return ++counts[name] <= 3;
    }

    // ---------------------------------------------------------------- structs

    struct X_WSADATA
    {
        u16 version;
        u16 versionHigh;
        char description[256 + 1];
        char systemStatus[128 + 1];
        u16 maxSockets;
        u16 maxUdpDg;
        u32 vendorInfoPtr;
    };
    static_assert(sizeof(X_WSADATA) == 0x190);

    struct X_XNADDR
    {
        u32 ina;
        u32 inaOnline;
        u16 portOnline;
        uint8_t enetAddress[6];
        uint8_t onlineIdentity[20];
    };
    static_assert(sizeof(X_XNADDR) == 0x24);

    // ---------------------------------------------------------------- helpers

    // Socket calls: every socket fails, so report WSAENETDOWN.
    static int32_t SocketError(const char* name)
    {
        if (ShouldLog(name))
            LOGI("{} -> WSAENETDOWN (networking offline)", name);
        SetWsaError(kWsaEnetdown);
        return kSocketError;
    }

    static uint32_t InvalidSocket(const char* name)
    {
        if (ShouldLog(name))
            LOGI("{} -> WSAENETDOWN (networking offline)", name);
        SetWsaError(kWsaEnetdown);
        return kInvalidSocket;
    }

    // XNet calls that return an error code directly.
    static int32_t XNetError(const char* name)
    {
        if (ShouldLog(name))
            LOGI("{} -> WSAENETDOWN (networking offline)", name);
        SetWsaError(kWsaEnetdown);
        return int32_t(kWsaEnetdown);
    }

    // Signals an event the game passed for an async call that is failed now, so no waiter hangs.
    static void SignalEventIfPresent(uint32_t handle)
    {
        if (!handle)
            return;
        if (auto event = LookupHandle<Event>(handle, ObjectType::Event))
            event->Set();
    }

    static void FillRandom(uint8_t* dst, uint32_t length)
    {
        if (!dst || !length)
            return;
        std::random_device rd;
        for (uint32_t i = 0; i < length; i += 4)
        {
            const uint32_t word = rd();
            memcpy(dst + i, &word, std::min<uint32_t>(4, length - i));
        }
    }

    // Dotted-quad IPv4 parser. Returns the address with the first octet in the high byte,
    // which is the big-endian value the guest reads.
    static uint32_t ParseIPv4(const char* s)
    {
        uint32_t value = 0;
        for (int part = 0; part < 4; part++)
        {
            uint32_t octet = 0;
            int digits = 0;
            while (*s >= '0' && *s <= '9')
            {
                octet = octet * 10 + uint32_t(*s - '0');
                if (octet > 255)
                    return kInaddrNone;
                s++;
                digits++;
            }
            if (!digits)
                return kInaddrNone;
            value = (value << 8) | octet;
            if (part < 3)
            {
                if (*s != '.')
                    return kInaddrNone;
                s++;
            }
        }
        return *s ? kInaddrNone : value;
    }

    // ---------------------------------------------------------------- Winsock core

    int32_t NetDll_WSAStartup(uint32_t caller, uint32_t version, X_WSADATA* data)
    {
        if (ShouldLog("NetDll_WSAStartup"))
            LOGI("NetDll_WSAStartup(version {:#x}) -> offline", version);
        if (data)
        {
            memset(data, 0, sizeof(*data));
            data->version = uint16_t(0x0202);
            data->versionHigh = uint16_t(0x0202);
        }
        return 0;
    }

    int32_t NetDll_WSACleanup(uint32_t caller)
    {
        return 0;
    }

    int32_t NetDll_WSAGetLastError()
    {
        return int32_t(t_wsaLastError);
    }

    // ---------------------------------------------------------------- WSA events

    uint32_t NetDll_WSACreateEvent()
    {
        auto event = std::make_shared<Event>(true, false); // manual-reset, initially non-signaled
        if (!AttachGuestHeader(event, sizeof(X_KEVENT)))
            return 0; // (WSA_INVALID_EVENT)
        return CreateHandle(event);
    }

    int32_t NetDll_WSACloseEvent(uint32_t event)
    {
        if (!LookupHandle<Event>(event, ObjectType::Event) || !CloseHandle(event))
        {
            SetWsaError(X_ERROR_INVALID_HANDLE);
            return 0;
        }
        return 1;
    }

    int32_t NetDll_WSASetEvent(uint32_t event)
    {
        auto e = LookupHandle<Event>(event, ObjectType::Event);
        if (!e)
        {
            SetWsaError(X_ERROR_INVALID_HANDLE);
            return 0;
        }
        e->Set();
        return 1;
    }

    int32_t NetDll_WSAResetEvent(uint32_t event)
    {
        auto e = LookupHandle<Event>(event, ObjectType::Event);
        if (!e)
        {
            SetWsaError(X_ERROR_INVALID_HANDLE);
            return 0;
        }
        e->Reset();
        return 1;
    }

    // Blocks in the kernel dispatcher. Timeout 0xFFFFFFFF is infinite; otherwise milliseconds.
    uint32_t NetDll_WSAWaitForMultipleEvents(uint32_t count, u32* events, uint32_t waitAll, uint32_t timeout, uint32_t alertable)
    {
        if (ShouldLog("NetDll_WSAWaitForMultipleEvents"))
            LOGI("NetDll_WSAWaitForMultipleEvents(count {}, waitAll {}, timeout {:#x}, alertable {})", count, waitAll, timeout, alertable);

        if (!events || !count || count > kWsaMaxEvents)
        {
            SetWsaError(kWsaInvalidParameter);
            return kWsaWaitFailed;
        }

        std::vector<ObjectRef> objects;
        objects.reserve(count);
        for (uint32_t i = 0; i < count; i++)
        {
            auto event = LookupHandle<Event>(uint32_t(events[i]), ObjectType::Event);
            if (!event)
            {
                SetWsaError(X_ERROR_INVALID_HANDLE);
                return kWsaWaitFailed;
            }
            objects.push_back(std::move(event));
        }

        // Kernel timeouts are 100 ns units; negative means relative.
        const bool infinite = timeout == kWaitInfinite;
        int64_t ticks = infinite ? 0 : -int64_t(timeout) * 10000;
        const uint32_t status = WaitMultiple(objects, waitAll != 0, infinite ? nullptr : &ticks, alertable != 0);

        if (status == X_STATUS_TIMEOUT)
            return kWsaWaitTimeout;
        if (status == X_STATUS_USER_APC)
            return kWsaWaitIoCompletion;
        if (status < count)
            return kWsaWaitEvent0 + status;

        SetWsaError(X_ERROR_INVALID_HANDLE);
        return kWsaWaitFailed;
    }

    // ---------------------------------------------------------------- XNet

    int32_t NetDll_XNetStartup(uint32_t caller, uint32_t params)
    {
        if (ShouldLog("NetDll_XNetStartup"))
            LOGI("NetDll_XNetStartup -> offline");
        return 0;
    }

    int32_t NetDll_XNetCleanup(uint32_t caller, uint32_t params)
    {
        return 0;
    }

    int32_t NetDll_XNetRandom(uint32_t caller, uint8_t* buffer, uint32_t length)
    {
        FillRandom(buffer, length);
        return 0;
    }

    uint32_t NetDll_XNetGetEthernetLinkStatus(uint32_t caller)
    {
        return 0; // no link
    }

    uint32_t NetDll_XNetGetTitleXnAddr(uint32_t caller, X_XNADDR* addr)
    {
        if (addr)
            memset(addr, 0, sizeof(*addr));
        if (ShouldLog("NetDll_XNetGetTitleXnAddr"))
            LOGI("NetDll_XNetGetTitleXnAddr -> XNET_GET_XNADDR_NONE");
        return kXnAddrNone;
    }

    uint32_t NetDll_XNetGetConnectStatus(uint32_t caller, uint32_t ina)
    {
        return kConnectStatusIdle;
    }

    int32_t NetDll_XNetConnect(uint32_t caller, uint32_t ina)
    {
        return XNetError("NetDll_XNetConnect");
    }

    int32_t NetDll_XNetCreateKey(uint32_t caller, uint8_t* keyId, uint8_t* exchangeKey)
    {
        FillRandom(keyId, 8);
        FillRandom(exchangeKey, 16);
        return 0;
    }

    int32_t NetDll_XNetRegisterKey(uint32_t caller, uint8_t* keyId, uint8_t* exchangeKey)
    {
        return 0;
    }

    int32_t NetDll_XNetUnregisterKey(uint32_t caller, uint8_t* keyId)
    {
        return 0;
    }

    int32_t NetDll_XNetDnsLookup(uint32_t caller, uint32_t host, uint32_t eventHandle, u32* dnsOut)
    {
        if (dnsOut)
            *dnsOut = 0;
        SignalEventIfPresent(eventHandle);
        return XNetError("NetDll_XNetDnsLookup");
    }

    int32_t NetDll_XNetDnsRelease(uint32_t caller, uint32_t dns)
    {
        return 0;
    }

    uint32_t NetDll_XNetInAddrToString(uint32_t caller, uint32_t inAddr, char* out, uint32_t maxLength)
    {
        if (!out || !maxLength)
            return 0;
        const std::string text = fmt::format("{}.{}.{}.{}", inAddr >> 24, (inAddr >> 16) & 0xFF, (inAddr >> 8) & 0xFF, inAddr & 0xFF);
        const size_t length = std::min<size_t>(text.size(), maxLength - 1);
        memcpy(out, text.data(), length);
        out[length] = '\0';
        return 0;
    }

    int32_t NetDll_XNetServerToInAddr(uint32_t caller, uint32_t ina, uint32_t serviceId, u32* inAddrOut)
    {
        if (inAddrOut)
            *inAddrOut = 0;
        return XNetError("NetDll_XNetServerToInAddr");
    }

    int32_t NetDll_XNetQosListen(uint32_t caller, uint32_t id, uint32_t data, uint32_t dataSize, uint32_t unknown, uint32_t flags)
    {
        return XNetError("NetDll_XNetQosListen");
    }

    // 12 arguments after the caller; the last two are hEvent and the XNQOS** output.
    int32_t NetDll_XNetQosLookup(uint32_t caller, uint32_t cxna, uint32_t apxna, uint32_t apxkid, uint32_t apxkey,
        uint32_t cina, uint32_t aina, uint32_t serviceIds, uint32_t cPorts, uint32_t aPorts,
        uint32_t flags, uint32_t eventHandle, u32* qosOut)
    {
        if (qosOut)
            *qosOut = 0;
        SignalEventIfPresent(eventHandle);
        return XNetError("NetDll_XNetQosLookup");
    }

    int32_t NetDll_XNetQosRelease(uint32_t caller, uint32_t qos)
    {
        return 0;
    }

    int32_t NetDll_XNetQosServiceLookup(uint32_t caller, uint32_t flags, uint32_t eventHandle, u32* qosOut)
    {
        if (qosOut)
            *qosOut = 0;
        SignalEventIfPresent(eventHandle);
        return XNetError("NetDll_XNetQosServiceLookup");
    }

    // ---------------------------------------------------------------- sockets
    // (SoD) An in-process datagram network. CryEngine runs its single-player game server and
    // client over UDP even inside one process (CGameContext binds "0.0.0.0:<port>") and doesn't
    // start a game without it. A datagram sent to any address goes to the socket of this
    // process bound to its port, if there is one; nothing reaches the real network. Stream
    // sockets still fail with WSAENETDOWN.

    constexpr uint32_t kWsaIoIncomplete = 996;
    constexpr uint32_t kWsaIoPending = 997;
    constexpr uint32_t kWsaOperationAborted = 995;
    constexpr uint32_t kWsaEinval = 10022;
    constexpr uint32_t kWsaEwouldblock = 10035;
    constexpr uint32_t kWsaEnotsock = 10038;
    constexpr uint32_t kWsaEafnosupport = 10047;
    constexpr uint32_t kWsaEaddrinuse = 10048;
    constexpr uint32_t kStatusPending = 0x103;
    constexpr uint32_t kStatusCancelled = 0xC0000120;
    constexpr uint32_t kAfInet = 2;
    constexpr uint32_t kSockDgram = 2;
    constexpr uint32_t kFdRead = 1;
    constexpr uint32_t kFdWrite = 2;
    constexpr uint32_t kFionbio = 0x8004667E;
    constexpr uint32_t kFionread = 0x4004667F;
    constexpr uint32_t kLoopback = 0x7F000001;
    constexpr uint32_t kFirstSocket = 0x4000; // callers are small numbers, sockets never are

    struct X_SOCKADDR_IN
    {
        u16 family;
        u16 port;
        u32 addr;
        uint8_t zero[8];
    };

    struct X_WSABUF
    {
        u32 length;
        u32 buffer;
    };

    struct X_WSAOVERLAPPED
    {
        u32 internal;
        u32 internalHigh;
        u32 offset;
        u32 offsetHigh;
        u32 event;
    };

    struct X_FD_SET
    {
        u32 count;
        u32 sockets[64];
    };

    struct X_TIMEVAL
    {
        s32 seconds;
        s32 microseconds;
    };

    struct Datagram
    {
        uint32_t fromAddr;
        uint16_t fromPort;
        std::vector<uint8_t> data;
    };

    // An overlapped receive the game left pending: the guest addresses of its arguments.
    struct PendingReceive
    {
        uint32_t buffers, bufferCount, bytesReceived, flags, from, fromLength, overlapped;
    };

    struct VirtualSocket
    {
        uint16_t port = 0;
        bool nonblocking = false;
        uint32_t peerAddr = 0;
        uint16_t peerPort = 0;
        std::deque<Datagram> queue;
        std::deque<PendingReceive> pending;
        uint32_t event = 0;
        uint32_t eventMask = 0;
    };

    static std::mutex s_netMutex;
    static std::condition_variable s_netCv;
    static std::unordered_map<uint32_t, VirtualSocket> s_sockets;
    static uint32_t s_nextSocket = kFirstSocket;
    static uint16_t s_nextEphemeral = 49152;

    static VirtualSocket* FindSocket(uint32_t s)
    {
        auto it = s_sockets.find(s);
        return it == s_sockets.end() ? nullptr : &it->second;
    }

    static VirtualSocket* FindPort(uint16_t port)
    {
        for (auto& [handle, socket] : s_sockets)
            if (socket.port == port)
                return &socket;
        return nullptr;
    }

    static uint16_t EphemeralPort()
    {
        for (;;)
        {
            const uint16_t port = s_nextEphemeral++;
            if (!s_nextEphemeral)
                s_nextEphemeral = 49152;
            if (!FindPort(port))
                return port;
        }
    }

    static int32_t Fail(uint32_t error)
    {
        SetWsaError(error);
        return kSocketError;
    }

    static void WriteAddress(uint32_t address, uint32_t lengthAddress, uint32_t ip, uint16_t port)
    {
        if (address)
        {
            auto* sa = g_memory.Translate<X_SOCKADDR_IN>(address);
            memset(sa, 0, sizeof(*sa));
            sa->family = uint16_t(kAfInet);
            sa->port = port;
            sa->addr = ip;
        }
        if (lengthAddress)
            *g_memory.Translate<u32>(lengthAddress) = uint32_t(sizeof(X_SOCKADDR_IN));
    }

    // Copies a datagram into the guest's buffers; what doesn't fit is dropped, as in UDP.
    static uint32_t Scatter(uint32_t buffers, uint32_t bufferCount, const std::vector<uint8_t>& data)
    {
        uint32_t copied = 0;
        const auto* bufs = g_memory.Translate<X_WSABUF>(buffers);
        for (uint32_t i = 0; i < bufferCount && copied < data.size(); i++)
        {
            const uint32_t n = std::min<uint32_t>(bufs[i].length, uint32_t(data.size()) - copied);
            memcpy(g_memory.Translate(bufs[i].buffer), data.data() + copied, n);
            copied += n;
        }
        return copied;
    }

    static std::vector<uint8_t> Gather(uint32_t buffers, uint32_t bufferCount)
    {
        std::vector<uint8_t> data;
        const auto* bufs = g_memory.Translate<X_WSABUF>(buffers);
        for (uint32_t i = 0; i < bufferCount; i++)
        {
            const auto* p = g_memory.Translate<uint8_t>(bufs[i].buffer);
            data.insert(data.end(), p, p + uint32_t(bufs[i].length));
        }
        return data;
    }

    static void CompleteOverlapped(uint32_t overlapped, uint32_t status, uint32_t bytes)
    {
        if (!overlapped)
            return;
        auto* o = g_memory.Translate<X_WSAOVERLAPPED>(overlapped);
        o->internalHigh = bytes;
        o->internal = status;
        SignalEventIfPresent(o->event);
    }

    static void FinishReceive(const PendingReceive& r, const Datagram& datagram)
    {
        const uint32_t n = Scatter(r.buffers, r.bufferCount, datagram.data);
        if (r.bytesReceived)
            *g_memory.Translate<u32>(r.bytesReceived) = n;
        if (r.flags)
            *g_memory.Translate<u32>(r.flags) = 0u;
        WriteAddress(r.from, r.fromLength, datagram.fromAddr, datagram.fromPort);
        CompleteOverlapped(r.overlapped, X_STATUS_SUCCESS, n);
    }

    // Hands a datagram to the socket bound to `port`. Caller holds s_netMutex.
    static void Deliver(uint16_t port, Datagram datagram)
    {
        VirtualSocket* socket = FindPort(port);
        if (!socket)
            return; // nobody bound: dropped, as in UDP
        if (!socket->pending.empty())
        {
            const PendingReceive r = socket->pending.front();
            socket->pending.pop_front();
            FinishReceive(r, datagram);
        }
        else
        {
            socket->queue.push_back(std::move(datagram));
            if (socket->eventMask & kFdRead)
                SignalEventIfPresent(socket->event);
        }
        s_netCv.notify_all();
    }

    static int32_t SendDatagram(uint32_t s, std::vector<uint8_t> data, uint32_t to)
    {
        std::lock_guard lock(s_netMutex);
        VirtualSocket* socket = FindSocket(s);
        if (!socket)
            return Fail(kWsaEnotsock);
        const uint16_t port = to ? uint16_t(g_memory.Translate<X_SOCKADDR_IN>(to)->port) : socket->peerPort;
        if (!port)
            return Fail(kWsaEinval);
        if (!socket->port)
            socket->port = EphemeralPort(); // an unbound socket gets a port on its first send
        const int32_t length = int32_t(data.size());
        Deliver(port, { kLoopback, socket->port, std::move(data) });
        return length;
    }

    // Takes the next datagram of socket `s` into `out`, blocking unless the socket is
    // non-blocking. Returns 0, or a WSA error. Caller holds `lock` on s_netMutex.
    static uint32_t TakeDatagram(std::unique_lock<std::mutex>& lock, uint32_t s, Datagram& out)
    {
        VirtualSocket* socket = FindSocket(s);
        if (!socket)
            return kWsaEnotsock;
        if (socket->queue.empty())
        {
            if (socket->nonblocking)
                return kWsaEwouldblock;
            s_netCv.wait(lock, [&] { socket = FindSocket(s); return !socket || !socket->queue.empty(); });
            if (!socket)
                return kWsaEnotsock;
        }
        out = std::move(socket->queue.front());
        socket->queue.pop_front();
        return 0;
    }

    // WSARecv / WSARecvFrom. An overlapped receive with nothing queued is left pending and
    // completes on delivery.
    static int32_t ReceiveDatagram(uint32_t s, const PendingReceive& r)
    {
        std::unique_lock lock(s_netMutex);
        VirtualSocket* socket = FindSocket(s);
        if (!socket)
            return Fail(kWsaEnotsock);
        if (r.overlapped && socket->queue.empty())
        {
            g_memory.Translate<X_WSAOVERLAPPED>(r.overlapped)->internal = kStatusPending;
            socket->pending.push_back(r);
            return Fail(kWsaIoPending);
        }
        Datagram datagram;
        if (const uint32_t error = TakeDatagram(lock, s, datagram))
            return Fail(error);
        FinishReceive(r, datagram);
        return 0;
    }

    static void CancelPending(VirtualSocket& socket)
    {
        for (const PendingReceive& r : socket.pending)
            CompleteOverlapped(r.overlapped, kStatusCancelled, 0);
        socket.pending.clear();
        s_netCv.notify_all();
    }

    uint32_t NetDll_socket(uint32_t caller, uint32_t af, uint32_t type, uint32_t protocol)
    {
        if (af != kAfInet)
        {
            SetWsaError(kWsaEafnosupport);
            return kInvalidSocket;
        }
        if (type != kSockDgram)
            return InvalidSocket("NetDll_socket (stream)");
        std::lock_guard lock(s_netMutex);
        const uint32_t s = s_nextSocket;
        s_nextSocket += 4;
        s_sockets.emplace(s, VirtualSocket{});
        if (ShouldLog("NetDll_socket"))
            LOGI("NetDll_socket(datagram, protocol {}) -> {:#x} (in-process network)", protocol, s);
        return s;
    }

    int32_t NetDll_closesocket(uint32_t caller, uint32_t s)
    {
        std::lock_guard lock(s_netMutex);
        VirtualSocket* socket = FindSocket(s);
        if (!socket)
            return Fail(kWsaEnotsock);
        CancelPending(*socket);
        s_sockets.erase(s);
        return 0;
    }

    int32_t NetDll_bind(uint32_t caller, uint32_t s, uint32_t name, uint32_t namelen)
    {
        if (!name)
            return Fail(kWsaEinval);
        std::lock_guard lock(s_netMutex);
        VirtualSocket* socket = FindSocket(s);
        if (!socket)
            return Fail(kWsaEnotsock);
        uint16_t port = g_memory.Translate<X_SOCKADDR_IN>(name)->port;
        if (!port)
            port = EphemeralPort();
        else if (VirtualSocket* other = FindPort(port); other && other != socket)
            return Fail(kWsaEaddrinuse);
        socket->port = port;
        if (ShouldLog("NetDll_bind"))
            LOGI("NetDll_bind({:#x}) -> port {}", s, port);
        return 0;
    }

    int32_t NetDll_connect(uint32_t caller, uint32_t s, uint32_t name, uint32_t namelen)
    {
        if (!name)
            return Fail(kWsaEinval);
        std::lock_guard lock(s_netMutex);
        VirtualSocket* socket = FindSocket(s);
        if (!socket)
            return Fail(kWsaEnotsock);
        const auto* sa = g_memory.Translate<X_SOCKADDR_IN>(name);
        socket->peerAddr = sa->addr;
        socket->peerPort = sa->port;
        if (!socket->port)
            socket->port = EphemeralPort();
        return 0;
    }

    int32_t NetDll_listen(uint32_t caller, uint32_t s, uint32_t backlog)
    {
        return SocketError("NetDll_listen");
    }

    uint32_t NetDll_accept(uint32_t caller, uint32_t s, uint32_t addr, u32* addrLength)
    {
        return InvalidSocket("NetDll_accept");
    }

    int32_t NetDll_shutdown(uint32_t caller, uint32_t s, uint32_t how)
    {
        return 0;
    }

    int32_t NetDll_send(uint32_t caller, uint32_t s, uint32_t buffer, uint32_t length, uint32_t flags)
    {
        const auto* p = g_memory.Translate<uint8_t>(buffer);
        return SendDatagram(s, std::vector<uint8_t>(p, p + length), 0);
    }

    int32_t NetDll_sendto(uint32_t caller, uint32_t s, uint32_t buffer, uint32_t length, uint32_t flags, uint32_t to, uint32_t toLength)
    {
        const auto* p = g_memory.Translate<uint8_t>(buffer);
        return SendDatagram(s, std::vector<uint8_t>(p, p + length), to);
    }

    int32_t NetDll_WSASendTo(uint32_t caller, uint32_t s, uint32_t buffers, uint32_t bufferCount, u32* bytesSent,
        uint32_t flags, uint32_t to, uint32_t toLength, uint32_t overlapped, uint32_t completionRoutine)
    {
        const int32_t n = SendDatagram(s, Gather(buffers, bufferCount), to);
        if (n < 0)
            return n;
        if (bytesSent)
            *bytesSent = uint32_t(n);
        CompleteOverlapped(overlapped, X_STATUS_SUCCESS, uint32_t(n));
        return 0;
    }

    int32_t NetDll_WSASend(uint32_t caller, uint32_t s, uint32_t buffers, uint32_t bufferCount, u32* bytesSent,
        uint32_t flags, uint32_t overlapped, uint32_t completionRoutine)
    {
        return NetDll_WSASendTo(caller, s, buffers, bufferCount, bytesSent, flags, 0, 0, overlapped, completionRoutine);
    }

    int32_t NetDll_recvfrom(uint32_t caller, uint32_t s, uint32_t buffer, uint32_t length, uint32_t flags, uint32_t from, u32* fromLength)
    {
        std::unique_lock lock(s_netMutex);
        Datagram datagram;
        if (const uint32_t error = TakeDatagram(lock, s, datagram))
            return Fail(error);
        const uint32_t n = std::min<uint32_t>(length, uint32_t(datagram.data.size()));
        memcpy(g_memory.Translate(buffer), datagram.data.data(), n);
        WriteAddress(from, g_memory.ToGuest(fromLength), datagram.fromAddr, datagram.fromPort);
        return int32_t(n);
    }

    int32_t NetDll_recv(uint32_t caller, uint32_t s, uint32_t buffer, uint32_t length, uint32_t flags)
    {
        return NetDll_recvfrom(caller, s, buffer, length, flags, 0, nullptr);
    }

    int32_t NetDll_WSARecvFrom(uint32_t caller, uint32_t s, uint32_t buffers, uint32_t bufferCount, u32* bytesReceived,
        u32* flags, uint32_t from, u32* fromLength, uint32_t overlapped, uint32_t completionRoutine)
    {
        return ReceiveDatagram(s, { buffers, bufferCount, g_memory.ToGuest(bytesReceived), g_memory.ToGuest(flags), from,
            g_memory.ToGuest(fromLength), overlapped });
    }

    int32_t NetDll_WSARecv(uint32_t caller, uint32_t s, uint32_t buffers, uint32_t bufferCount, u32* bytesReceived,
        u32* flags, uint32_t overlapped, uint32_t completionRoutine)
    {
        return ReceiveDatagram(s, { buffers, bufferCount, g_memory.ToGuest(bytesReceived), g_memory.ToGuest(flags), 0, 0,
            overlapped });
    }

    int32_t NetDll_WSAGetOverlappedResult(uint32_t caller, uint32_t s, uint32_t overlapped, u32* transferred, uint32_t wait, u32* flags)
    {
        if (!overlapped)
        {
            SetWsaError(kWsaEinval);
            return 0; // FALSE
        }
        auto* o = g_memory.Translate<X_WSAOVERLAPPED>(overlapped);
        if (uint32_t(o->internal) == kStatusPending)
        {
            if (!wait)
            {
                SetWsaError(kWsaIoIncomplete);
                return 0;
            }
            std::unique_lock lock(s_netMutex);
            s_netCv.wait(lock, [&] { return uint32_t(o->internal) != kStatusPending; });
        }
        if (transferred)
            *transferred = uint32_t(o->internalHigh);
        if (flags)
            *flags = 0u;
        if (uint32_t(o->internal) != X_STATUS_SUCCESS)
        {
            SetWsaError(kWsaOperationAborted);
            return 0;
        }
        return 1;
    }

    // No caller found in SoD to confirm the arguments: (caller, socket) or (socket).
    int32_t NetDll_WSACancelOverlappedIO(uint32_t a, uint32_t b)
    {
        const uint32_t s = a >= kFirstSocket ? a : b;
        std::lock_guard lock(s_netMutex);
        VirtualSocket* socket = FindSocket(s);
        if (!socket)
            return Fail(kWsaEnotsock);
        CancelPending(*socket);
        return 0;
    }

    // (socket, event, mask), with or without the caller argument in front, as above.
    int32_t NetDll_WSAEventSelect(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
    {
        const bool withCaller = a < kFirstSocket;
        const uint32_t s = withCaller ? b : a;
        const uint32_t event = withCaller ? c : b;
        const uint32_t mask = withCaller ? d : c;
        std::lock_guard lock(s_netMutex);
        VirtualSocket* socket = FindSocket(s);
        if (!socket)
            return Fail(kWsaEnotsock);
        socket->event = event;
        socket->eventMask = mask;
        if ((mask & kFdWrite) || ((mask & kFdRead) && !socket->queue.empty()))
            SignalEventIfPresent(event);
        return 0;
    }

    // Datagram sockets can always send; a read set waits for a queued datagram.
    int32_t NetDll_select(uint32_t caller, uint32_t nfds, uint32_t readfds, uint32_t writefds, uint32_t exceptfds, uint32_t timeout)
    {
        auto* reads = readfds ? g_memory.Translate<X_FD_SET>(readfds) : nullptr;
        auto* writes = writefds ? g_memory.Translate<X_FD_SET>(writefds) : nullptr;
        std::vector<uint32_t> wanted;
        for (uint32_t i = 0; reads && i < std::min<uint32_t>(reads->count, 64); i++)
            wanted.push_back(reads->sockets[i]);
        const uint32_t writable = writes ? std::min<uint32_t>(writes->count, 64) : 0;
        if (exceptfds)
            g_memory.Translate<X_FD_SET>(exceptfds)->count = 0u;

        std::unique_lock lock(s_netMutex);
        const auto readable = [&] {
            std::vector<uint32_t> ready;
            for (uint32_t s : wanted)
                if (VirtualSocket* socket = FindSocket(s); socket && !socket->queue.empty())
                    ready.push_back(s);
            return ready;
        };
        if (!writable && readable().empty())
        {
            if (!timeout)
                s_netCv.wait(lock, [&] { return !readable().empty(); });
            else
            {
                const auto* tv = g_memory.Translate<X_TIMEVAL>(timeout);
                const auto span = std::chrono::seconds(int32_t(tv->seconds)) + std::chrono::microseconds(int32_t(tv->microseconds));
                s_netCv.wait_for(lock, span, [&] { return !readable().empty(); });
            }
        }
        const std::vector<uint32_t> ready = readable();
        if (reads)
        {
            reads->count = uint32_t(ready.size());
            for (size_t i = 0; i < ready.size(); i++)
                reads->sockets[i] = ready[i];
        }
        return int32_t(ready.size() + writable);
    }

    // No caller argument on this one (as in Xenia).
    int32_t NetDll___WSAFDIsSet(uint32_t s, uint32_t set)
    {
        if (!set)
            return 0;
        const auto* fds = g_memory.Translate<X_FD_SET>(set);
        for (uint32_t i = 0; i < std::min<uint32_t>(fds->count, 64); i++)
            if (uint32_t(fds->sockets[i]) == s)
                return 1;
        return 0;
    }

    int32_t NetDll_ioctlsocket(uint32_t caller, uint32_t s, uint32_t cmd, uint32_t argp)
    {
        std::lock_guard lock(s_netMutex);
        VirtualSocket* socket = FindSocket(s);
        if (!socket)
            return Fail(kWsaEnotsock);
        if (cmd == kFionbio && argp)
            socket->nonblocking = uint32_t(*g_memory.Translate<u32>(argp)) != 0;
        else if (cmd == kFionread && argp)
            *g_memory.Translate<u32>(argp) = socket->queue.empty() ? 0u : uint32_t(socket->queue.front().data.size());
        return 0;
    }

    int32_t NetDll_setsockopt(uint32_t caller, uint32_t s, uint32_t level, uint32_t optname, uint32_t optval, uint32_t optlen)
    {
        std::lock_guard lock(s_netMutex);
        return FindSocket(s) ? 0 : Fail(kWsaEnotsock);
    }

    // Every option reads 64 K: right for the buffer sizes, harmless for the flags.
    int32_t NetDll_getsockopt(uint32_t caller, uint32_t s, uint32_t level, uint32_t optname, uint32_t optval, u32* optlen)
    {
        std::lock_guard lock(s_netMutex);
        if (!FindSocket(s))
            return Fail(kWsaEnotsock);
        if (optval && optlen && uint32_t(*optlen) >= 4)
        {
            *g_memory.Translate<u32>(optval) = 0x10000u;
            *optlen = 4u;
        }
        return 0;
    }

    int32_t NetDll_getsockname(uint32_t caller, uint32_t s, uint32_t name, u32* namelen)
    {
        std::lock_guard lock(s_netMutex);
        VirtualSocket* socket = FindSocket(s);
        if (!socket)
            return Fail(kWsaEnotsock);
        WriteAddress(name, g_memory.ToGuest(namelen), kLoopback, socket->port);
        return 0;
    }

    int32_t NetDll_getpeername(uint32_t caller, uint32_t s, uint32_t name, u32* namelen)
    {
        std::lock_guard lock(s_netMutex);
        VirtualSocket* socket = FindSocket(s);
        if (!socket)
            return Fail(kWsaEnotsock);
        WriteAddress(name, g_memory.ToGuest(namelen), socket->peerAddr, socket->peerPort);
        return 0;
    }

    // No caller argument on this one. Dotted-quad only; the empty string gives 0 as on console.
    uint32_t NetDll_inet_addr(const char* text)
    {
        if (!text)
            return kInaddrNone;
        if (!text[0])
            return 0;
        return ParseIPv4(text);
    }
}

ME1_IMPORT_AS(NetDll_WSAStartup, me1::imp::NetDll_WSAStartup)
ME1_IMPORT_AS(NetDll_WSACleanup, me1::imp::NetDll_WSACleanup)
ME1_IMPORT_AS(NetDll_WSAGetLastError, me1::imp::NetDll_WSAGetLastError)
ME1_IMPORT_AS(NetDll_WSACreateEvent, me1::imp::NetDll_WSACreateEvent)
ME1_IMPORT_AS(NetDll_WSACloseEvent, me1::imp::NetDll_WSACloseEvent)
ME1_IMPORT_AS(NetDll_WSASetEvent, me1::imp::NetDll_WSASetEvent)
ME1_IMPORT_AS(NetDll_WSAResetEvent, me1::imp::NetDll_WSAResetEvent)
ME1_IMPORT_AS(NetDll_WSAWaitForMultipleEvents, me1::imp::NetDll_WSAWaitForMultipleEvents)
ME1_IMPORT_AS(NetDll_WSAGetOverlappedResult, me1::imp::NetDll_WSAGetOverlappedResult)
ME1_IMPORT_AS(NetDll_WSARecv, me1::imp::NetDll_WSARecv)
ME1_IMPORT_AS(NetDll_WSARecvFrom, me1::imp::NetDll_WSARecvFrom)
ME1_IMPORT_AS(NetDll_XNetStartup, me1::imp::NetDll_XNetStartup)
ME1_IMPORT_AS(NetDll_XNetCleanup, me1::imp::NetDll_XNetCleanup)
ME1_IMPORT_AS(NetDll_XNetRandom, me1::imp::NetDll_XNetRandom)
ME1_IMPORT_AS(NetDll_XNetGetEthernetLinkStatus, me1::imp::NetDll_XNetGetEthernetLinkStatus)
ME1_IMPORT_AS(NetDll_XNetGetTitleXnAddr, me1::imp::NetDll_XNetGetTitleXnAddr)
ME1_IMPORT_AS(NetDll_XNetGetConnectStatus, me1::imp::NetDll_XNetGetConnectStatus)
ME1_IMPORT_AS(NetDll_XNetConnect, me1::imp::NetDll_XNetConnect)
ME1_IMPORT_AS(NetDll_XNetCreateKey, me1::imp::NetDll_XNetCreateKey)
ME1_IMPORT_AS(NetDll_XNetRegisterKey, me1::imp::NetDll_XNetRegisterKey)
ME1_IMPORT_AS(NetDll_XNetUnregisterKey, me1::imp::NetDll_XNetUnregisterKey)
ME1_IMPORT_AS(NetDll_XNetDnsLookup, me1::imp::NetDll_XNetDnsLookup)
ME1_IMPORT_AS(NetDll_XNetDnsRelease, me1::imp::NetDll_XNetDnsRelease)
ME1_IMPORT_AS(NetDll_XNetInAddrToString, me1::imp::NetDll_XNetInAddrToString)
ME1_IMPORT_AS(NetDll_XNetServerToInAddr, me1::imp::NetDll_XNetServerToInAddr)
ME1_IMPORT_AS(NetDll_XNetQosListen, me1::imp::NetDll_XNetQosListen)
ME1_IMPORT_AS(NetDll_XNetQosLookup, me1::imp::NetDll_XNetQosLookup)
ME1_IMPORT_AS(NetDll_XNetQosRelease, me1::imp::NetDll_XNetQosRelease)
ME1_IMPORT_AS(NetDll_XNetQosServiceLookup, me1::imp::NetDll_XNetQosServiceLookup)
ME1_IMPORT_AS(NetDll_socket, me1::imp::NetDll_socket)
ME1_IMPORT_AS(NetDll_closesocket, me1::imp::NetDll_closesocket)
ME1_IMPORT_AS(NetDll_bind, me1::imp::NetDll_bind)
ME1_IMPORT_AS(NetDll_connect, me1::imp::NetDll_connect)
ME1_IMPORT_AS(NetDll_listen, me1::imp::NetDll_listen)
ME1_IMPORT_AS(NetDll_accept, me1::imp::NetDll_accept)
ME1_IMPORT_AS(NetDll_shutdown, me1::imp::NetDll_shutdown)
ME1_IMPORT_AS(NetDll_send, me1::imp::NetDll_send)
ME1_IMPORT_AS(NetDll_sendto, me1::imp::NetDll_sendto)
ME1_IMPORT_AS(NetDll_recv, me1::imp::NetDll_recv)
ME1_IMPORT_AS(NetDll_recvfrom, me1::imp::NetDll_recvfrom)
ME1_IMPORT_AS(NetDll_select, me1::imp::NetDll_select)
ME1_IMPORT_AS(NetDll_ioctlsocket, me1::imp::NetDll_ioctlsocket)
ME1_IMPORT_AS(NetDll_setsockopt, me1::imp::NetDll_setsockopt)
ME1_IMPORT_AS(NetDll_getsockopt, me1::imp::NetDll_getsockopt)
ME1_IMPORT_AS(NetDll_getsockname, me1::imp::NetDll_getsockname)
ME1_IMPORT_AS(NetDll_getpeername, me1::imp::NetDll_getpeername)
ME1_IMPORT_AS(NetDll_inet_addr, me1::imp::NetDll_inet_addr)
ME1_IMPORT_AS(NetDll_WSASend, me1::imp::NetDll_WSASend)
ME1_IMPORT_AS(NetDll_WSASendTo, me1::imp::NetDll_WSASendTo)
ME1_IMPORT_AS(NetDll_WSAEventSelect, me1::imp::NetDll_WSAEventSelect)
ME1_IMPORT_AS(NetDll_WSACancelOverlappedIO, me1::imp::NetDll_WSACancelOverlappedIO)
ME1_IMPORT_AS(NetDll___WSAFDIsSet, me1::imp::NetDll___WSAFDIsSet)
