#include <include/database/entities/Profile.h>

#include "include/database/GroupsRepo.h"
#include "include/global/Configs.hpp"

namespace Configs
{
    Profile::Profile(Configs::outbound *outbound, const QString &type_)
    {
        if (!type_.isEmpty()) this->type = type_;


        if (outbound != nullptr) {
            this->outbound = std::shared_ptr<Configs::outbound>(outbound);
        }
    }

    ProfileTestSnapshot
        Profile::TestSnapshot() const
    {
        return testState_.Snapshot();
    }


    void Profile::SetTestSnapshot(
        const ProfileTestSnapshot& snapshot)
    {
        testState_.SetSnapshot(
            snapshot
        );
    }


    void Profile::SetLatency(
        int latency)
    {
        testState_.SetLatency(
            latency
        );
    }


    void Profile::SetIpTestResult(
        const QString& ip,
        const QString& country)
    {
        testState_.SetIpResult(
            ip,
            country
        );
    }


    void Profile::ClearIpTestResult()
    {
        testState_.ClearIpResult();
    }


    void Profile::MergeSpeedTestResult(
        const QString& dlSpeed,
        const QString& ulSpeed,
        int measuredLatency,
        const QString& country)
    {
        testState_.MergeSpeedResult(
            dlSpeed,
            ulSpeed,
            measuredLatency,
            country
        );
    }


    void Profile::SetSpeedTestResult(
        const QString& dlSpeed,
        const QString& ulSpeed,
        int measuredLatency,
        const QString& country)
    {
        testState_.SetSpeedResult(
            dlSpeed,
            ulSpeed,
            measuredLatency,
            country
        );
    }


    void Profile::MergeCountryTestResult(
        int measuredLatency,
        const QString& country)
    {
        testState_.MergeCountryResult(
            measuredLatency,
            country
        );
    }


    void Profile::SetSpeedTestError()
    {
        testState_.SetSpeedError();
    }

    void Profile::ClearTestResults()
    {
        testState_.Clear();
    }

    QString Profile::DisplayTestResult() const
    {
        const auto test =
            TestSnapshot();


        auto group =
            dataManager
            ->groupsRepo
            ->GetGroup(gid);

        if (!group) {
            return {};
        }

        const auto groupSnapshot =
            group->Snapshot();


        QString result;

        if (!test.testCountry.isEmpty()) {

            result +=
                UNICODE_LRO
                +
                CountryCodeToFlag(
                    test.testCountry
                )
                +
                " ";
        }

        if (test.latency < 0) {

            return "Unavailable";
        }

        if (test.latency > 0) {

            result +=
                QString("%1 ms")
                .arg(
                    test.latency
                );
        }

        const bool showSpeed =
            groupSnapshot.test_items_to_show ==
            testShowItems::all
            ||
            groupSnapshot.test_items_to_show ==
            testShowItems::speedOnly;

        const bool showIP =
            groupSnapshot.test_items_to_show ==
            testShowItems::all
            ||
            groupSnapshot.test_items_to_show ==
            testShowItems::ipOnly;

        if (!test.dlSpeed.isEmpty() &&
            test.dlSpeed != "N/A" &&
            showSpeed)
        {
            result +=
                " ↓"
                +
                test.dlSpeed;
        }

        if (!test.ulSpeed.isEmpty() &&
            test.ulSpeed != "N/A" &&
            showSpeed)
        {
            result +=
                " ↑"
                +
                test.ulSpeed;
        }

        if (!test.ipOut.isEmpty() &&
            showIP)
        {
            result +=
                " 🌐"
                +
                test.ipOut;
        }

        return result;
    }

    QColor Profile::DisplayLatencyColor() const
    {
        const auto test =
            TestSnapshot();

        if (test.latency < 0) {

            return Qt::darkGray;
        }

        if (test.latency <= 0) {

            return {};
        }

        if (test.latency <= 100) {

            return Qt::darkGreen;
        }

        if (test.latency <= 300) {

            return Qt::darkYellow;
        }

        return Qt::red;
    }

    void Profile::AddTraffic(
        qint64 downlinkDelta,
        qint64 uplinkDelta)
    {
        traffic_.Add(
            downlinkDelta,
            uplinkDelta
        );
    }


    void Profile::SetTraffic(
        qint64 downlink,
        qint64 uplink)
    {
        traffic_.Set(
            downlink,
            uplink
        );
    }


    ProfileTrafficSnapshot
        Profile::TrafficSnapshot() const
    {
        return traffic_.Snapshot();
    }

    QString Profile::DisplayTraffic() const
    {
        const auto traffic =
            TrafficSnapshot();


        if (traffic.total() == 0) {
            return "";
        }


        return UNICODE_LRO
            + QString("%1↑ %2↓")
            .arg(
                ReadableSize(traffic.uplink),
                ReadableSize(traffic.downlink)
            );
    }

    void Profile::ResetTraffic()
    {
        traffic_.Reset();
    }

    QString ProfileFilter_ent_key(const std::shared_ptr<Configs::Profile> &ent, bool ignoreMetadata) {
        auto key = ent->outbound->ExportJsonLink(ignoreMetadata);
        return key;
    }

    void ProfileFilter::Uniq(const QList<std::shared_ptr<Profile>> &in,
                             QList<std::shared_ptr<Profile>> &out,
                             bool keep_last, bool ignoreMetadata) {
        QMap<QString, std::shared_ptr<Profile>> hashMap;

        for (const auto &ent: in) {
            QString key = ProfileFilter_ent_key(ent, ignoreMetadata);
            if (hashMap.contains(key)) {
                if (keep_last) {
                    out.removeAll(hashMap[key]);
                    hashMap[key] = ent;
                    out += ent;
                }
            } else {
                hashMap[key] = ent;
                out += ent;
            }
        }
    }

    void ProfileFilter::Common(const QList<std::shared_ptr<Profile>> &src,
                               const QList<std::shared_ptr<Profile>> &dst,
                               QList<std::shared_ptr<Profile>> &outSrc,
                               QList<std::shared_ptr<Profile>> &outDst,
                               bool ignoreMetadata) {
        QMap<QString, std::shared_ptr<Profile>> hashMap;

        for (const auto &ent: src) {
            QString key = ProfileFilter_ent_key(ent, ignoreMetadata);
            hashMap[key] = ent;
        }
        for (const auto &ent: dst) {
            QString key = ProfileFilter_ent_key(ent, ignoreMetadata);
            if (hashMap.contains(key)) {
                outDst += ent;
                outSrc += hashMap[key];
            }
        }
    }

    void ProfileFilter::OnlyInSrc(const QList<std::shared_ptr<Profile>> &src,
                                  const QList<std::shared_ptr<Profile>> &dst,
                                  QList<std::shared_ptr<Profile>> &out,
                                  bool ignoreMetadata) {
        QMap<QString, bool> hashMap;

        for (const auto &ent: dst) {
            QString key = ProfileFilter_ent_key(ent, ignoreMetadata);
            hashMap[key] = true;
        }
        for (const auto &ent: src) {
            QString key = ProfileFilter_ent_key(ent, ignoreMetadata);
            if (!hashMap.contains(key)) out += ent;
        }
    }

    void ProfileFilter::OnlyInSrc_ByPointer(const QList<std::shared_ptr<Profile>> &src,
                                            const QList<std::shared_ptr<Profile>> &dst,
                                            QList<std::shared_ptr<Profile>> &out) {
        for (const auto &ent: src) {
            if (!dst.contains(ent)) out += ent;
        }
    }
}
