#include "include/api/RPC.h"
#include <utility>

#include "include/global/Configs.hpp"

#include <QLocalSocket>
#include <QDataStream>
#include <QAtomicInt>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QDebug>
#include <QPointer>

#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <exception>
#include <memory>

namespace API {

    // -----------------------------------------------------------------------
    // LocalSocketChannel — multiplexed framing over QLocalSocket
    //
    // Request wire format (little-endian):
    //   [uint32: request ID]
    //   [uint16: method name length][method name bytes]
    //   [uint32: payload length][payload bytes]
    //
    // Response wire format (little-endian):
    //   [uint32: request ID]
    //   [uint8:  status (0=OK, 1=error)]
    //   [uint32: data length][data bytes]
    //
    // The channel is created once and lives for the whole app. On core
    // restart the *connection* is replaced via Reconnect(): the old socket
    // is torn down and the new one adopted, all on the io_thread, while the
    // channel object (and therefore `defaultClient`) stays stable so that
    // worker threads calling into it never touch freed memory.
    //
    // `sock` and `read_buf` are io_thread-only state. All socket operations
    // are dispatched as queued lambdas to `io_anchor`, a stable QObject that
    // lives on io_thread for the channel's entire lifetime — callers never
    // dereference `sock` directly, so swapping it cannot race with a write.
    //
    // PendingCall is owned through a shared_ptr held jointly by Call() and
    // the `pending` map, so a timed-out / spuriously-woken Call() can never
    // destroy it while the io_thread is still writing to it or notifying.
    // -----------------------------------------------------------------------
    class Client::LocalSocketChannel 
    {

        struct PendingCall 
        {
            std::mutex          mu;
            std::condition_variable cv;
            bool                done   = false;
            quint8              status = 1;     // default: error
            QByteArray          data;
        };

        QThread      *io_thread;
        QObject      *io_anchor;          // stable dispatch target on io_thread
        QLocalSocket *sock = nullptr;     // io_thread only
        QByteArray    read_buf;           // io_thread only
        QAtomicInt    next_id{1};         // monotonic across reconnects
        std::mutex    pending_mu;
        QMap<quint32, std::shared_ptr<PendingCall>> pending;
        std::atomic_bool connected_{ false };
        std::atomic<quint64> connectionEpoch_{ 0 };
        // Last successfully adopted Core process generation.
        // Never reset on disconnect; prevents stale readoption.
        std::atomic<quint64> activeProcessGeneration_{ 0 };

        static constexpr int kIOTimeoutMs = 30000;
        static constexpr qsizetype kMaxRPCPayloadBytes = 16 * 1024 * 1024;
        static constexpr qsizetype kMaxRPCMethodBytes = 128;
        static constexpr qint64 kRPCReadChunkBytes = 64 * 1024;

        void failProtocol(const QString& reason)
        {
            qWarning().noquote() << "[RPC] Protocol violation:" << reason;
            read_buf.clear();
            qWarning() << "[RPC][WAKE] Cause: protocol violation";
            // Invalidate the current connection generationand wake all pending RPC callers.
            wakeAllWithError();

            if (sock)
            {
                sock->abort();
            }
        }
        // Called on io_thread via readyRead
        void onReadyRead()
        {
            if (!sock)
            {
                return;
            }

            while (sock->bytesAvailable() > 0)
            {
                const QByteArray chunk = sock->read(kRPCReadChunkBytes);

                if (chunk.isEmpty())
                {
                    break;
                }

                read_buf.append(chunk);

                if (!processBuffer())
                {
                    return;
                }
            }
        }

        void wakeAllWithError()
        {
            qWarning() << "[RPC][WAKE] Pending RPC calls interrupted";
            std::lock_guard<std::mutex> lock(pending_mu);
            connected_.store(false, std::memory_order_release);
            connectionEpoch_.fetch_add(1, std::memory_order_acq_rel);

            for (auto& call : pending)
            {
                std::lock_guard<std::mutex> cg(call->mu);
                call->done = true;
                call->cv.notify_one();
            }

            pending.clear();
        }

        // io_thread only
        bool processBuffer()
        {
            constexpr qsizetype kHeaderSize = 9;

            while (read_buf.size() >= kHeaderSize)
            {
                quint32 reqId = 0;
                quint32 dataLen = 0;
                quint8 status = 0;

                //Decode the header FIRST.
                {
                    QDataStream ds(read_buf);
                    ds.setByteOrder(QDataStream::LittleEndian);

                    ds >> reqId >> status >> dataLen;

                    if (ds.status() != QDataStream::Ok)
                    {
                        failProtocol("Failed to decode response header");
                        return false;
                    }
                }

                //Validate the decoded header.
                if (dataLen > kMaxRPCPayloadBytes)
                {
                    failProtocol(QString("response payload too large: %1 bytes").arg(dataLen));
                    return false;
                }

                if (status > 1)
                {
                    failProtocol(QString("invalid response status: %1").arg(status));
                    return false;
                }

                //Wait for the complete frame.
                const qint64 totalSize = kHeaderSize + static_cast<qint64>(dataLen);

                if (read_buf.size() < totalSize)
                {
                    break;
                }

                // The complete response frame is available.
                // Log it only once, after validating its length.
                qDebug().noquote() << QString("[RPC][RX] reqId=%1 status=%2 payloadLen=%3 frameBytes=%4").arg(reqId).arg(status).arg(dataLen).arg(totalSize);

                //Extract the response.
                QByteArray data = read_buf.mid(kHeaderSize, static_cast<qsizetype>(dataLen));
                read_buf.remove(0, static_cast<qsizetype>(totalSize));

                //Find the matching pending RPC.
                std::shared_ptr<PendingCall> call;

                {
                    std::lock_guard<std::mutex> lock(pending_mu);

                    auto it = pending.find(reqId);

                    if (it != pending.end())
                    {
                        call = it.value();
                        pending.erase(it);
                    }
                }

                //Complete the request.
                if (call)
                {
                    std::lock_guard<std::mutex> lock(call->mu);

                    call->status = status;
                    call->data = std::move(data);
                    call->done = true;

                    call->cv.notify_one();
                }
            }
            return true;
        }

    public:
        LocalSocketChannel()
        {
            io_thread = new QThread;
            io_anchor = new QObject;
            io_anchor->moveToThread(io_thread);
            QObject::connect(io_thread, &QThread::finished, io_anchor, &QObject::deleteLater);
            io_thread->start();
        }

        ~LocalSocketChannel()
        {
            // No new RPC calls may enter during destruction.
            // All external RPC users must have been stopped first.
            Q_ASSERT(QThread::currentThread() != io_thread);
            wakeAllWithError();

            const bool queued = QMetaObject::invokeMethod(io_anchor, [this]()
                {
                    if (sock)
                    {
                        sock->disconnect(io_anchor);
                        sock->close();
                        delete sock;
                        sock = nullptr;
                    }

                    read_buf.clear();
                    io_thread->quit();
                }, Qt::QueuedConnection
            );

            if (!queued)
            {
                qCritical() << "[RPC] Failed to queue channel shutdown";
                io_thread->quit();
            }

            io_thread->wait();
            // io_anchor is destroyed through:
            // QThread::finished -> QObject::deleteLater.
            io_anchor = nullptr;
            delete io_thread;
            io_thread = nullptr;
        }


        // Replace the underlying connection. Must be called on the thread that currently owns `newSock` (the UI thread, same as before).
        bool Reconnect(QLocalSocket* newSock, quint64 generation)
        {
            Q_ASSERT(QThread::currentThread() != io_thread);

            if (!newSock || generation == 0)
            {
                if (newSock)
                {
                    newSock->close();
                    newSock->deleteLater();
                }

                return false;
            }

            // Transfer ownership to the RPC IO thread.
            newSock->setParent(nullptr);
            newSock->moveToThread(io_thread);

            if (newSock->thread() != io_thread)
            {
                qWarning() << "[RPC] Failed to move IPC socket";
                newSock->deleteLater();
                return false;
            }

            bool accepted = false;

            const bool invoked = QMetaObject::invokeMethod(io_anchor, [this, newSock, generation, &accepted]()
                {
                    const quint64 currentGeneration = activeProcessGeneration_.load(std::memory_order_acquire);

                    // Old or duplicate sessions cannot replace the current RPC connection.
                    if (generation <= currentGeneration)
                    {
                        qWarning() << "[RPC] Stale IPC generation rejected" << generation << "current:" << currentGeneration;

                        newSock->abort();
                        newSock->deleteLater();
                        return;
                    }

                    if (newSock->state() != QLocalSocket::ConnectedState)
                    {
                        qWarning() << "[RPC] IPC socket disconnected before adoption";

                        newSock->abort();
                        newSock->deleteLater();
                        return;
                    }

                    // The candidate is valid.
                    // Only now may the previous session be invalidated.
                    if (sock)
                    {
                        // Invalidate pending RPC calls belonging to the previous session.
                        wakeAllWithError();
                        sock->disconnect(io_anchor);
                        sock->close();
                        sock->deleteLater();
                    }

                    sock = newSock;
                    read_buf.clear();

                    const QPointer<QLocalSocket> acceptedSocket(sock);

                    QObject::connect(sock, &QLocalSocket::readyRead, io_anchor, [this, acceptedSocket]()
                        {
                            if (!acceptedSocket || sock != acceptedSocket.data())
                            {
                                return;
                            }

                            onReadyRead();
                        }
                    );

                    QObject::connect(sock, & QLocalSocket::disconnected, io_anchor, [this, acceptedSocket]()
                        {
                            if (!acceptedSocket || sock != acceptedSocket.data())
                            {
                                return;
                            }

                            qWarning() << "[RPC][SOCKET] Disconnected" << acceptedSocket->errorString();

                            wakeAllWithError();
                        }
                    );

                    activeProcessGeneration_.store(generation, std::memory_order_release);

                    connected_.store(true, std::memory_order_release);

                    if (sock->bytesAvailable() > 0)
                    {
                        onReadyRead();
                    }

                    accepted = connected_.load(std::memory_order_acquire);

                    if (accepted)
                    {
                        qInfo() << "[RPC] IPC generation adopted" << generation;
                    }
                }, Qt::BlockingQueuedConnection
            );

            if (!invoked)
            {
                qWarning() << "[RPC] IPC adoption dispatch failed";
                newSock->deleteLater();
                return false;
            }

            return accepted;
        }

        bool IsSessionConnected(quint64 generation) const noexcept
        {
            if (generation == 0)
            {
                return false;
            }

            const quint64 first = activeProcessGeneration_.load(std::memory_order_acquire);

            const bool connected = connected_.load(std::memory_order_acquire);

            const quint64 second = activeProcessGeneration_.load(std::memory_order_acquire);

            return connected && first == generation && second == generation;
        }

        void DisconnectIfGeneration(quint64 generation)
        {
            if (generation == 0)
            {
                return;
            }

            const bool queued = QMetaObject::invokeMethod(io_anchor, [this, generation]()
                {
                    if (!sock || activeProcessGeneration_.load(std::memory_order_acquire) != generation)
                    {
                        return;
                    }

                    QLocalSocket* oldSocket = sock;

                    oldSocket->disconnect(io_anchor);

                    sock = nullptr;
                    read_buf.clear();

                    wakeAllWithError();

                    oldSocket->abort();
                    oldSocket->deleteLater();

                    qInfo() << "[RPC] IPC generation discarded" << generation;
                }, Qt::QueuedConnection
            );

            if (!queued)
            {
                qWarning() << "[RPC] Failed to queue IPC session discard";
            }
        }

        // Returns 0 on success, non-zero on failure.
        int Call(const QString &methodName, const std::string &req, std::vector<uint8_t> &rsp, int timeout_ms = 0) 
        {
            if (!Configs::dataManager->settingsRepo->core_running.load(std::memory_order_acquire))
            {
                return -1919;
            }

            const int ms = (timeout_ms > 0) ? timeout_ms : kIOTimeoutMs;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
            quint32 reqId = static_cast<quint32>(next_id.fetchAndAddOrdered(1));

            // Build request frame

            const QByteArray methodBytes = methodName.toUtf8();

            if (methodBytes.isEmpty() ||methodBytes.size() > kMaxRPCMethodBytes || req.size() > static_cast<std::size_t>(kMaxRPCPayloadBytes))
            {
                qWarning() << "[RPC] Outgoing request exceeds protocol limits";
                return 2;
            }

            const QByteArray reqBytes = QByteArray::fromStdString(req);

            QByteArray frame;
            {
                QDataStream ds(&frame, QIODevice::WriteOnly);
                ds.setByteOrder(QDataStream::LittleEndian);
                ds << reqId;
                ds << static_cast<quint16>(methodBytes.size());
                ds.writeRawData(methodBytes.constData(), methodBytes.size());
                ds << static_cast<quint32>(reqBytes.size());
                ds.writeRawData(reqBytes.constData(), reqBytes.size());
            }

            // Register before sending (never miss the response)
            auto call = std::make_shared<PendingCall>();
            quint64 expectedEpoch = 0;

            {
                std::lock_guard<std::mutex> lock(pending_mu);

                if (!connected_.load(std::memory_order_acquire))
                {
                    return -1919;
                }

                expectedEpoch = connectionEpoch_.load(std::memory_order_acquire);
                pending[reqId] = call;
            }

            // A queued write must never be delivered to a socket belonging to a different connection generation.
            const bool queued = QMetaObject::invokeMethod(io_anchor,
                [this, frame, expectedEpoch]()
                {
                    if (!connected_.load(std::memory_order_acquire))
                    {
                        return;
                    }

                    if (connectionEpoch_.load(std::memory_order_acquire) != expectedEpoch)
                    {
                        return;
                    }

                    if (!sock || sock->state() != QLocalSocket::ConnectedState)
                    {
                        return;
                    }

                    sock->write(frame);
                }, Qt::QueuedConnection
            );

            if (!queued)
            {
                wakeAllWithError();
                return 1;
            }

            // Wait until the original deadline.
            std::unique_lock<std::mutex> lock(call->mu);

            const bool completed = call->cv.wait_until(lock, deadline, [&call]()
                {
                    return call->done;
                }
            );

            lock.unlock();

            if (!completed)
            {
                // Remove our pending slot if still registered.
                // If the reader already owns the shared_ptr, it may finish safely after this caller exits.
                std::lock_guard<std::mutex> pendingLock(pending_mu);
                pending.remove(reqId);
                return 1;
            }

            std::lock_guard<std::mutex> resultLock(call->mu);

            if (call->status != 0)
            {
                MW_show_log("[Core error] " + QString::fromUtf8(call->data));
                return 1;
            }

            rsp.assign(call->data.begin(), call->data.end());
            return 0;
        }
    };

    // Client
    namespace 
    {
        // spb throws std::runtime_error on any malformed/torn input. These calls run on worker QThreads, so an uncaught throw would terminate the whole process. Turn a bad frame into a failed RPC instead.
        template <typename T>
        bool tryDeserialize(const std::vector<uint8_t> &resp, T &out) 
        {
            try 
            {
                out = spb::pb::deserialize<T>(resp);
                return true;
            } 
            catch (const std::exception &e) 
            {
                MW_show_log(QString("[RPC] dropped malformed response: ") + e.what());
                return false;
            } 
            catch (...) 
            {
                MW_show_log("[RPC] dropped malformed response");
                return false;
            }
        }
    }

    Client::~Client() = default;
    Client::Client() 
    {
        this->channel = std::make_unique<LocalSocketChannel>();
    }


    bool Client::Reconnect(QLocalSocket* socket, quint64 generation)
    {
        return channel->Reconnect(socket, generation);
    }

    bool Client::IsSessionConnected(quint64 generation) const noexcept
    {
        return channel->IsSessionConnected(generation);
    }

    void Client::DisconnectIfGeneration(quint64 generation)
    {
        channel->DisconnectIfGeneration(generation);
    }

#define CALL_OK 0

#define NOT_OK      \
    *rpcOK = false; \
    MW_show_log(QString("IPC call failed (code %1)\n").arg(status));

    QString Client::Start(bool *rpcOK, const libcore::LoadConfigReq &request) 
    {
        libcore::ErrorResp reply;
        std::vector<uint8_t> resp;
        auto status = channel->Call("Start", spb::pb::serialize<std::string>(request), resp);

        if (status == CALL_OK && tryDeserialize(resp, reply)) 
        {
            *rpcOK = true;
            return QString::fromStdString(reply.error.value_or(""));
        } 
        else 
        {
            NOT_OK
            return "";
        }
    }

    QString Client::Stop(bool *rpcOK) 
    {
        libcore::EmptyReq request;
        libcore::ErrorResp reply;
        std::vector<uint8_t> resp;
        auto status = channel->Call("Stop", spb::pb::serialize<std::string>(request), resp);

        if (status == CALL_OK && tryDeserialize(resp, reply)) 
        {
            *rpcOK = true;
            return QString::fromStdString(reply.error.value_or(""));
        }
        else 
        {
            NOT_OK
            return "";
        }
    }

    libcore::QueryStatsResp Client::QueryStats() 
    {
        libcore::EmptyReq request;
        libcore::QueryStatsResp reply;
        std::vector<uint8_t> resp;
        auto status = channel->Call("QueryStats", spb::pb::serialize<std::string>(request), resp, 500);

        if (status == CALL_OK && tryDeserialize(resp, reply)) 
        {
            return reply;
        }
        return {};
    }

    libcore::TestResp Client::Test(bool *rpcOK, const libcore::TestReq &request) 
    {
        libcore::TestResp reply;
        std::vector<uint8_t> resp;
        auto status = channel->Call("Test", spb::pb::serialize<std::string>(request), resp);

        if (status == CALL_OK && tryDeserialize(resp, reply)) {
            *rpcOK = true;
            return reply;
        }
        else 
        {
            NOT_OK
            return {};
        }
    }

    void Client::StopTests(bool *rpcOK) 
    {
        const libcore::EmptyReq request;
        std::vector<uint8_t> resp;
        auto status = channel->Call("StopTest", spb::pb::serialize<std::string>(request), resp);

        if (status == CALL_OK) 
        {
            *rpcOK = true;
        } 
        else 
        {
            NOT_OK
        }
    }

    libcore::QueryURLTestResponse Client::QueryURLTest(bool *rpcOK)
    {
        libcore::EmptyReq request;
        libcore::QueryURLTestResponse reply;
        std::vector<uint8_t> resp;
        auto status = channel->Call("QueryURLTest", spb::pb::serialize<std::string>(request), resp);

        if (status == CALL_OK && tryDeserialize(resp, reply)) {
            *rpcOK = true;
            return reply;
        } 
        else 
        {
            NOT_OK
            return {};
        }
    }

    libcore::IPTestResp Client::IPTest(bool *rpcOK, const libcore::IPTestRequest &request) 
    {
        libcore::IPTestResp reply;
        std::vector<uint8_t> resp;
        auto status = channel->Call("IPTest", spb::pb::serialize<std::string>(request), resp);

        if (status == CALL_OK && tryDeserialize(resp, reply)) {
            *rpcOK = true;
            return reply;
        } 
        else
        {
            NOT_OK
            return {};
        }
    }

    libcore::QueryIPTestResponse Client::QueryIPTest(bool *rpcOK) 
    {
        libcore::EmptyReq request;
        libcore::QueryIPTestResponse reply;
        std::vector<uint8_t> resp;
        auto status = channel->Call("QueryIPTest", spb::pb::serialize<std::string>(request), resp);

        if (status == CALL_OK && tryDeserialize(resp, reply)) {
            *rpcOK = true;
            return reply;
        } 
        else 
        {
            NOT_OK
            return {};
        }
    }

    QString Client::SetSystemDNS(bool *rpcOK, const bool clear) const 
    {
        libcore::SetSystemDNSRequest request{clear};
        std::vector<uint8_t> resp;
        auto status = channel->Call("SetSystemDNS", spb::pb::serialize<std::string>(request), resp);

        if (status == CALL_OK) 
        {
            *rpcOK = true;
            return "";
        } 
        else 
        {
            NOT_OK
            return "IPC error";
        }
    }

    libcore::ListConnectionsResp Client::ListConnections() const
    {
        libcore::EmptyReq request;
        libcore::ListConnectionsResp reply;
        std::vector<uint8_t> resp;
        auto status = channel->Call("ListConnections", spb::pb::serialize<std::string>(request), resp);

        if (status == CALL_OK && tryDeserialize(resp, reply)) 
        {
            return reply;
        }
        if (status != CALL_OK)
        {
            MW_show_log("Failed to list connections: IPC error");
        }
        return {};
    }

    QString Client::CheckConfig(bool* rpcOK, const QString& config) const
    {
        libcore::LoadConfigReq request{.core_config = config.toStdString()};
        libcore::ErrorResp reply;
        std::vector<uint8_t> resp;
        auto status = channel->Call("CheckConfig", spb::pb::serialize<std::string>(request), resp);

        if (status == CALL_OK && tryDeserialize(resp, reply))
        {
            *rpcOK = true;
            return QString::fromStdString(reply.error.value_or(""));
        }
        else
        {
            NOT_OK
            return "IPC error";
        }
    }

    bool Client::IsPrivileged(bool* rpcOK) const
    {
        libcore::EmptyReq request;
        libcore::IsPrivilegedResponse reply;
        std::vector<uint8_t> resp;
        auto status = channel->Call("IsPrivileged", spb::pb::serialize<std::string>(request), resp);

        if (status == CALL_OK && tryDeserialize(resp, reply))
        {
            *rpcOK = true;
            return reply.has_privilege.value();
        }
        else
        {
            NOT_OK
            return false;
        }
    }

    libcore::SpeedTestResponse Client::SpeedTest(bool *rpcOK, const libcore::SpeedTestRequest &request)
    {
        libcore::SpeedTestResponse reply;
        std::vector<uint8_t> resp;
        auto status = channel->Call("SpeedTest", spb::pb::serialize<std::string>(request), resp);

        if (status == CALL_OK && tryDeserialize(resp, reply)) {
            *rpcOK = true;
            return reply;
        }
        else 
        {
            NOT_OK
            return {};
        }
    }

    libcore::QuerySpeedTestResponse Client::QueryCurrentSpeedTests(bool *rpcOK)
    {
        const libcore::EmptyReq request;
        libcore::QuerySpeedTestResponse reply;
        std::vector<uint8_t> resp;
        auto status = channel->Call("QuerySpeedTest", spb::pb::serialize<std::string>(request), resp);

        if (status == CALL_OK && tryDeserialize(resp, reply)) {
            *rpcOK = true;
            return reply;
        } 
        else 
        {
            NOT_OK
            return {};
        }
    }

    libcore::QueryCountryTestResponse Client::QueryCountryTestResults(bool* rpcOK)
    {
        const libcore::EmptyReq request;
        libcore::QueryCountryTestResponse reply;
        std::vector<uint8_t> resp;
        auto status = channel->Call("QueryCountryTest", spb::pb::serialize<std::string>(request), resp);

        if (status == CALL_OK && tryDeserialize(resp, reply)) {
            *rpcOK = true;
            return reply;
        } 
        else 
        {
            NOT_OK
            return {};
        }
    }

    libcore::GenWgKeyPairResponse Client::GenWgKeyPair(bool *rpcOK)
    {
        const libcore::EmptyReq request;
        libcore::GenWgKeyPairResponse reply;
        std::vector<uint8_t> resp;
        auto status = channel->Call("GenWgKeyPair", spb::pb::serialize<std::string>(request), resp);

        if (status == CALL_OK && tryDeserialize(resp, reply)) {
            *rpcOK = true;
            return reply;
        } 
        else 
        {
            NOT_OK
            return {};
        }
    }

} // namespace API
