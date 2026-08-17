#pragma once

#include "Database.h"
#include "include/database/entities/Profile.h"
#include <3rdparty/SQLiteCpp/include/SQLiteCpp.h>

#include <memory>
#include <mutex>
#include <map>
#include <vector>
#include <cstdint>

#include <QString>
#include <QJsonObject>


namespace Configs {
    class ProfilesRepo {
    private:
        Database& db;
        mutable std::mutex mutex;
        
        // Identity map:
        //
        // profile ID -> currently published Profile object.
        mutable std::map<
            int,
            std::weak_ptr<Profile>
        > identityMap;

        // Incremented whenever Profiles are deleted from
        // persistent storage.
        //
        // GetProfile()/GetProfileBatch() use this value to
        // detect a delete that happened while SQLite was being
        // read without holding ProfilesRepo::mutex.
        std::uint64_t deletionEpoch_ = 0;

        // Helper to serialize Profile to JSON
        QJsonObject profileToJson(const Profile* profile) const;
        
        // Helper to deserialize Profile from JSON
        std::shared_ptr<Profile> profileFromJson(const QJsonObject& json) const;
        
        // Save Profile snapshot to SQLite.
        //
        // Returns true only when the database operation
        // completed successfully.
        [[nodiscard]]
        bool saveToDatabase(
            const Profile* profile,
            int id
        ) const;

        // Build one row for batch insert (same columns as saveToDatabase)
        ProfileInsertRow profileToInsertRow(const Profile* profile, int id, int gid) const;

        // Load profile from database
        std::shared_ptr<Profile> loadFromDatabase(int id) const;
        
        // Build profile from current row of a SELECT (same columns as loadFromDatabase)
        std::shared_ptr<Profile> profileFromRow(SQLite::Statement& stmt) const;

        // Load profiles for given ids (one SELECT IN chunk). Does not touch identity map.
        std::map<int, std::shared_ptr<Profile>> loadProfilesByIdsChunk(const QList<int>& ids) const;
        
        // Create tables if they don't exist
        void createTables() const;

        // Get next available profile ID (single)
        int NewProfileID() const;
        // Allocate a contiguous block of n IDs; returns first ID (use firstId, firstId+1, ..., firstId+n-1). DB atomic, no lock required.
        int NewProfileIDRange(int n) const;

    public:

        explicit ProfilesRepo(
            Database& database
        );

        // -----------------------------------------------------
        // Create outbound object without publishing a Profile.
        //
        // Used by Profile copy-on-write configuration.
        // -----------------------------------------------------
        [[nodiscard]]
        static std::shared_ptr<Configs::outbound>
            NewOutbound(
                const QString& type
            );

        // -----------------------------------------------------
        // Create detached/unpublished Profile
        // -----------------------------------------------------
        [[nodiscard]]
        static std::shared_ptr<Profile>
            NewProfile(
                const QString& type
            );
        
        // Add one new unpublished Profile.
        //
        // Uses the same all-or-nothing transactional path
        // as AddProfileBatch().
        bool AddProfile(
            std::shared_ptr<Profile>& profile,
            int gid = -1
        );


        // Add multiple new unpublished Profiles atomically.
        //
        // Either the complete batch is published into
        // SQLite/repository/Group state, or the operation
        // rolls back.
        bool AddProfileBatch(
            QList<std::shared_ptr<Profile>>& profiles,
            int gid = -1
        );
        
        // Get profile by ID (uses identity map)
        std::shared_ptr<Profile> GetProfile(int id) const;

        [[nodiscard]]
        QList<std::shared_ptr<Profile>>
            GetProfileBatch(
                const QList<int>& ids
            );

        QList<std::pair<int, QString> > GetProfileIDNameMappedBatch(QList<int> ids);

        std::shared_ptr<Profile> GetProfileByName(const QString &name);

        QList<std::pair<int, QString> > GetAllProfileIDNameMapped();

        QStringList GetAllProfileNames();
        
        // Delete multiple profiles
        bool BatchDeleteProfiles(QList<int>& ids, bool stopRunningProfile = false);
        
        // Get all profile IDs in order
        QList<int> GetAllProfileIds() const;
        
        // Save profile to database (manual save, like old Save() method)
        // Only saves if profile has a valid ID (id >= 0)
        bool Save(const std::shared_ptr<Profile>& profile);

        // Update only the traffic field of the profile in the database (no existence check, just UPDATE).
        bool SaveTraffic(const std::shared_ptr<Profile>& profile);

        void SaveTrafficBatch(
            const std::vector<ProfileTrafficRow>& rows);

        // Batch-save existing profiles.
        //
        // Creates immutable ProfileInsertRow snapshots and
        // persists them as one serialized batch operation.
        // Does not create a worker thread.
        void SaveBatch(
            const QList<std::shared_ptr<Profile>>& profiles);
    };
}
