#pragma once

#include "include/global/Utils.hpp"
#include <3rdparty/SQLiteCpp/include/SQLiteCpp.h>
#include <3rdparty/SQLiteCpp/include/Statement.h>

#include <algorithm>
#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Configs 
{
    struct ProfileInsertRow 
    {
        int id;
        std::string type;
        std::string name;
        int gid;
        int latency;
        std::string dl_speed;
        std::string ul_speed;
        std::string test_country;
        std::string ip_out;
        std::string outbound_json;
        long long traffic_dl = 0;
        long long traffic_up = 0;
    };
    
    struct ProfileTrafficRow 
    {
        int id = -1;
        long long traffic_dl = 0;
        long long traffic_up = 0;
    };
    // Which logical categories a backup contains / a restore should apply.
    // profiles -> groups, groups_order, profiles tables
    // routes   -> route_profiles, route_rules tables
    // settings -> settings table
    // icons    -> icons/ folder (handled by the UI layer, not the database)

    struct GroupInsertRow
    {
        int id = -1;

        bool archive = false;
        bool skip_auto_update = false;
        bool auto_clear_unavailable = false;

        std::string name;
        std::string url;
        std::string info;

        long long sub_last_update = 0;

        int front_proxy_id = -1;
        int landing_proxy_id = -1;

        std::string column_width_json;
        std::string profiles_json;

        int scroll_last_profile = -1;

        int test_sort_by = 0;
        int traffic_sort_by = 0;
        int test_items_to_show = 0;
    };

    // =============================================================
    // Partial Group update used by atomic Profile deletion.
    //
    // We deliberately store ONLY profiles_json here.
    //
    // BatchDeleteProfiles must not overwrite unrelated Group fields
    // from an old snapshot.
    // =============================================================
    struct GroupProfilesUpdateRow
    {
        int id = -1;

        std::string profiles_json;
    };

    struct RouteRuleInsertRow
    {
        std::string name;

        int type = 0;

        std::string ip_version;
        std::string network;
        std::string protocol;

        std::string inbound_json;
        std::string domain_json;
        std::string domain_suffix_json;
        std::string domain_keyword_json;
        std::string domain_regex_json;

        std::string source_ip_cidr_json;
        bool source_ip_is_private = false;

        std::string ip_cidr_json;
        bool ip_is_private = false;

        std::string source_port_json;
        std::string source_port_range_json;

        std::string port_json;
        std::string port_range_json;

        std::string process_name_json;
        std::string process_path_json;
        std::string process_path_regex_json;

        std::string rule_set_json;

        bool invert = false;

        int outbound_id = -2;

        std::string action;
        std::string reject_method;

        bool no_drop = false;

        std::string override_address;
        std::string override_port;

        std::string sniffers_json;

        bool sniff_override_dest = false;

        std::string strategy;

        std::string wifi_ssid_json;
        std::string wifi_bssid_json;
    };


    struct RouteProfileSaveRow
    {
        int id = -1;

        std::string name;

        int default_outbound_id = -1;

        std::vector<RouteRuleInsertRow>
            rules;
    };

    struct BackupParts 
    {
        bool profiles = false;
        bool routes = false;
        bool settings = false;
        bool icons = false;

        [[nodiscard]] bool anyDb() const { return profiles || routes || settings; }
        [[nodiscard]] bool any() const { return profiles || routes || settings || icons; }
    };

    // Max bound parameters per statement (SQLite default SQLITE_MAX_VARIABLE_NUMBER is 999).
    constexpr int BATCH_LIMIT_WRITE = 1500;
    constexpr int BATCH_LIMIT_READ = 4096;
    // Run WAL checkpoint after this many write operations (exec or batch chunk).
    constexpr int WAL_CHECKPOINT_AFTER_WRITES = 10000;

    inline void NotifyError(const std::string& query, std::exception& e) 
    {
        runOnUiThread([=] {
            std::string shortQ;
            if (query.length() > 200) shortQ = query.substr(0, 200);
            else shortQ = query;
            MessageBoxWarning("DB error occurred", "Failed for " + QString::fromStdString(shortQ) + " with exception: " + e.what() + "\n your database may be corrupted");
        });
    }

    class DatabaseQuery
    {
    public:
        DatabaseQuery() = default;


        DatabaseQuery(
            std::recursive_mutex& mutex,
            SQLite::Database& database,
            const std::string& sql)
            :
            lock_(mutex),
            statement_(
                std::make_unique<SQLite::Statement>(
                    database,
                    sql
                )
            )
        {}


        DatabaseQuery(
            const DatabaseQuery&) = delete;

        DatabaseQuery& operator=(
            const DatabaseQuery&) = delete;


        DatabaseQuery(
            DatabaseQuery&&) noexcept = default;

        DatabaseQuery&
            operator=(
                DatabaseQuery&& other)
            noexcept
        {
            if (this == &other) {
                return *this;
            }

            // Destroy the current Statement while
            // its current DB lock is still held.
            statement_.reset();

            // Transfer ownership of the lock.
            //
            // If this object currently owns another lock,
            // unique_lock will release it here only AFTER
            // its Statement was destroyed above.
            lock_ =
                std::move(
                    other.lock_
                );

            // The source Statement remains protected:
            // its mutex ownership has just moved into *this.
            statement_ =
                std::move(
                    other.statement_
                );

            return *this;
        }

        SQLite::Statement* operator->() noexcept
        {
            return statement_.get();
        }

        const SQLite::Statement* operator->() const noexcept
        {
            return statement_.get();
        }

        SQLite::Statement& operator*() noexcept
        {
            return *statement_;
        }

        const SQLite::Statement& operator*() const noexcept
        {
            return *statement_;
        }

        explicit operator bool() const noexcept
        {
            return statement_ != nullptr;
        }


        bool operator!() const noexcept
        {
            return statement_ == nullptr;
        }


    private:
        // IMPORTANT:
        //
        // lock_ is declared BEFORE statement_.
        //
        // Members are destroyed in reverse order,
        // therefore SQLite::Statement is destroyed
        // before the database mutex is released.
        std::unique_lock<std::recursive_mutex>
            lock_;

        std::unique_ptr<SQLite::Statement>
            statement_;
    };

    class Database {
    private:
        SQLite::Database db;

        // Serializes every operation on the single
        // SQLite connection shared by all repositories.
        //
        // recursive_mutex is intentional:
        // existing code may keep a DatabaseQuery alive
        // and call db.exec() from the same thread.
        mutable std::recursive_mutex db_mutex;

        std::atomic<int> writeCount{ 0 };

        void maybeCheckpoint(int count);

        void execDeleteByIdInChunk(const std::string& table, const std::string& idColumn, const std::vector<int>& ids);
        void execBatchSettingsReplaceChunk(const std::vector<std::pair<std::string, std::string>>& keyValues);
        void execBatchInsertIntPairsChunk(const std::string& table, const std::string& colA, const std::string& colB,
                                         const std::vector<int>& pairs);
        void execBatchInsertProfilesChunk(const std::vector<ProfileInsertRow>& rows);
        void execBatchReplaceProfilesChunk(const std::vector<ProfileInsertRow>& rows);
        // Writes one routing profile and all its rules.
        //
        // IMPORTANT:
        // - assumes db_mutex is already locked;
        // - assumes caller owns the SQLite transaction;
        // - does NOT catch SQLite exceptions;
        // - does NOT COMMIT;
        // - does NOT call maybeCheckpoint().
        //
        // Any SQLite exception must propagate to the transaction owner.
        void writeRouteProfile0(
            const RouteProfileSaveRow& row
        );
    public:
        
        Database(const std::string& path)
            : db(path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE) {
            db.exec("PRAGMA foreign_keys = ON");
            db.exec("PRAGMA journal_mode = WAL");
            db.exec("PRAGMA synchronous = NORMAL");
            db.exec("PRAGMA mmap_size = 67108864"); // 64MB
            checkpointWal();
        }

    private:

        // 1. Bind one argument; explicit overloads avoid ambiguity on Linux (int32_t/int64_t/uint32_t)
        template<typename T>
        std::enable_if_t<std::is_integral_v<std::decay_t<T>>> bindOne(SQLite::Statement& query, int index, T&& value) {
            query.bind(index, static_cast<int64_t>(value));
        }
        template<typename T>
        std::enable_if_t<std::is_floating_point_v<std::decay_t<T>>> bindOne(SQLite::Statement& query, int index, T&& value) {
            query.bind(index, static_cast<double>(value));
        }
        void bindOne(SQLite::Statement& query, int index, const std::string& value) {
            query.bind(index, value);
        }
        void bindOne(SQLite::Statement& query, int index, const char* value) {
            query.bind(index, value);
        }

        template<typename T, typename... Rest>
        void bindArgs(SQLite::Statement& query, int index, T&& first, Rest&&... rest) {
            bindOne(query, index, std::forward<T>(first));
            bindArgs(query, index + 1, std::forward<Rest>(rest)...);
        }

        void bindArgs(SQLite::Statement& query, int index) {
            // No more args to bind
        }

        // 2. The "PGX Style" Exec (No return value, e.g., UPDATE/INSERT)
        template<typename... Args>
        void exec0(
            const std::string& sql,
            Args&&... args)
        {
            std::lock_guard<std::recursive_mutex>
                locker(db_mutex);

            SQLite::Statement query(
                db,
                sql
            );

            bindArgs(
                query,
                1,
                std::forward<Args>(args)...
            );

            query.exec();
            maybeCheckpoint(1);
        }

        // Run WAL checkpoint manually (e.g. from a periodic timer). Safe to call from any thread.
        void checkpointWal();

        // 3. Helper for fetching a single row
        // Returns a Statement you can extract data from
        template<typename... Args>
        DatabaseQuery query0(
            const std::string& sql,
            Args&&... args)
        {
            // DatabaseQuery acquires db_mutex here
            // and keeps it locked for its whole lifetime.
            DatabaseQuery query(
                db_mutex,
                db,
                sql
            );

            bindArgs(
                *query,
                1,
                std::forward<Args>(args)...
            );

            return query;
        }

        // 4. Execute DELETE FROM table WHERE idColumn IN (ids), chunked by BATCH_LIMIT
        void execDeleteByIdIn0(const std::string& table, const std::string& idColumn, const std::vector<int>& ids) {
            std::lock_guard<std::recursive_mutex>
                locker(db_mutex);

            for (size_t off = 0; off < ids.size(); off += BATCH_LIMIT_WRITE) {
                size_t end = std::min(off + BATCH_LIMIT_WRITE, ids.size());
                std::vector<int> chunk(ids.begin() + static_cast<std::ptrdiff_t>(off), ids.begin() + static_cast<std::ptrdiff_t>(end));
                execDeleteByIdInChunk(table, idColumn, chunk);
            }
        }

        // 5. Execute INSERT OR REPLACE INTO settings (key, value) VALUES ..., chunked (2 params per row -> BATCH_LIMIT/2 rows per chunk)
        void execBatchSettingsReplace0(const std::vector<std::pair<std::string, std::string>>& keyValues) {
            std::lock_guard<std::recursive_mutex>
                locker(db_mutex);
            const size_t chunkSize = BATCH_LIMIT_WRITE / 2;
            for (size_t off = 0; off < keyValues.size(); off += chunkSize) {
                size_t end = std::min(off + chunkSize, keyValues.size());
                std::vector<std::pair<std::string, std::string>> chunk(keyValues.begin() + static_cast<std::ptrdiff_t>(off),
                                                                       keyValues.begin() + static_cast<std::ptrdiff_t>(end));
                execBatchSettingsReplaceChunk(chunk);
            }
        }

        // 6. Execute INSERT INTO table (colA, colB) VALUES ..., chunked (2 params per pair -> BATCH_LIMIT/2 pairs per chunk)
        void execBatchInsertIntPairs0(const std::string& table, const std::string& colA, const std::string& colB,
                                     const std::vector<int>& pairs) {
            std::lock_guard<std::recursive_mutex>
                locker(db_mutex);
            if (pairs.size() < 2 || pairs.size() % 2 != 0) return;
            const size_t chunkPairs = BATCH_LIMIT_WRITE / 2;
            for (size_t off = 0; off < pairs.size() / 2; off += chunkPairs) {
                size_t pairCount = std::min(chunkPairs, pairs.size() / 2 - off);
                std::vector<int> chunk;
                chunk.reserve(pairCount * 2);
                for (size_t i = 0; i < pairCount; ++i) {
                    size_t idx = (off + i) * 2;
                    chunk.push_back(pairs[idx]);
                    chunk.push_back(pairs[idx + 1]);
                }
                execBatchInsertIntPairsChunk(table, colA, colB, chunk);
            }
        }

        // Chunked (12 params per row -> BATCH_LIMIT/12 rows per chunk)
        void execBatchInsertProfiles0(
            const std::vector<ProfileInsertRow>& rows)
        {
            std::lock_guard<std::recursive_mutex>
                locker(db_mutex);

            const size_t chunkSize =
                BATCH_LIMIT_WRITE / 12;

            for (
                size_t off = 0;
                off < rows.size();
                off += chunkSize)
            {
                const size_t end =
                    std::min(
                        off + chunkSize,
                        rows.size()
                    );

                std::vector<ProfileInsertRow>
                    chunk(
                        rows.begin()
                        + static_cast<std::ptrdiff_t>(off),

                        rows.begin()
                        + static_cast<std::ptrdiff_t>(end)
                    );

                execBatchInsertProfilesChunk(
                    chunk
                );
            }
        }

        // Same chunking as execBatchInsertProfiles; INSERT OR REPLACE for batch save/update
        void execBatchReplaceProfiles0(const std::vector<ProfileInsertRow>& rows) {
            std::lock_guard<std::recursive_mutex>
                locker(db_mutex);
            const size_t chunkSize = BATCH_LIMIT_WRITE / 12;
            for (size_t off = 0; off < rows.size(); off += chunkSize) {
                size_t end = std::min(off + chunkSize, rows.size());
                std::vector<ProfileInsertRow> chunk(rows.begin() + static_cast<std::ptrdiff_t>(off),
                                                    rows.begin() + static_cast<std::ptrdiff_t>(end));
                execBatchReplaceProfilesChunk(chunk);
            }
        }

    public:
        template<typename... Args>
        [[nodiscard]]
        bool exec(
            const std::string& sql,
            Args&&... args)
        {
            try
            {
                exec0(
                    sql,
                    std::forward<Args>(args)...
                );

                return true;
            }
            catch (std::exception& e)
            {
                NotifyError(
                    sql,
                    e
                );

                return false;
            }
        }


        template<typename... Args>
        DatabaseQuery query(
            const std::string& sql,
            Args&&... args)
        {
            try
            {
                return query0(
                    sql,
                    std::forward<Args>(args)...
                );
            }
            catch (std::exception& e)
            {
                NotifyError(
                    sql,
                    e
                );

                return {};
            }
        }

        [[nodiscard]]
        bool execDeleteByIdIn(
            const std::string& table,
            const std::string& idColumn,
            const std::vector<int>& ids
        );
        
        [[nodiscard]]
        bool execBatchSettingsReplace(
            const std::vector<
            std::pair<
            std::string,
            std::string
            >
            >& keyValues
        );

        [[nodiscard]]
        bool execBatchInsertIntPairs(
            const std::string& table,
            const std::string& colA,
            const std::string& colB,
            const std::vector<int>& pairs
        );

        [[nodiscard]]
        bool insertGroupAtomic(
            const GroupInsertRow& row
        );

        [[nodiscard]]
        bool saveRouteProfileAtomic(
            const RouteProfileSaveRow& row
        );

        [[nodiscard]]
        bool replaceRouteProfilesAtomic(
            const std::vector<RouteProfileSaveRow>& rows
        );

        [[nodiscard]]
        bool execBatchReplaceProfiles(
            const std::vector<ProfileInsertRow>& rows
        );

        [[nodiscard]]
        bool execBatchUpdateProfileTraffic(
            const std::vector<ProfileTrafficRow>& rows
        );

        [[nodiscard]]
        bool execBatchInsertProfiles(
            const std::vector<ProfileInsertRow>& rows
        );

        [[nodiscard]]
        bool deleteProfilesAtomic(
            const std::vector<int>& ids
        );

        [[nodiscard]]
        bool deleteProfilesWithGroupUpdatesAtomic(
            const std::vector<GroupProfilesUpdateRow>& groupUpdates,
            const std::vector<int>& profileIds
        );

        void backupTo(
            const std::string& destPath
        );


        void restoreFrom(
            const std::string& srcPath
        );


        void backupSelective(
            const std::string& destPath,
            const BackupParts& parts
        );


        void restoreSelective(
            const std::string& srcPath,
            const BackupParts& parts
        );
    }; // class Database
} // namespace Configs