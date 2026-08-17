#include "include/database/ProfilesRepo.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <map>

#include "include/database/GroupsRepo.h"
#include "include/ui/mainwindowapi.h"


namespace Configs {
    ProfilesRepo::ProfilesRepo(Database& database) : db(database) {
        createTables();
    }

    void ProfilesRepo::createTables() const {
        // Note: This table has a foreign key to groups(id).
        // Ensure GroupsRepo::createTables() is called before this method
        // to avoid foreign key constraint errors.
        // Create profiles table
        db.exec(R"(
            CREATE TABLE IF NOT EXISTS profiles (
                id INTEGER PRIMARY KEY,
                type TEXT NOT NULL,
                name TEXT,
                gid INTEGER NOT NULL DEFAULT 0,
                latency INTEGER NOT NULL DEFAULT 0,
                dl_speed TEXT,
                ul_speed TEXT,
                test_country TEXT,
                ip_out TEXT,
                outbound_json TEXT NOT NULL,
                traffic_dl INTEGER NOT NULL DEFAULT 0,
                traffic_up INTEGER NOT NULL DEFAULT 0,
                created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
                updated_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
                FOREIGN KEY(gid) REFERENCES groups(id) ON DELETE CASCADE
            )
        )");

        db.exec("CREATE INDEX IF NOT EXISTS idx_profiles_name ON profiles(name)");
    }

    QJsonObject ProfilesRepo::profileToJson(
        const Profile* profile) const
    {
        if (!profile)
        {
            return {};
        }

        const auto config =
            profile->ConfigSnapshot();

        const auto test =
            profile->TestSnapshot();

        const auto traffic =
            profile->TrafficSnapshot();


        QJsonObject json;

        json["type"] =
            config.type;

        json["name"] =
            config.name;

        json["id"] =
            config.id;

        json["gid"] =
            config.gid;

        json["latency"] =
            test.latency;

        json["dl_speed"] =
            test.dlSpeed;

        json["ul_speed"] =
            test.ulSpeed;

        json["test_country"] =
            test.testCountry;

        json["ip_out"] =
            test.ipOut;

        if (!config.outboundJson.isEmpty())
        {
            json["outbound"] =
                config.outboundJson;
        }

        json["traffic_dl"] =
            traffic.downlink;

        json["traffic_up"] =
            traffic.uplink;

        return json;
    }

    std::shared_ptr<Profile>
        ProfilesRepo::profileFromJson(
            const QJsonObject& json) const
    {
        const QString type =
            json["type"].toString();


        auto profile =
            ProfilesRepo::NewProfile(
                type
            );

        if (!profile)
        {
            return nullptr;
        }


        // =====================================================
        // Identity
        // =====================================================
        profile->LoadIdentity(
            json["id"].toInt(),
            json["gid"].toInt()
        );


        // =====================================================
        // Outbound
        // =====================================================
        if (json.contains("outbound") &&
            json["outbound"].isObject())
        {
            const QJsonObject outboundJson =
                json["outbound"]
                .toObject();


            const bool parsed =
                profile
                ->MutateOutbound<
                Configs::outbound
                >(
                    [
                        outboundJson
                    ](
                        Configs::outbound& out
                        ) -> bool
                    {
                        return out
                            .ParseFromJson(
                                outboundJson
                            );
                    }
                            );


            if (!parsed)
            {
                return nullptr;
            }
        }
        else
        {
            const QString name =
                json["name"]
                .toString();


            const bool updated =
                profile
                ->MutateOutbound<
                Configs::outbound
                >(
                    [
                        name
                    ](
                        Configs::outbound& out
                        ) -> bool
                    {
                        out.name =
                            name;


                        return true;
                    }
                            );

            if (!updated)
            {
                return nullptr;
            }
        }

        // =====================================================
        // Test state
        // =====================================================
        ProfileTestSnapshot test;

        test.latency =
            json["latency"].toInt();

        test.dlSpeed =
            json["dl_speed"].toString();

        test.ulSpeed =
            json["ul_speed"].toString();

        test.testCountry =
            json["test_country"].toString();

        test.ipOut =
            json["ip_out"].toString();


        profile->SetTestSnapshot(
            test
        );


        // =====================================================
        // Traffic
        // =====================================================

        const qint64 trafficDownlink =
            json["traffic_dl"]
            .toVariant()
            .toLongLong();

        const qint64 trafficUplink =
            json["traffic_up"]
            .toVariant()
            .toLongLong();


        profile->SetTraffic(
            trafficDownlink,
            trafficUplink
        );


        return profile;
    }

    bool ProfilesRepo::saveToDatabase(
        const Profile* profile,
        int id) const
    {
        // =========================================================
        // Validation
        // =========================================================

        if (!profile)
        {
            MW_show_log(
                "ProfilesRepo::saveToDatabase: "
                "null Profile"
            );

            return false;
        }


        if (id < 0)
        {
            MW_show_log(
                "ProfilesRepo::saveToDatabase: "
                "invalid Profile id"
            );

            return false;
        }


        // =========================================================
        // Freeze Profile state BEFORE database I/O
        // =========================================================

        const auto config =
            profile->ConfigSnapshot();


        const auto test =
            profile->TestSnapshot();


        const auto traffic =
            profile->TrafficSnapshot();


        const QString outboundJson =
            QString::fromUtf8(
                QJsonDocument(
                    config.outboundJson
                )
                .toJson(
                    QJsonDocument::Compact
                )
            );


        const long long trafficDl =
            static_cast<long long>(
                traffic.downlink
                );


        const long long trafficUp =
            static_cast<long long>(
                traffic.uplink
                );


        // =========================================================
        // Check whether the row already exists
        // =========================================================

        auto checkQuery =
            db.query(
                "SELECT id "
                "FROM profiles "
                "WHERE id = ?",
                id
            );


        if (!checkQuery)
        {
            MW_show_log(
                "ProfilesRepo::saveToDatabase: "
                "failed to query Profile existence"
            );

            return false;
        }


        bool exists =
            false;


        try
        {
            exists =
                checkQuery->executeStep();
        }
        catch (const std::exception& e)
        {
            MW_show_log(
                "ProfilesRepo::saveToDatabase: "
                "failed while checking Profile existence: "
                +
                QString::fromUtf8(
                    e.what()
                )
            );

            return false;
        }


        // Release SELECT statement before UPDATE/INSERT.
        //
        // This is particularly useful with a single SQLite
        // connection because the read statement should not stay
        // alive longer than necessary.
        checkQuery =
        {};


        // =========================================================
        // UPDATE existing row
        // =========================================================

        if (exists)
        {
            const bool updated =
                db.exec(
                    R"(
                    UPDATE profiles
                    SET type = ?,
                        name = ?,
                        gid = ?,
                        latency = ?,
                        dl_speed = ?,
                        ul_speed = ?,
                        test_country = ?,
                        ip_out = ?,
                        outbound_json = ?,
                        traffic_dl = ?,
                        traffic_up = ?,
                        updated_at = strftime('%s', 'now')
                    WHERE id = ?
                    )",

                    config.type.toStdString(),
                    config.name.toStdString(),
                    config.gid,

                    test.latency,
                    test.dlSpeed.toStdString(),
                    test.ulSpeed.toStdString(),
                    test.testCountry.toStdString(),
                    test.ipOut.toStdString(),

                    outboundJson.toStdString(),

                    trafficDl,
                    trafficUp,

                    id
                );


            if (!updated)
            {
                MW_show_log(
                    "ProfilesRepo::saveToDatabase: "
                    "UPDATE failed for Profile "
                    +
                    Int2String(id)
                );

                return false;
            }


            return true;
        }


        // =========================================================
        // INSERT missing row
        // =========================================================

        const bool inserted =
            db.exec(
                R"(
                INSERT INTO profiles
                (
                    id,
                    type,
                    name,
                    gid,
                    latency,
                    dl_speed,
                    ul_speed,
                    test_country,
                    ip_out,
                    outbound_json,
                    traffic_dl,
                    traffic_up
                )
                VALUES
                (
                    ?, ?, ?, ?, ?, ?, ?,
                    ?, ?, ?, ?, ?
                )
                )",

                id,

                config.type.toStdString(),
                config.name.toStdString(),
                config.gid,

                test.latency,
                test.dlSpeed.toStdString(),
                test.ulSpeed.toStdString(),
                test.testCountry.toStdString(),
                test.ipOut.toStdString(),

                outboundJson.toStdString(),

                trafficDl,
                trafficUp
            );


        if (!inserted)
        {
            MW_show_log(
                "ProfilesRepo::saveToDatabase: "
                "INSERT failed for Profile "
                +
                Int2String(id)
            );

            return false;
        }


        return true;
    }

    ProfileInsertRow
        ProfilesRepo::profileToInsertRow(
            const Profile* profile,
            int id,
            int gid) const
    {
        ProfileInsertRow row;

        if (!profile)
        {
            return row;
        }


        const auto config =
            profile->ConfigSnapshot();

        const auto test =
            profile->TestSnapshot();

        const auto traffic =
            profile->TrafficSnapshot();


        const QString outboundJson =
            QString::fromUtf8(
                QJsonDocument(
                    config.outboundJson
                )
                .toJson(
                    QJsonDocument::Compact
                )
            );


        row.id =
            id;

        row.type =
            config.type.toStdString();

        row.name =
            config.name.toStdString();

        row.gid =
            gid;

        row.latency =
            test.latency;

        row.dl_speed =
            test.dlSpeed.toStdString();

        row.ul_speed =
            test.ulSpeed.toStdString();

        row.test_country =
            test.testCountry.toStdString();

        row.ip_out =
            test.ipOut.toStdString();

        row.outbound_json =
            outboundJson.toStdString();

        row.traffic_dl =
            static_cast<long long>(
                traffic.downlink
                );

        row.traffic_up =
            static_cast<long long>(
                traffic.uplink
                );


        return row;
    }

    std::shared_ptr<Profile> ProfilesRepo::profileFromRow(SQLite::Statement& stmt) const {
        QJsonObject json;
        json["id"] = stmt.getColumn(0).getInt();
        json["type"] = QString::fromStdString(stmt.getColumn(1).getText());
        json["name"] = QString::fromStdString(stmt.getColumn(2).getText());
        json["gid"] = stmt.getColumn(3).getInt();
        json["latency"] = stmt.getColumn(4).getInt();
        json["dl_speed"] = QString::fromStdString(stmt.getColumn(5).getText());
        json["ul_speed"] = QString::fromStdString(stmt.getColumn(6).getText());
        json["test_country"] = QString::fromStdString(stmt.getColumn(7).getText());
        json["ip_out"] = QString::fromStdString(stmt.getColumn(8).getText());
        
        QString outboundJsonStr = QString::fromStdString(stmt.getColumn(9).getText());
        QJsonDocument outboundDoc = QJsonDocument::fromJson(outboundJsonStr.toUtf8());
        if (!outboundDoc.isNull() && outboundDoc.isObject()) {
            json["outbound"] = outboundDoc.object();
        }
        
        json["traffic_dl"] = static_cast<qint64>(stmt.getColumn(10).getInt64());
        json["traffic_up"] = static_cast<qint64>(stmt.getColumn(11).getInt64());
        
        return profileFromJson(json);
    }

    std::shared_ptr<Profile> ProfilesRepo::loadFromDatabase(int id) const {
        auto query = db.query(R"(
            SELECT id, type, name, gid, latency, dl_speed, ul_speed, test_country, 
                   ip_out, outbound_json, traffic_dl, traffic_up
            FROM profiles WHERE id = ?
        )", id);
        if (!query || !query->executeStep()) {
            return nullptr;
        }
        return profileFromRow(*query);
    }

    std::shared_ptr<Configs::outbound>
        Configs::ProfilesRepo::NewOutbound(
            const QString& type)
    {
        if (type == "socks")
        {
            return std::make_shared<
                Configs::socks
            >();
        }

        if (type == "http")
        {
            return std::make_shared<
                Configs::http
            >();
        }

        if (type == "shadowsocks")
        {
            return std::make_shared<
                Configs::shadowsocks
            >();
        }

        if (type == "chain")
        {
            return std::make_shared<
                Configs::chain
            >();
        }

        if (type == "vmess")
        {
            return std::make_shared<
                Configs::vmess
            >();
        }

        if (type == "trojan")
        {
            return std::make_shared<
                Configs::Trojan
            >();
        }

        if (type == "vless")
        {
            return std::make_shared<
                Configs::vless
            >();
        }

        if (type == "xrayvless")
        {
            return std::make_shared<
                Configs::xrayVless
            >();
        }

        if (type == "hysteria" ||
            type == "hysteria2")
        {
            return std::make_shared<
                Configs::hysteria
            >();
        }

        if (type == "tuic")
        {
            return std::make_shared<
                Configs::tuic
            >();
        }

        if (type == "juicity")
        {
            return std::make_shared<
                Configs::juicity
            >();
        }

        if (type == "trusttunnel")
        {
            return std::make_shared<
                Configs::trusttunnel
            >();
        }

        if (type == "anytls")
        {
            return std::make_shared<
                Configs::anyTLS
            >();
        }

        if (type == "shadowtls")
        {
            return std::make_shared<
                Configs::shadowtls
            >();
        }

        if (type == "wireguard")
        {
            return std::make_shared<
                Configs::wireguard
            >();
        }

        if (type == "tailscale")
        {
            return std::make_shared<
                Configs::tailscale
            >();
        }

        if (type == "ssh")
        {
            return std::make_shared<
                Configs::ssh
            >();
        }

        if (type == "custom")
        {
            return std::make_shared<
                Configs::Custom
            >();
        }

        if (type == "extracore")
        {
            return std::make_shared<
                Configs::extracore
            >();
        }

        if (type == "naive")
        {
            return std::make_shared<
                Configs::naive
            >();
        }

        if (type == "direct")
        {
            return std::make_shared<
                Configs::direct
            >();
        }
        auto invalid =
            std::make_shared<
            Configs::outbound
            >();
        invalid->invalid =
            true;
        return invalid;
    }

    std::shared_ptr<Configs::Profile>
        Configs::ProfilesRepo::NewProfile(
            const QString& type)
    {
        auto outbound =
            NewOutbound(
                type
            );


        if (!outbound)
        {
            return nullptr;
        }


        return std::make_shared<
            Configs::Profile
        >(
            std::move(
                outbound
            ),
            type
        );
    }

    bool ProfilesRepo::AddProfile(
        std::shared_ptr<Profile>& profile,
        int gid)
    {
        // =========================================================
        // Basic validation
        // =========================================================

        if (!profile)
        {
            MW_show_log(
                "ProfilesRepo::AddProfile: "
                "null Profile"
            );

            return false;
        }


        // AddProfile() is only intended for a new,
        // unpublished Profile.
        //
        // A Profile with id >= 0 is already published
        // into persistent repository state.
        if (profile->Id() >= 0)
        {
            MW_show_log(
                "ProfilesRepo::AddProfile: "
                "Profile is already published"
            );

            return false;
        }


        // =========================================================
        // Use the transactional batch implementation
        // =========================================================
        //
        // A single Profile is just a batch containing one element.
        //
        // This is intentional:
        //
        // AddProfileBatch() already implements:
        //
        //   1. target Group validation;
        //   2. ID reservation;
        //   3. Profile identity assignment;
        //   4. atomic SQLite INSERT;
        //   5. identityMap publication;
        //   6. Group publication;
        //   7. Group persistence;
        //   8. complete rollback on failure.
        //
        // Keeping all of this in ONE implementation avoids
        // AddProfile() and AddProfileBatch() drifting apart again.
        // =========================================================

        QList<std::shared_ptr<Profile>>
            batch;


        batch.reserve(1);


        batch.append(
            profile
        );


        return AddProfileBatch(
            batch,
            gid
        );
    }

    bool ProfilesRepo::AddProfileBatch(
        QList<std::shared_ptr<Profile>>& profiles,
        int gid)
    {
        // =========================================================
        // Basic validation
        // =========================================================

        if (!Configs::dataManager ||
            !Configs::dataManager->settingsRepo ||
            !Configs::dataManager->groupsRepo)
        {
            return false;
        }


        // Empty batch is already successfully complete.
        if (profiles.isEmpty())
        {
            return true;
        }


        // =========================================================
        // Resolve target Group
        // =========================================================

        const int targetGid =
            gid < 0
            ? Configs::dataManager
            ->settingsRepo
            ->current_group
            : gid;


        auto group =
            Configs::dataManager
            ->groupsRepo
            ->GetGroup(
                targetGid
            );


        if (!group)
        {
            MW_show_log(
                "ProfilesRepo::AddProfileBatch: "
                "group not found: "
                +
                Int2String(
                    targetGid
                )
            );


            return false;
        }


        // =========================================================
        // Strict pre-validation
        //
        // ALL entries must be valid unpublished Profiles.
        //
        // Do NOT silently filter the input list.
        // =========================================================

        QSet<const Profile*>
            uniqueProfiles;


        uniqueProfiles.reserve(
            profiles.size()
        );


        for (const auto& profile :
            profiles)
        {
            if (!profile)
            {
                MW_show_log(
                    "ProfilesRepo::AddProfileBatch: "
                    "null Profile in batch"
                );


                return false;
            }


            // AddProfileBatch is only for NEW unpublished Profiles.
            if (profile->Id() >= 0)
            {
                MW_show_log(
                    "ProfilesRepo::AddProfileBatch: "
                    "batch contains an already published Profile"
                );


                return false;
            }


            // The same shared_ptr twice would make the first
            // TryAssignIdentity succeed and the second fail.
            if (uniqueProfiles.contains(
                profile.get()))
            {
                MW_show_log(
                    "ProfilesRepo::AddProfileBatch: "
                    "duplicate Profile object in batch"
                );


                return false;
            }


            uniqueProfiles.insert(
                profile.get()
            );
        }


        // =========================================================
        // Reserve one contiguous ID range
        // =========================================================

        const int count =
            profiles.size();


        const int firstId =
            NewProfileIDRange(
                count
            );


        if (firstId <= 0)
        {
            MW_show_log(
                "ProfilesRepo::AddProfileBatch: "
                "failed to reserve profile IDs"
            );


            return false;
        }


        // =========================================================
        // Prepare rollback bookkeeping
        // =========================================================

        struct AssignedProfile
        {
            std::shared_ptr<Profile> profile;
            int id = -1;
        };


        QList<AssignedProfile>
            assignedProfiles;


        assignedProfiles.reserve(
            count
        );


        QList<int>
            profileIDs;


        profileIDs.reserve(
            count
        );


        std::vector<ProfileInsertRow>
            rows;


        rows.reserve(
            static_cast<size_t>(
                count
                )
        );


        // =========================================================
        // Rollback helper
        // =========================================================

        const auto rollbackIdentities =
            [&assignedProfiles,
            targetGid]()
            {
                // Reverse order is preferable for rollback.
                for (auto it =
                    assignedProfiles.rbegin();
                    it !=
                    assignedProfiles.rend();
                    ++it)
                {
                    if (!it->profile)
                    {
                        continue;
                    }


                    const bool rolledBack =
                        it->profile
                        ->RollbackAssignedIdentity(
                            it->id,
                            targetGid
                        );


                    if (!rolledBack)
                    {
                        MW_show_log(
                            "ProfilesRepo::AddProfileBatch: "
                            "failed to rollback Profile identity"
                        );
                    }
                }
            };


        // =========================================================
        // Assign ALL identities
        //
        // If ONE assignment fails -> roll back EVERYTHING.
        // =========================================================

        for (int i = 0;
            i < count;
            ++i)
        {
            const int newId =
                firstId + i;


            auto& profile =
                profiles[i];


            if (!profile->TryAssignIdentity(
                newId,
                targetGid))
            {
                MW_show_log(
                    "ProfilesRepo::AddProfileBatch: "
                    "failed to assign identity; "
                    "rolling back entire batch"
                );


                rollbackIdentities();


                return false;
            }


            assignedProfiles.append(
                {
                    profile,
                    newId
                }
            );


            profileIDs.append(
                newId
            );


            rows.push_back(
                profileToInsertRow(
                    profile.get(),
                    newId,
                    targetGid
                )
            );
        }


        // =========================================================
        // Defensive invariant checks
        // =========================================================

        if (assignedProfiles.size() != count ||
            profileIDs.size() != count ||
            rows.size() !=
            static_cast<size_t>(count))
        {
            MW_show_log(
                "ProfilesRepo::AddProfileBatch: "
                "internal batch size mismatch"
            );


            rollbackIdentities();


            return false;
        }


        // =========================================================
// Atomic DB INSERT
//
// Nothing is published into Group before the complete
// Profile batch has been committed to SQLite.
// =========================================================

        {
            QMutexLocker locker(
                &mutex
            );


            const bool inserted =
                db.execBatchInsertProfiles(
                    rows
                );


            if (!inserted)
            {
                // Do not hold ProfilesRepo::mutex while taking
                // Profile configuration locks during rollback.
                locker.unlock();


                MW_show_log(
                    "ProfilesRepo::AddProfileBatch: "
                    "database transaction failed; "
                    "rolling back identities"
                );


                rollbackIdentities();


                return false;
            }


            // =====================================================
            // Publish profiles into identity map only AFTER
            // SQLite transaction has committed successfully.
            // =====================================================

            for (const auto& assigned :
                assignedProfiles)
            {
                identityMap[
                    assigned.id
                ] =
                    std::weak_ptr<Profile>(
                        assigned.profile
                    );
            }
        }


        // =========================================================
        // FULL ROLLBACK
        //
        // At this point Profile rows already exist in SQLite and
        // have been published into identityMap.
        //
        // If publication into Group or Group persistence fails,
        // undo all of that.
        // =========================================================

        const auto rollbackCommittedBatch =
            [
                this,
                &assignedProfiles,
                &profileIDs,
                &rollbackIdentities,
                group
            ]() -> bool
            {
                bool rollbackOk =
                    true;


                // =================================================
                // 1. Remove newly-added IDs from Group memory
                // =================================================

                if (group)
                {
                    group->RemoveProfileBatch(
                        profileIDs
                    );
                }


                // =================================================
                // 2. Convert QList<int> to std::vector<int>
                // =================================================

                std::vector<int>
                    ids;


                ids.reserve(
                    static_cast<size_t>(
                        profileIDs.size()
                        )
                );


                for (const int id :
                profileIDs)
                {
                    ids.push_back(
                        id
                    );
                }


                // =================================================
                // 3. Remove committed Profile rows from SQLite
                // =================================================

                const bool deleted =
                    db.deleteProfilesAtomic(
                        ids
                    );


                if (!deleted)
                {
                    MW_show_log(
                        "ProfilesRepo::AddProfileBatch: "
                        "CRITICAL: failed to remove "
                        "Profile rows during rollback"
                    );


                    rollbackOk =
                        false;
                }


                // =================================================
                // 4. Remove Profiles from identity map
                // =================================================

                {
                    QMutexLocker locker(
                        &mutex
                    );


                    for (const auto& assigned :
                        assignedProfiles)
                    {
                        identityMap.erase(
                            assigned.id
                        );
                    }
                }


                // =================================================
                // 5. Return Profile objects to unpublished state
                // =================================================

                rollbackIdentities();


                return rollbackOk;
            };


        // =========================================================
        // Publish IDs into Group
        // =========================================================

        const bool groupChanged =
            group->AddProfileBatch(
                profileIDs
            );


        if (!groupChanged)
        {
            MW_show_log(
                "ProfilesRepo::AddProfileBatch: "
                "failed to publish Profile IDs into Group; "
                "rolling back complete batch"
            );


            rollbackCommittedBatch();


            return false;
        }


        // =========================================================
        // Persist Group
        // =========================================================

        const bool groupSaved =
            Configs::dataManager
            ->groupsRepo
            ->Save(
                group
            );


        if (!groupSaved)
        {
            MW_show_log(
                "ProfilesRepo::AddProfileBatch: "
                "failed to persist Group; "
                "rolling back complete batch"
            );


            rollbackCommittedBatch();


            return false;
        }


        // =========================================================
        // Complete success
        // =========================================================

        return true;
    }

    std::shared_ptr<Profile>
        ProfilesRepo::GetProfile(
            int id) const
    {
        if (id < 0)
        {
            return nullptr;
        }


        for (;;)
        {
            std::uint64_t
                deletionEpochSnapshot = 0;


            // =====================================================
            // PHASE 1
            //
            // Check identity map.
            // =====================================================

            {
                std::lock_guard<std::mutex>
                    locker(
                        mutex
                    );


                deletionEpochSnapshot =
                    deletionEpoch_;


                auto it =
                    identityMap.find(
                        id
                    );


                if (it !=
                    identityMap.end())
                {
                    if (auto existing =
                        it->second.lock())
                    {
                        return existing;
                    }


                    identityMap.erase(
                        it
                    );
                }
            }


            // =====================================================
            // PHASE 2
            //
            // SQLite read WITHOUT ProfilesRepo::mutex.
            // =====================================================

            auto loadedProfile =
                loadFromDatabase(
                    id
                );


            if (!loadedProfile)
            {
                return nullptr;
            }


            // =====================================================
            // PHASE 3
            //
            // Publish result.
            // =====================================================

            {
                std::lock_guard<std::mutex>
                    locker(
                        mutex
                    );


                // Profile may have been deleted while DB SELECT
                // was running.
                if (deletionEpoch_ !=
                    deletionEpochSnapshot)
                {
                    continue;
                }


                // Another reader may already have published the
                // same Profile.
                auto it =
                    identityMap.find(
                        id
                    );


                if (it !=
                    identityMap.end())
                {
                    if (auto existing =
                        it->second.lock())
                    {
                        return existing;
                    }


                    identityMap.erase(
                        it
                    );
                }


                identityMap[id] =
                    std::weak_ptr<Profile>(
                        loadedProfile
                    );


                return loadedProfile;
            }
        }
    }

    std::map<int, std::shared_ptr<Profile>> ProfilesRepo::loadProfilesByIdsChunk(const QList<int>& chunkIds) const {
        std::map<int, std::shared_ptr<Profile>> result;
        if (chunkIds.isEmpty()) return result;
        QString idList;
        for (int i = 0; i < chunkIds.size(); ++i) {
            if (i > 0) idList += ",";
            idList += QString::number(chunkIds[i]);
        }
        std::string sql = "SELECT id, type, name, gid, latency, dl_speed, ul_speed, test_country, "
                         "ip_out, outbound_json, traffic_dl, traffic_up FROM profiles WHERE id IN (" +
                         idList.toStdString() + ") ORDER BY id";
        auto query = db.query(sql);
        if (!query) return result;
        while (query->executeStep()) {
            auto profile = profileFromRow(*query);
            result[profile->Id()] =
                std::move(profile);
        }
        return result;
    }

    QList<std::shared_ptr<Profile>>
        ProfilesRepo::GetProfileBatch(
            const QList<int>& ids)
    {
        if (ids.isEmpty())
        {
            return {};
        }


        // =========================================================
        // Retry loop
        //
        // Usually executes exactly once.
        //
        // A retry is necessary only when Profiles are deleted
        // while this method performs SQLite I/O outside
        // ProfilesRepo::mutex.
        // =========================================================

        for (;;)
        {
            std::map<
                int,
                std::shared_ptr<Profile>
            > byId;


            QList<int>
                missingIds;


            std::uint64_t
                deletionEpochSnapshot = 0;


            // =====================================================
            // PHASE 1
            //
            // Read identityMap under a SHORT repository lock.
            //
            // NO SQLite access in this scope.
            // =====================================================

            {
                std::lock_guard<std::mutex>
                    locker(
                        mutex
                    );


                deletionEpochSnapshot =
                    deletionEpoch_;


                for (const int id :
                ids)
                {
                    if (id < 0)
                    {
                        continue;
                    }


                    auto it =
                        identityMap.find(
                            id
                        );


                    if (it !=
                        identityMap.end())
                    {
                        if (auto existing =
                            it->second.lock())
                        {
                            // Profile is already alive.
                            byId[id] =
                                std::move(
                                    existing
                                );


                            continue;
                        }


                        // weak_ptr expired.
                        //
                        // Remove stale cache entry.
                        identityMap.erase(
                            it
                        );
                    }


                    // Avoid duplicate DB lookup when input contains
                    // the same profile ID several times.
                    if (!missingIds.contains(
                        id))
                    {
                        missingIds.append(
                            id
                        );
                    }
                }
            }


            // =====================================================
            // Repository mutex is RELEASED here.
            // =====================================================


            // =====================================================
            // Fast path
            //
            // Everything was found in identityMap.
            // =====================================================

            if (missingIds.isEmpty())
            {
                QList<
                    std::shared_ptr<Profile>
                > result;


                result.reserve(
                    ids.size()
                );


                for (const int id :
                ids)
                {
                    const auto it =
                        byId.find(
                            id
                        );


                    if (it !=
                        byId.end())
                    {
                        result.append(
                            it->second
                        );
                    }
                }


                return result;
            }


            // =====================================================
            // PHASE 2
            //
            // Load missing Profiles from SQLite.
            //
            // IMPORTANT:
            // ProfilesRepo::mutex is NOT held here.
            //
            // Database has its own synchronization.
            // =====================================================

            std::map<
                int,
                std::shared_ptr<Profile>
            > loadedById;


            for (
                int offset = 0;
                offset < missingIds.size();
                offset += Configs::BATCH_LIMIT_READ)
            {
                const int end =
                    std::min(
                        offset
                        +
                        Configs::BATCH_LIMIT_READ,

                        static_cast<int>(
                            missingIds.size()
                            )
                    );


                const QList<int> chunk =
                    missingIds.sliced(
                        offset,
                        end - offset
                    );

                if (chunk.isEmpty())
                {
                    continue;
                }

                auto loaded =
                    loadProfilesByIdsChunk(
                        chunk
                    );

                for (auto& [
                    id,
                    profile
                ] :
                    loaded)
                {
                    if (!profile)
                    {
                        continue;
                    }


                    loadedById[id] =
                        std::move(
                            profile
                        );
                }
            }

            // =====================================================
            // PHASE 3
            //
            // Re-enter repository state.
            //
            // We must now:
            //
            // 1. detect concurrent deletions;
            // 2. avoid replacing an object another thread has
            //    already published;
            // 3. publish newly loaded objects.
            // =====================================================
            bool retry =
                false;
            {
                std::lock_guard<std::mutex>
                    locker(
                        mutex
                    );


                // -------------------------------------------------
                // A delete happened while SQLite was being read.
                //
                // Some of loadedById may now represent rows which
                // no longer exist.
                //
                // Do NOT publish them.
                // -------------------------------------------------
                if (deletionEpoch_ !=
                    deletionEpochSnapshot)
                {
                    retry =
                        true;
                }
                else
                {
                    for (auto& [
                        id,
                        loadedProfile
                    ] :
                        loadedById)
                    {
                        if (!loadedProfile)
                        {
                            continue;
                        }

                        // =========================================
                        // Another thread may have loaded the same
                        // Profile while our DB query was running.
                        //
                        // identityMap object wins.
                        // =========================================
                        auto existingIt =
                            identityMap.find(
                                id
                            );


                        if (existingIt !=
                            identityMap.end())
                        {
                            if (auto existing =
                                existingIt
                                ->second
                                .lock())
                            {
                                byId[id] =
                                    std::move(
                                        existing
                                    );


                                continue;
                            }

                            // Stale weak_ptr.
                            identityMap.erase(
                                existingIt
                            );
                        }

                        // =========================================
                        // Nobody else published it.
                        //
                        // Publish our loaded Profile.
                        // =========================================
                        identityMap[id] =
                            std::weak_ptr<Profile>(
                                loadedProfile
                            );


                        byId[id] =
                            std::move(
                                loadedProfile
                            );
                    }
                }
            }

            // =====================================================
            // Concurrent delete happened.
            //
            // Start again using the new persistent state.
            // =====================================================
            if (retry)
            {
                continue;
            }


            // =====================================================
            // PHASE 4
            //
            // Restore exact caller order.
            //
            // The caller may supply:
            //
            //     [5, 2, 7, 5]
            //
            // and we preserve that order, including duplicates.
            // =====================================================
            QList<
                std::shared_ptr<Profile>
            > result;


            result.reserve(
                ids.size()
            );


            for (const int id :
            ids)
            {
                const auto it =
                    byId.find(
                        id
                    );


                if (it !=
                    byId.end())
                {
                    result.append(
                        it->second
                    );
                }
            }


            return result;
        }
    }

    QList<std::pair<int, QString> > ProfilesRepo::GetProfileIDNameMappedBatch(QList<int> ids) {
        QList<std::pair<int, QString> > result;
        if (ids.isEmpty()) return result;

        std::map<int, QString> idToName;

        for (int off = 0; off < ids.size(); off += Configs::BATCH_LIMIT_READ) {
            const int end = std::min(off + Configs::BATCH_LIMIT_READ, static_cast<int>(ids.size()));
            const auto chunk = ids.sliced(off, end - off);
            if (chunk.isEmpty()) continue;

            QString idList;
            for (int i = 0; i < chunk.size(); ++i) {
                if (i > 0) idList += ",";
                idList += QString::number(chunk[i]);
            }
            const std::string sql = "SELECT id, name FROM profiles WHERE id IN (" + idList.toStdString() + ") ORDER BY id";
            auto query = db.query(sql);
            if (!query) continue;
            while (query->executeStep()) {
                const int id = query->getColumn(0).getInt();
                idToName[id] = QString::fromStdString(query->getColumn(1).getText());
            }
        }

        for (int id : ids) {
            const auto it = idToName.find(id);
            if (it != idToName.end()) {
                result.append({it->first, it->second});
            }
        }
        return result;
    }

    std::shared_ptr<Profile>
        ProfilesRepo::GetProfileByName(
            const QString& name)
    {
        int id = -1;

        // DB query has its own scope.
        //
        // DatabaseQuery must be destroyed BEFORE
        // GetProfile() acquires ProfilesRepo::mutex.
        {
            auto query =
                db.query(
                    "SELECT id "
                    "FROM profiles "
                    "WHERE name = ? "
                    "LIMIT 1",
                    name.toStdString()
                );

            if (!query ||
                !query->executeStep())
            {
                return nullptr;
            }

            id =
                query
                ->getColumn(0)
                .getInt();
        }

        // db_mutex has already been released here.
        return GetProfile(id);
    }

    QList<std::pair<int, QString> > ProfilesRepo::GetAllProfileIDNameMapped() {
        auto query = db.query("SELECT id, name FROM profiles ORDER BY id");
        if (!query) return {};
        QList<std::pair<int, QString> > res;
        while (query->executeStep()) {
            res.append({query->getColumn(0).getInt(), QString(query->getColumn(1).getString().c_str())});
        }
        return res;
    }

    QStringList ProfilesRepo::GetAllProfileNames() {
        auto query = db.query("SELECT name FROM profiles ORDER BY id");
        if (!query) return {};
        QStringList names;
        while (query->executeStep()) {
            names.append(QString(query->getColumn(0).getString().c_str()));
        }
        return names;
    }

    bool ProfilesRepo::BatchDeleteProfiles(QList<int>& ids, bool stopRunningProfile) {
        QSet<int> groupIDs;
        if (ids.contains(dataManager->settingsRepo->started_id)) {
            if (stopRunningProfile) {
                MainWindowApi::StopProfile(false, true, false);
            }
            else ids.removeAll(dataManager->settingsRepo->started_id);
        }
        auto profiles = GetProfileBatch(ids);
        for (const auto& ent : profiles) {
            groupIDs.insert(
                ent->GroupId()
            );
        }
        for (auto groupID : groupIDs) {
            auto group = dataManager->groupsRepo->GetGroup(groupID);
            if (!group) {
                MW_show_log("Could not find group with id " + Int2String(groupID));
                return false;
            }
            group->RemoveProfileBatch(ids);
            dataManager->groupsRepo->Save(group);
        }

        // =========================================================
        // Delete persistent Profiles
        // =========================================================
        {
            std::lock_guard<std::mutex>
                locker(
                    mutex
                );


            if (!ids.isEmpty())
            {
                // -------------------------------------------------
                // Invalidate any GetProfile/GetProfileBatch
                // operation which started before this deletion.
                //
                // Increment while repository mutex is held.
                // -------------------------------------------------
                ++deletionEpoch_;


                // -------------------------------------------------
                // Remove cached objects first.
                // -------------------------------------------------
                for (const int id :
                ids)
                {
                    identityMap.erase(
                        id
                    );
                }


                // -------------------------------------------------
                // Keep the repository mutex during DELETE.
                //
                // Deletion is relatively rare.
                //
                // This is intentional: it guarantees that readers
                // cannot enter their publication phase between
                // epoch invalidation and persistent deletion.
                // -------------------------------------------------
                const std::vector<int> idVec(
                    ids.begin(),
                    ids.end()
                );

                db.execDeleteByIdIn(
                    "profiles",
                    "id",
                    idVec
                );
            }
        }
        return true;
    }

    QList<int> ProfilesRepo::GetAllProfileIds() const {
        QList<int> ids;
        auto query = db.query("SELECT id FROM profiles ORDER BY id");
        if (query) {
            while (query->executeStep()) {
                ids.append(query->getColumn(0).getInt());
            }
        }
        return ids;
    }
    
    // Don't used anymore
    //int ProfilesRepo::NewProfileID() const {
    //    // Atomically increment and get the new ID using RETURNING clause (DB atomic, no lock required)
    //    auto query = db.query("UPDATE entity_ids SET profile_last_id = profile_last_id + 1 RETURNING profile_last_id");
    //    if (query && query->executeStep()) {
    //        return query->getColumn(0).getInt();
    //    }
    //    return 0;
    //}

    int ProfilesRepo::NewProfileIDRange(int n) const {
        if (n <= 0) return 0;
        // Atomically reserve n IDs; RETURNING gives the new value (old + n), so first ID = newValue - n + 1
        auto query = db.query("UPDATE entity_ids SET profile_last_id = profile_last_id + ? RETURNING profile_last_id", n);
        if (query && query->executeStep()) {
            int newValue = query->getColumn(0).getInt();
            return newValue - n + 1;
        }
        return 0;
    }

    bool ProfilesRepo::Save(
        const std::shared_ptr<Profile>& profile)
    {
        // =========================================================
        // Validation
        // =========================================================

        if (!profile)
        {
            MW_show_log(
                "ProfilesRepo::Save: "
                "null Profile"
            );

            return false;
        }


        const int id =
            profile->Id();


        if (id < 0)
        {
            MW_show_log(
                "ProfilesRepo::Save: "
                "cannot save unpublished Profile"
            );

            return false;
        }


        // =========================================================
        // Persist FIRST
        //
        // Do not update identityMap before SQLite confirms the save.
        // =========================================================

        const bool persisted =
            saveToDatabase(
                profile.get(),
                id
            );


        if (!persisted)
        {
            MW_show_log(
                "ProfilesRepo::Save: "
                "failed to persist Profile "
                +
                Int2String(id)
            );

            return false;
        }


        // =========================================================
        // Publish in-memory state only AFTER DB success
        // =========================================================

        {
            std::lock_guard<std::mutex>
                locker(
                    mutex
                );


            identityMap[id] =
                std::weak_ptr<Profile>(
                    profile
                );
        }


        return true;
    }

    bool ProfilesRepo::SaveTraffic(
        const std::shared_ptr<Profile>& profile)
    {
        if (!profile)
        {
            return false;
        }


        const int id =
            profile->Id();


        if (id < 0)
        {
            return false;
        }


        // Freeze traffic snapshot before SQLite I/O.
        const auto traffic =
            profile->TrafficSnapshot();


        const long long dl =
            static_cast<long long>(
                traffic.downlink
                );


        const long long up =
            static_cast<long long>(
                traffic.uplink
                );


        // =========================================================
        // No ProfilesRepo::mutex needed here.
        //
        // Database serializes its own SQLite connection.
        // This operation does not touch identityMap.
        // =========================================================

        const bool persisted =
            db.exec(
                "UPDATE profiles "
                "SET traffic_dl = ?, "
                "traffic_up = ?, "
                "updated_at = strftime('%s', 'now') "
                "WHERE id = ?",

                dl,
                up,
                id
            );


        if (!persisted)
        {
            MW_show_log(
                "ProfilesRepo::SaveTraffic: "
                "failed to persist traffic for Profile "
                +
                Int2String(id)
            );

            return false;
        }


        return true;
    }

    void ProfilesRepo::SaveBatch(
        const QList<std::shared_ptr<Profile>>& profiles)
    {
        if (profiles.isEmpty()) {
            return;
        }


        // -------------------------------------------------
        // SaveBatch is synchronous now.
        //
        // No new QThread is created.
        //
        // The repository mutex is acquired BEFORE creating
        // ProfileInsertRow snapshots so that SaveTrafficBatch()
        // cannot persist a newer traffic value between
        // snapshot creation and the batch write.
        // -------------------------------------------------

        std::lock_guard<std::mutex>
            locker(mutex);


        std::vector<ProfileInsertRow>
            rows;

        rows.reserve(
            static_cast<size_t>(
                profiles.size()
                )
        );


        // Keep the exact ID associated with each snapshot.
        //
        // Do not read profile->id again after the DB operation.
        std::vector<
            std::pair<
            int,
            std::weak_ptr<Profile>
            >
        > identityUpdates;

        identityUpdates.reserve(
            static_cast<size_t>(
                profiles.size()
                )
        );


        // -------------------------------------------------
        // Build immutable persistence snapshots
        // -------------------------------------------------

        for (const auto& profile : profiles) {

            if (!profile) {
                continue;
            }


            const auto config =
                profile->ConfigSnapshot();

            const int id =
                config.id;

            const int gid =
                config.gid;

            rows.push_back(
                profileToInsertRow(
                    profile.get(),
                    id,
                    gid
                )
            );


            identityUpdates.emplace_back(
                id,
                std::weak_ptr<Profile>(
                    profile
                )
            );
        }


        if (rows.empty()) {
            return;
        }


        // -------------------------------------------------
        // Persist immutable rows
        // -------------------------------------------------

        db.execBatchReplaceProfiles(
            rows
        );


        // -------------------------------------------------
        // Refresh identity map
        // -------------------------------------------------

        for (const auto& [
            id,
            weakProfile
        ] : identityUpdates)
        {
            identityMap[id] =
                weakProfile;
        }
    }

    void ProfilesRepo::SaveTrafficBatch(
        const std::vector<ProfileTrafficRow>& rows)
    {
        if (rows.empty()) {
            return;
        }

        // Serialize operations inside ProfilesRepo.
        std::lock_guard<std::mutex> locker(mutex);

        db.execBatchUpdateProfileTraffic(rows);
    }
}