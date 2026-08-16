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

    class Profile {
    public:
        QString type;
        QString name;

        int id = -1;
        int gid = 0;
        
        std::shared_ptr<Configs::outbound> outbound;

        QString runningCountryInfo; // volatile, not saved to db


        Profile() = default;

        Profile(
            Configs::outbound* outbound,
            const QString& type_
        );

        // ---------------------------------------------
        // Test/runtime state
        // ---------------------------------------------

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


        // ---------------------------------------------
        // Traffic
        // ---------------------------------------------

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

        [[nodiscard]] Configs::socks* Socks() const {
            return dynamic_cast<Configs::socks*>(outbound.get());
        };

        [[nodiscard]] Configs::http* Http() const {
            return dynamic_cast<Configs::http*>(outbound.get());
        };

        [[nodiscard]] Configs::shadowsocks* ShadowSocks() const {
            return dynamic_cast<Configs::shadowsocks*>(outbound.get());
        };

        [[nodiscard]] Configs::vmess* VMess() const {
            return dynamic_cast<Configs::vmess*>(outbound.get());
        };

        [[nodiscard]] Configs::Trojan* Trojan() const {
            return dynamic_cast<Configs::Trojan*>(outbound.get());
        };

        [[nodiscard]] Configs::vless* VLESS() const {
            return dynamic_cast<Configs::vless*>(outbound.get());
        };

        [[nodiscard]] Configs::xrayVless* XrayVLESS() const {
            return dynamic_cast<Configs::xrayVless*>(outbound.get());
        }

        [[nodiscard]] Configs::anyTLS* AnyTLS() const {
            return dynamic_cast<Configs::anyTLS*>(outbound.get());
        };

        [[nodiscard]] Configs::hysteria* Hysteria() const {
            return dynamic_cast<Configs::hysteria*>(outbound.get());
        };

        [[nodiscard]] Configs::ssh* SSH() const {
            return dynamic_cast<Configs::ssh*>(outbound.get());
        };

        [[nodiscard]] Configs::tailscale* Tailscale() const {
            return dynamic_cast<Configs::tailscale*>(outbound.get());
        };

        [[nodiscard]] Configs::tuic* TUIC() const {
            return dynamic_cast<Configs::tuic*>(outbound.get());
        };

        [[nodiscard]] Configs::juicity* Juicity() const {
            return dynamic_cast<Configs::juicity*>(outbound.get());
        };

        [[nodiscard]] Configs::trusttunnel* TrustTunnel() const {
            return dynamic_cast<Configs::trusttunnel*>(outbound.get());
        };

        [[nodiscard]] Configs::naive* Naive() const {
            return dynamic_cast<Configs::naive*>(outbound.get());
        };

        [[nodiscard]] Configs::shadowtls* ShadowTLS() const {
            return dynamic_cast<Configs::shadowtls*>(outbound.get());
        };

        [[nodiscard]] Configs::wireguard* Wireguard() const {
            return dynamic_cast<Configs::wireguard*>(outbound.get());
        };

        [[nodiscard]] Configs::Custom* Custom() const {
            return dynamic_cast<Configs::Custom*>(outbound.get());
        };

        [[nodiscard]] Configs::chain* Chain() const {
            return dynamic_cast<Configs::chain*>(outbound.get());
        };

        [[nodiscard]] Configs::direct* Direct() const {
            return dynamic_cast<Configs::direct*>(outbound.get());
        };

        [[nodiscard]] Configs::extracore* ExtraCore() const {
            return dynamic_cast<Configs::extracore*>(outbound.get());
        };
    private:
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