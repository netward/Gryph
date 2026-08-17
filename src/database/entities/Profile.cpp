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
        std::shared_ptr<Configs::outbound> outbound,
        const QString& type)
        :
        type_(type),
        outbound_(
            std::move(
                outbound
            )
        )
    {}

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

    bool Profile::RollbackAssignedIdentity(
        int expectedId,
        int expectedGid)
    {
        QWriteLocker locker(
            &configLock_
        );


        // Never reset a Profile whose identity has meanwhile
        // been changed by somebody else.
        if (id_ != expectedId ||
            gid_ != expectedGid)
        {
            return false;
        }


        // Restore state of a newly-created unpublished Profile.
        id_ = -1;
        gid_ = 0;


        // Increment revision deliberately.
        //
        // Any operation that captured the old revision while
        // this temporary identity was visible must become stale.
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

    // =========================================================
// Create detached outbound from configuration snapshot
// =========================================================

    std::shared_ptr<Configs::outbound>
        Profile::CreateOutboundFromSnapshot(
            const ProfileConfigSnapshot& snapshot) const
    {
        // -----------------------------------------------------
        // A Profile without a valid type cannot be cloned.
        // -----------------------------------------------------

        if (snapshot.type.isEmpty())
        {
            return nullptr;
        }


        // -----------------------------------------------------
        // Create a NEW outbound object of the same type.
        //
        // This must not return the live outbound_ object.
        // -----------------------------------------------------

        auto outbound =
            Configs::ProfilesRepo
            ::NewOutbound(
                snapshot.type
            );


        if (!outbound)
        {
            return nullptr;
        }


        // -----------------------------------------------------
        // Deep-copy configuration through JSON.
        // -----------------------------------------------------

        if (!snapshot
            .outboundJson
            .isEmpty())
        {
            if (!outbound
                ->ParseFromJson(
                    snapshot.outboundJson
                ))
            {
                return nullptr;
            }
        }


        return outbound;
    }


    // =========================================================
    // Safe detached outbound snapshot
    // =========================================================

    std::shared_ptr<Configs::outbound>
        Profile::OutboundClone() const
    {
        // -----------------------------------------------------
        // ConfigSnapshot() holds configLock_ while serializing
        // the current configuration.
        // -----------------------------------------------------

        const auto snapshot =
            ConfigSnapshot();


        // -----------------------------------------------------
        // Create a completely independent outbound object.
        //
        // The returned shared_ptr does NOT point to outbound_.
        // -----------------------------------------------------

        return CreateOutboundFromSnapshot(
            snapshot
        );
    }

    bool Profile::MutateOutboundBase(
        const OutboundMutator& mutator)
    {
        if (!mutator)
        {
            return false;
        }


        // -----------------------------------------------------
        // Obtain one coherent configuration snapshot.
        // -----------------------------------------------------

        const auto snapshot =
            ConfigSnapshot();


        return MutateOutboundBaseAtRevision(
            snapshot.revision,
            mutator
        );
    }


    bool Profile::MutateOutboundBaseAtRevision(
        quint64 expectedRevision,
        const OutboundMutator& mutator)
    {
        if (!mutator)
        {
            return false;
        }


        // =====================================================
        // Step 1:
        // Snapshot current configuration.
        // =====================================================

        const auto snapshot =
            ConfigSnapshot();


        // Somebody changed Profile before this operation
        // even began.
        if (snapshot.revision !=
            expectedRevision)
        {
            return false;
        }


        // =====================================================
        // Step 2:
        // Create a completely detached mutable copy.
        //
        // No Profile lock is held while parsing or editing it.
        // =====================================================

        auto replacement =
            CreateOutboundFromSnapshot(
                snapshot
            );


        if (!replacement)
        {
            return false;
        }


        // =====================================================
        // Step 3:
        // Modify only detached copy.
        // =====================================================

        if (!mutator(
            *replacement))
        {
            return false;
        }


        // =====================================================
        // Step 4:
        // Atomically publish replacement.
        // =====================================================

        return CommitOutboundReplacement(
            std::move(
                replacement
            ),
            expectedRevision
        );
    }


    bool Profile::CommitOutboundReplacement(
        std::shared_ptr<Configs::outbound> replacement,
        quint64 expectedRevision)
    {
        if (!replacement)
        {
            return false;
        }


        QWriteLocker locker(
            &configLock_
        );


        // -----------------------------------------------------
        // Optimistic concurrency control.
        //
        // If another thread published newer configuration,
        // this stale mutation must NOT overwrite it.
        // -----------------------------------------------------

        if (configRevision_ !=
            expectedRevision)
        {
            return false;
        }


        outbound_ =
            std::move(
                replacement
            );


        ++configRevision_;


        return true;
    }

    std::shared_ptr<Profile>
        Profile::CloneForEditing() const
    {
        // =====================================================
        // One coherent configuration snapshot
        // =====================================================

        const auto config =
            ConfigSnapshot();


        // =====================================================
        // Deep-copy outbound
        // =====================================================

        auto clonedOutbound =
            CreateOutboundFromSnapshot(
                config
            );


        if (!clonedOutbound)
        {
            return nullptr;
        }


        // =====================================================
        // Detached Profile
        // =====================================================

        auto clone =
            std::make_shared<
            Profile
            >(
                std::move(
                    clonedOutbound
                ),
                config.type
            );


        // Editing copy keeps original identity only so dialogs
        // know what object they are editing.
        clone->LoadIdentity(
            config.id,
            config.gid
        );


        // =====================================================
        // Test state
        // =====================================================

        clone->SetTestSnapshot(
            TestSnapshot()
        );


        // =====================================================
        // Traffic
        // =====================================================

        const auto traffic =
            TrafficSnapshot();


        clone->SetTraffic(
            traffic.downlink,
            traffic.uplink
        );


        // =====================================================
        // Runtime information
        // =====================================================

        clone->SetRunningCountryInfo(
            RunningCountryInfo()
        );


        return clone;
    }

    // =========================================================
    // Atomic configuration commit
    // =========================================================

    bool Profile::CommitConfigurationFrom(
        const Profile& edited,
        quint64 expectedRevision)
    {
        // =====================================================
        // Snapshot detached editor
        // =====================================================

        const auto editedSnapshot =
            edited.ConfigSnapshot();


        if (editedSnapshot
            .type
            .isEmpty())
        {
            return false;
        }


        // =====================================================
        // Create completely independent outbound
        // =====================================================

        auto replacement =
            CreateOutboundFromSnapshot(
                editedSnapshot
            );


        if (!replacement)
        {
            return false;
        }


        // =====================================================
        // Atomic commit
        // =====================================================

        QWriteLocker locker(
            &configLock_
        );


        if (configRevision_ !=
            expectedRevision)
        {
            return false;
        }


        type_ =
            editedSnapshot.type;


        outbound_ =
            std::move(
                replacement
            );


        ++configRevision_;


        return true;
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
                    const QString resolvedAddress =
                        addresses
                        .first()
                        .toString();


                    self
                        ->MutateOutboundAtRevision<
                        Configs::outbound
                        >(
                            expectedRevision,

                            [
                                resolvedAddress
                            ](
                                Configs::outbound& out
                                ) -> bool
                            {
                                out.SetAddress(
                                    resolvedAddress
                                );


                                return true;
                            }
                                    );
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
        // =====================================================
        // Validate Profile
        // =====================================================

        if (!ent)
        {
            return {};
        }


        // =====================================================
        // Take one immutable configuration snapshot
        //
        // Do NOT expose or access the live outbound_ object.
        // =====================================================

        const auto config =
            ent->ConfigSnapshot();


        if (config.outboundJson.isEmpty())
        {
            return {};
        }


        // =====================================================
        // Work only with a local JSON copy
        // =====================================================

        QJsonObject json =
            config.outboundJson;


        // ExportJsonLink(stripMetadata) previously removed
        // the "tag" field when ignoreMetadata == true.
        if (ignoreMetadata)
        {
            json.remove(
                QStringLiteral("tag")
            );
        }


        // =====================================================
        // Reproduce outbound::ExportJsonLink()
        //
        // json://Gryph#<Base64Url(JSON)>
        // =====================================================

        QUrl url;


        url.setScheme(
            QStringLiteral("json")
        );


        url.setHost(
            QStringLiteral("Gryph")
        );


        url.setFragment(
            QJsonObject2QString(
                json,
                true
            )
            .toUtf8()
            .toBase64(
                QByteArray::Base64UrlEncoding
            )
        );


        return url.toString(
            QUrl::FullyEncoded
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