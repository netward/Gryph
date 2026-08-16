#include <include/database/entities/Profile.h>
#include "include/database/ProfilesRepo.h"
#include "include/database/GroupsRepo.h"

#include "include/global/Configs.hpp"

#include <QReadLocker>
#include <QWriteLocker>
#include <QHostInfo>
#include <QApplication>



namespace Configs
{
    Profile::Profile(
        Configs::outbound* outbound,
        const QString& type)
        :
        type_(type)
    {
        if (outbound)
        {
            outbound_ =
                std::shared_ptr<
                Configs::outbound
                >(
                    outbound
                );
        }
    }

    int Profile::Id() const
    {
        QReadLocker locker(
            &configLock_
        );

        return id_;
    }


    int Profile::GroupId() const
    {
        QReadLocker locker(
            &configLock_
        );

        return gid_;
    }


    QString Profile::Type() const
    {
        QReadLocker locker(
            &configLock_
        );

        return type_;
    }

    QString Profile::Name() const
    {
        QReadLocker locker(
            &configLock_
        );

        if (!outbound_)
        {
            return {};
        }

        return outbound_->name;
    }

    bool Profile::TryAssignIdentity(
        int id,
        int gid)
    {
        if (id < 0 ||
            gid < 0)
        {
            return false;
        }


        QWriteLocker locker(
            &configLock_
        );


        if (id_ >= 0)
        {
            return false;
        }


        id_ = id;
        gid_ = gid;

        ++configRevision_;


        return true;
    }


    void Profile::LoadIdentity(
        int id,
        int gid)
    {
        QWriteLocker locker(
            &configLock_
        );


        id_ = id;
        gid_ = gid;

        ++configRevision_;
    }

    ProfileConfigSnapshot
        Profile::ConfigSnapshot() const
    {
        QReadLocker locker(
            &configLock_
        );


        ProfileConfigSnapshot snapshot;


        snapshot.id =
            id_;

        snapshot.gid =
            gid_;

        snapshot.type =
            type_;

        snapshot.revision =
            configRevision_;


        if (!outbound_)
        {
            return snapshot;
        }


        snapshot.name =
            outbound_->name;


        snapshot.address =
            outbound_->GetAddress();


        snapshot.displayName =
            outbound_->DisplayName();


        snapshot.displayType =
            outbound_->DisplayType();


        snapshot.displayAddress =
            outbound_->DisplayAddress();


        snapshot.invalid =
            outbound_->invalid;


        snapshot.outboundJson =
            outbound_->ExportToJson();


        return snapshot;
    }

    std::shared_ptr<Configs::outbound>
        Profile::OutboundSnapshot() const
    {
        QReadLocker locker(
            &configLock_
        );


        return outbound_;
    }

    std::shared_ptr<Profile>
        Profile::CloneForEditing() const
    {
        // Take one coherent snapshot of the persistent
        // profile configuration.
        const auto config =
            ConfigSnapshot();


        // Create a completely new Profile with a completely
        // new outbound object of the same type.
        auto clone =
            Configs::ProfilesRepo::NewProfile(
                config.type
            );


        if (!clone)
        {
            return nullptr;
        }


        auto cloneOutbound =
            clone->OutboundSnapshot();


        if (!cloneOutbound)
        {
            return nullptr;
        }


        // Deep-copy outbound configuration through JSON.
        //
        // This is important:
        // clone must NOT share the same mutable outbound
        // instance with the original Profile.
        if (!config.outboundJson.isEmpty())
        {
            if (!cloneOutbound->ParseFromJson(
                config.outboundJson))
            {
                return nullptr;
            }
        }


        // Preserve identity in the editing copy.
        //
        // The clone is NOT inserted into ProfilesRepo,
        // these values are only needed by the editor.
        clone->LoadIdentity(
            config.id,
            config.gid
        );


        // Test state is independent from configuration,
        // but copying it makes the detached Profile complete.
        clone->SetTestSnapshot(
            TestSnapshot()
        );


        // Same for traffic counters.
        const auto traffic =
            TrafficSnapshot();


        clone->SetTraffic(
            traffic.downlink,
            traffic.uplink
        );


        // Runtime display information.
        clone->SetRunningCountryInfo(
            RunningCountryInfo()
        );


        return clone;
    }

    // =========================================================
    // Atomic configuration commit
    // =========================================================

    bool Profile::CommitConfigurationFrom(
        const Profile& edited)
    {
        const auto current =
            ConfigSnapshot();


        return CommitConfigurationFrom(
            edited,
            current.revision
        );
    }

    QString
        Profile::RunningCountryInfo() const
    {
        QReadLocker locker(
            &configLock_
        );


        return runningCountryInfo_;
    }

    void Profile::ResolveDomainToIP(
        const std::function<void()>& onFinished)
    {
        const auto snapshot =
            ConfigSnapshot();


        const QString address =
            snapshot.address;


        if (address.isEmpty() ||
            IsIpAddress(address))
        {
            if (onFinished)
            {
                onFinished();
            }

            return;
        }


        const std::weak_ptr<Profile>
            weakSelf =
            weak_from_this();


        QHostInfo::lookupHost(
            address,
            QApplication::instance(),

            [
                weakSelf,
                expectedRevision =
                snapshot.revision,
            onFinished
            ](
                const QHostInfo& host)
            {
                auto self =
                    weakSelf.lock();


                if (!self)
                {
                    return;
                }


                const auto addresses =
                    host.addresses();


                if (!addresses.isEmpty())
                {
                    auto edited =
                        self
                        ->CloneForEditing();


                    if (edited)
                    {
                        auto out =
                            edited
                            ->OutboundSnapshot();


                        if (out)
                        {
                            out->SetAddress(
                                addresses
                                .first()
                                .toString()
                            );


                            self
                                ->CommitConfigurationFrom(
                                    *edited,
                                    expectedRevision
                                );
                        }
                    }
                }


                if (onFinished)
                {
                    onFinished();
                }
            }
        );
    }

    void Profile::SetRunningCountryInfo(
        const QString& value)
    {
        QWriteLocker locker(
            &configLock_
        );


        runningCountryInfo_ =
            value;
    }


    void Profile::ClearRunningCountryInfo()
    {
        QWriteLocker locker(
            &configLock_
        );


        runningCountryInfo_.clear();
    }

    bool Profile::CommitConfigurationFrom(
        const Profile& edited,
        quint64 expectedRevision)
    {
        // -----------------------------------------------------
        // Take immutable snapshot from detached edited Profile.
        //
        // IMPORTANT:
        // do this BEFORE locking this Profile.
        // -----------------------------------------------------

        const auto editedSnapshot =
            edited.ConfigSnapshot();


        if (editedSnapshot.type.isEmpty())
        {
            return false;
        }


        // -----------------------------------------------------
        // Create a completely new outbound instance.
        //
        // Do not reuse edited.outbound directly.
        // Otherwise both Profile objects could share
        // mutable configuration.
        // -----------------------------------------------------

        auto temporary =
            Configs::ProfilesRepo::NewProfile(
                editedSnapshot.type
            );


        if (!temporary)
        {
            return false;
        }


        auto newOutbound =
            temporary->OutboundSnapshot();


        if (!newOutbound)
        {
            return false;
        }


        // -----------------------------------------------------
        // Deep-copy outbound configuration.
        // -----------------------------------------------------

        if (!editedSnapshot
            .outboundJson
            .isEmpty())
        {
            if (!newOutbound
                ->ParseFromJson(
                    editedSnapshot
                    .outboundJson
                ))
            {
                return false;
            }
        }


        // -----------------------------------------------------
        // Atomic replacement.
        //
        // No expensive JSON parsing while holding the lock.
        // -----------------------------------------------------

        QWriteLocker locker(
            &configLock_
        );


        // The live Profile changed after the editor/worker
        // obtained its snapshot.
        //
        // Reject stale result instead of overwriting
        // newer configuration.
        if (configRevision_ !=
            expectedRevision)
        {
            return false;
        }


        type_ =
            editedSnapshot.type;


        outbound_ =
            std::move(
                newOutbound
            );


        ++configRevision_;


        return true;
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
            ->GetGroup(
                GroupId()
            );

        if (!group)
        {
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

    QString ProfileFilter_ent_key(
        const std::shared_ptr<Configs::Profile>& ent,
        bool ignoreMetadata)
    {
        if (!ent)
        {
            return {};
        }

        const auto outbound =
            ent->OutboundSnapshot();

        if (!outbound)
        {
            return {};
        }

        return outbound
            ->ExportJsonLink(
                ignoreMetadata
            );
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
