#pragma once

#include <include/configs/outbounds/tailscale.h>
#include <include/configs/outbounds/wireguard.h>

#include "include/configs/common/Outbound.h"
#include "include/configs/outbounds/anyTLS.h"
#include "include/configs/outbounds/direct.h"
#include "include/configs/outbounds/chain.h"
#include "include/configs/outbounds/custom.h"
#include "include/configs/outbounds/extracore.h"
#include "include/configs/outbounds/socks.h"
#include "include/configs/outbounds/http.h"
#include "include/configs/outbounds/hysteria.h"
#include "include/configs/outbounds/shadowsocks.h"
#include "include/configs/outbounds/ssh.h"
#include "include/configs/outbounds/trojan.h"
#include "include/configs/outbounds/tuic.h"
#include "include/configs/outbounds/juicity.h"
#include "include/configs/outbounds/trusttunnel.h"
#include "include/configs/outbounds/naive.h"
#include "include/configs/outbounds/shadowtls.h"
#include "include/configs/outbounds/vless.h"
#include "include/configs/outbounds/vmess.h"
#include "include/configs/outbounds/xrayVless.h"

#include "include/global/CountryHelper.hpp"

#include <QReadWriteLock>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>

namespace Configs {
    struct ProfileTrafficSnapshot
    {
        qint64 downlink = 0;
        qint64 uplink = 0;

        [[nodiscard]]
        qint64 total() const noexcept
        {
            return downlink + uplink;
        }
    };

    class ProfileTrafficCounters
    {
    public:
        ProfileTrafficCounters() = default;


        ProfileTrafficCounters(
            const ProfileTrafficCounters& other)
        {
            const auto snapshot =
                other.Snapshot();

            downlink_ = snapshot.downlink;
            uplink_ = snapshot.uplink;
        }


        ProfileTrafficCounters&
            operator=(
                const ProfileTrafficCounters& other)
        {
            if (this == &other) {
                return *this;
            }

            const auto snapshot =
                other.Snapshot();

            QMutexLocker locker(&mutex_);

            downlink_ = snapshot.downlink;
            uplink_ = snapshot.uplink;

            return *this;
        }


        ProfileTrafficCounters(
            ProfileTrafficCounters&& other) noexcept
        {
            const auto snapshot =
                other.Snapshot();

            downlink_ = snapshot.downlink;
            uplink_ = snapshot.uplink;
        }


        ProfileTrafficCounters&
            operator=(
                ProfileTrafficCounters&& other) noexcept
        {
            if (this == &other) {
                return *this;
            }

            const auto snapshot =
                other.Snapshot();

            QMutexLocker locker(&mutex_);

            downlink_ = snapshot.downlink;
            uplink_ = snapshot.uplink;

            return *this;
        }


        void Add(
            qint64 downlinkDelta,
            qint64 uplinkDelta)
        {
            QMutexLocker locker(&mutex_);

            downlink_ += downlinkDelta;
            uplink_ += uplinkDelta;
        }


        void Set(
            qint64 downlink,
            qint64 uplink)
        {
            QMutexLocker locker(&mutex_);

            downlink_ = downlink;
            uplink_ = uplink;
        }


        void Reset()
        {
            QMutexLocker locker(&mutex_);

            downlink_ = 0;
            uplink_ = 0;
        }


        [[nodiscard]]
        ProfileTrafficSnapshot Snapshot() const
        {
            QMutexLocker locker(&mutex_);

            return {
                downlink_,
                uplink_
            };
        }


    private:
        mutable QMutex mutex_;

        qint64 downlink_ = 0;
        qint64 uplink_ = 0;
    };

    struct ProfileTestSnapshot
    {
        int latency = 0;

        QString dlSpeed;
        QString ulSpeed;

        QString testCountry;
        QString ipOut;
    };

    struct ProfileConfigSnapshot
    {
        int id = -1;
        int gid = 0;

        QString type;

        QString name;
        QString address;

        QString displayName;
        QString displayType;
        QString displayAddress;

        bool invalid = false;

        QJsonObject outboundJson;

        quint64 revision = 0;
    };

    class ProfileTestState
    {
    public:
        ProfileTestState() = default;


        ProfileTestState(
            const ProfileTestState& other)
        {
            const auto snapshot =
                other.Snapshot();

            latency_ =
                snapshot.latency;

            dlSpeed_ =
                snapshot.dlSpeed;

            ulSpeed_ =
                snapshot.ulSpeed;

            testCountry_ =
                snapshot.testCountry;

            ipOut_ =
                snapshot.ipOut;
        }


        ProfileTestState&
            operator=(
                const ProfileTestState& other)
        {
            if (this == &other) {
                return *this;
            }


            const auto snapshot =
                other.Snapshot();


            QMutexLocker locker(
                &mutex_
            );


            latency_ =
                snapshot.latency;

            dlSpeed_ =
                snapshot.dlSpeed;

            ulSpeed_ =
                snapshot.ulSpeed;

            testCountry_ =
                snapshot.testCountry;

            ipOut_ =
                snapshot.ipOut;


            return *this;
        }


        ProfileTestState(
            ProfileTestState&& other) noexcept
        {
            const auto snapshot =
                other.Snapshot();

            latency_ =
                snapshot.latency;

            dlSpeed_ =
                snapshot.dlSpeed;

            ulSpeed_ =
                snapshot.ulSpeed;

            testCountry_ =
                snapshot.testCountry;

            ipOut_ =
                snapshot.ipOut;
        }


        ProfileTestState&
            operator=(
                ProfileTestState&& other) noexcept
        {
            if (this == &other) {
                return *this;
            }


            const auto snapshot =
                other.Snapshot();


            QMutexLocker locker(
                &mutex_
            );


            latency_ =
                snapshot.latency;

            dlSpeed_ =
                snapshot.dlSpeed;

            ulSpeed_ =
                snapshot.ulSpeed;

            testCountry_ =
                snapshot.testCountry;

            ipOut_ =
                snapshot.ipOut;


            return *this;
        }


        [[nodiscard]]
        ProfileTestSnapshot Snapshot() const
        {
            QMutexLocker locker(
                &mutex_
            );


            return {
                latency_,
                dlSpeed_,
                ulSpeed_,
                testCountry_,
                ipOut_
            };
        }


        void SetSnapshot(
            const ProfileTestSnapshot& snapshot)
        {
            QMutexLocker locker(
                &mutex_
            );


            latency_ =
                snapshot.latency;

            dlSpeed_ =
                snapshot.dlSpeed;

            ulSpeed_ =
                snapshot.ulSpeed;

            testCountry_ =
                snapshot.testCountry;

            ipOut_ =
                snapshot.ipOut;
        }


        void Clear()
        {
            QMutexLocker locker(
                &mutex_
            );


            latency_ = 0;

            dlSpeed_.clear();
            ulSpeed_.clear();

            testCountry_.clear();
            ipOut_.clear();
        }


        void SetLatency(
            int latency)
        {
            QMutexLocker locker(
                &mutex_
            );

            latency_ =
                latency;
        }


        void SetIpResult(
            const QString& ip,
            const QString& country)
        {
            QMutexLocker locker(
                &mutex_
            );


            ipOut_ =
                ip;

            testCountry_ =
                country;
        }


        void ClearIpResult()
        {
            QMutexLocker locker(
                &mutex_
            );


            ipOut_.clear();

            testCountry_.clear();
        }


        // Used by intermediate speed-test polling.
        //
        // Empty values do not overwrite
        // already collected values.
        void MergeSpeedResult(
            const QString& dlSpeed,
            const QString& ulSpeed,
            int measuredLatency,
            const QString& country)
        {
            QMutexLocker locker(
                &mutex_
            );


            if (!dlSpeed.isEmpty()) {

                dlSpeed_ =
                    dlSpeed;
            }


            if (!ulSpeed.isEmpty()) {

                ulSpeed_ =
                    ulSpeed;
            }


            if (latency_ <= 0 &&
                measuredLatency > 0)
            {
                latency_ =
                    measuredLatency;
            }


            if (!country.isEmpty()) {

                testCountry_ =
                    country;
            }
        }


        // Used for the final successful
        // speed-test result.
        void SetSpeedResult(
            const QString& dlSpeed,
            const QString& ulSpeed,
            int measuredLatency,
            const QString& country)
        {
            QMutexLocker locker(
                &mutex_
            );


            dlSpeed_ =
                dlSpeed;

            ulSpeed_ =
                ulSpeed;


            if (latency_ <= 0 &&
                measuredLatency > 0)
            {
                latency_ =
                    measuredLatency;
            }


            if (!country.isEmpty()) {

                testCountry_ =
                    country;
            }
        }


        void MergeCountryResult(
            int measuredLatency,
            const QString& country)
        {
            QMutexLocker locker(
                &mutex_
            );


            if (latency_ <= 0 &&
                measuredLatency > 0)
            {
                latency_ =
                    measuredLatency;
            }


            if (!country.isEmpty()) {

                testCountry_ =
                    country;
            }
        }

        void SetSpeedError()
        {
            QMutexLocker locker(
                &mutex_
            );


            dlSpeed_ =
                "N/A";

            ulSpeed_ =
                "N/A";

            latency_ =
                -1;

            testCountry_.clear();
        }


    private:
        mutable QMutex mutex_;


        int latency_ = 0;

        QString dlSpeed_;
        QString ulSpeed_;

        QString testCountry_;
        QString ipOut_;
    };

    class Profile :
        public std::enable_shared_from_this<Profile>
    {
    public:

        Profile() = default;

        Profile(
            Configs::outbound* outbound,
            const QString& type_
        );

        // =====================================================
        // Identity
        // =====================================================

        [[nodiscard]]
        int Id() const;

        [[nodiscard]]
        int GroupId() const;

        [[nodiscard]]
        QString Type() const;

        [[nodiscard]]
        QString Name() const;

        bool TryAssignIdentity(
            int id,
            int gid
        );

        void LoadIdentity(
            int id,
            int gid
        );


        // =====================================================
        // Configuration
        // =====================================================

        [[nodiscard]]
        ProfileConfigSnapshot
            ConfigSnapshot() const;

        [[nodiscard]]
        std::shared_ptr<Configs::outbound>
            OutboundSnapshot() const;

        [[nodiscard]]
        std::shared_ptr<Profile>
            CloneForEditing() const;

        bool CommitConfigurationFrom(
            const Profile& edited
        );

        bool CommitConfigurationFrom(
            const Profile& edited,
            quint64 expectedRevision
        );


        // =====================================================
        // Runtime information
        // =====================================================

        [[nodiscard]]
        QString RunningCountryInfo() const;

        void SetRunningCountryInfo(
            const QString& value
        );

        void ClearRunningCountryInfo();


        // =====================================================
        // DNS resolution
        // =====================================================

        void ResolveDomainToIP(
            const std::function<void()>& onFinished
        );


        // =====================================================
        // Test/runtime state
        // =====================================================

        [[nodiscard]]
        ProfileTestSnapshot TestSnapshot() const;

        void SetTestSnapshot(
            const ProfileTestSnapshot& snapshot
        );

        void SetLatency(
            int latency
        );

        void SetIpTestResult(
            const QString& ip,
            const QString& country
        );

        void ClearIpTestResult();

        void MergeSpeedTestResult(
            const QString& dlSpeed,
            const QString& ulSpeed,
            int measuredLatency,
            const QString& country
        );

        void SetSpeedTestResult(
            const QString& dlSpeed,
            const QString& ulSpeed,
            int measuredLatency,
            const QString& country
        );

        void MergeCountryTestResult(
            int measuredLatency,
            const QString& country
        );

        void SetSpeedTestError();

        void ClearTestResults();

        [[nodiscard]]
        QString DisplayTestResult() const;

        [[nodiscard]]
        QColor DisplayLatencyColor() const;


        // =====================================================
        // Traffic
        // =====================================================

        void AddTraffic(
            qint64 downlinkDelta,
            qint64 uplinkDelta
        );

        void SetTraffic(
            qint64 downlink,
            qint64 uplink
        );

        [[nodiscard]]
        ProfileTrafficSnapshot TrafficSnapshot() const;

        [[nodiscard]]
        QString DisplayTraffic() const;

        void ResetTraffic();


        // =====================================================
        // Typed outbound access
        //
        // Use these mainly while constructing a NEW unpublished
        // Profile or its detached editing copy.
        // =====================================================

        [[nodiscard]]
        std::shared_ptr<Configs::socks>
            Socks() const
        {
            return OutboundAs<Configs::socks>();
        }

        [[nodiscard]]
        std::shared_ptr<Configs::http>
            Http() const
        {
            return OutboundAs<Configs::http>();
        }

        [[nodiscard]]
        std::shared_ptr<Configs::shadowsocks>
            ShadowSocks() const
        {
            return OutboundAs<Configs::shadowsocks>();
        }

        [[nodiscard]]
        std::shared_ptr<Configs::vmess>
            VMess() const
        {
            return OutboundAs<Configs::vmess>();
        }

        [[nodiscard]]
        std::shared_ptr<Configs::Trojan>
            Trojan() const
        {
            return OutboundAs<Configs::Trojan>();
        }

        [[nodiscard]]
        std::shared_ptr<Configs::vless>
            VLESS() const
        {
            return OutboundAs<Configs::vless>();
        }

        [[nodiscard]]
        std::shared_ptr<Configs::xrayVless>
            XrayVLESS() const
        {
            return OutboundAs<Configs::xrayVless>();
        }

        [[nodiscard]]
        std::shared_ptr<Configs::hysteria>
            Hysteria() const
        {
            return OutboundAs<Configs::hysteria>();
        }

        [[nodiscard]]
        std::shared_ptr<Configs::anyTLS>
            AnyTLS() const
        {
            return OutboundAs<Configs::anyTLS>();
        }

        [[nodiscard]]
        std::shared_ptr<Configs::ssh>
            SSH() const
        {
            return OutboundAs<Configs::ssh>();
        }

        [[nodiscard]]
        std::shared_ptr<Configs::tailscale>
            Tailscale() const
        {
            return OutboundAs<Configs::tailscale>();
        }

        [[nodiscard]]
        std::shared_ptr<Configs::tuic>
            TUIC() const
        {
            return OutboundAs<Configs::tuic>();
        }

        [[nodiscard]]
        std::shared_ptr<Configs::juicity>
            Juicity() const
        {
            return OutboundAs<Configs::juicity>();
        }

        [[nodiscard]]
        std::shared_ptr<Configs::trusttunnel>
            TrustTunnel() const
        {
            return OutboundAs<Configs::trusttunnel>();
        }

        [[nodiscard]]
        std::shared_ptr<Configs::naive>
            Naive() const
        {
            return OutboundAs<Configs::naive>();
        }

        [[nodiscard]]
        std::shared_ptr<Configs::shadowtls>
            ShadowTLS() const
        {
            return OutboundAs<Configs::shadowtls>();
        }

        [[nodiscard]]
        std::shared_ptr<Configs::wireguard>
            Wireguard() const
        {
            return OutboundAs<Configs::wireguard>();
        }

        [[nodiscard]]
        std::shared_ptr<Configs::Custom>
            Custom() const
        {
            return OutboundAs<Configs::Custom>();
        }

        [[nodiscard]]
        std::shared_ptr<Configs::chain>
            Chain() const
        {
            return OutboundAs<Configs::chain>();
        }

        [[nodiscard]]
        std::shared_ptr<Configs::direct>
            Direct() const
        {
            return OutboundAs<Configs::direct>();
        }

        [[nodiscard]]
        std::shared_ptr<Configs::extracore>
            ExtraCore() const
        {
            return OutboundAs<Configs::extracore>();
        }

        template <typename T>
        [[nodiscard]]
        std::shared_ptr<T> OutboundAs() const
        {
            return std::dynamic_pointer_cast<T>(
                OutboundSnapshot()
            );
        }

    private:

        // =====================================================
        // Persistent/configuration state
        // =====================================================

        mutable QReadWriteLock configLock_;

        QString type_;

        int id_ = -1;
        int gid_ = 0;


        std::shared_ptr<
            Configs::outbound
        > outbound_;


        quint64 configRevision_ = 0;


        // =====================================================
        // Runtime state
        // =====================================================

        QString runningCountryInfo_;


        // These classes already have their own locks.
        ProfileTrafficCounters traffic_;
        ProfileTestState testState_;

       
    };

    class ProfileFilter {
    public:
        static void Uniq(
            const QList<std::shared_ptr<Profile>>& in,
            QList<std::shared_ptr<Profile>>& out,
            bool keep_last = false, // def keep first
            bool ignoreMetadata = true
        );

        static void Common(
            const QList<std::shared_ptr<Profile>>& src,
            const QList<std::shared_ptr<Profile>>& dst,
            QList<std::shared_ptr<Profile>>& outSrc,
            QList<std::shared_ptr<Profile>>& outDst,
            bool ignoreMetadata = true
        );

        static void OnlyInSrc(
            const QList<std::shared_ptr<Profile>>& src,
            const QList<std::shared_ptr<Profile>>& dst,
            QList<std::shared_ptr<Profile>>& out,
            bool ignoreMetadata = true
        );

        static void OnlyInSrc_ByPointer(
            const QList<std::shared_ptr<Profile>>& src,
            const QList<std::shared_ptr<Profile>>& dst,
            QList<std::shared_ptr<Profile>>& out);
    };
} // namespace Configs