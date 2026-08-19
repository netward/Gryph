#include "include/database/Database.h"
#include <3rdparty/SQLiteCpp/include/Backup.h>

#include <set>
#include <stdexcept>

namespace Configs {
    void Database::maybeCheckpoint(
        int count)
    {
        const int newCount =
            writeCount.fetch_add(
                count,
                std::memory_order_relaxed
            )
            + count;

        if (newCount >=
            WAL_CHECKPOINT_AFTER_WRITES)
        {
            writeCount.store(
                0,
                std::memory_order_relaxed
            );

            checkpointWal();
        }
    }

    void Database::checkpointWal() {
        std::lock_guard<std::recursive_mutex>
            locker(db_mutex);
        try {
            db.exec("PRAGMA wal_checkpoint(TRUNCATE)");
        } catch (std::exception& e) {
            std::cerr << "DB WAL checkpoint error: " << e.what() << std::endl;
        }
    }

    void Database::execDeleteByIdInChunk(
        const std::string& table,
        const std::string& idColumn,
        const std::vector<int>& ids)
    {
        if (ids.empty())
        {
            return;
        }


        std::string sql =
            "DELETE FROM "
            + table
            + " WHERE "
            + idColumn
            + " IN (";


        for (size_t i = 0;
            i < ids.size();
            ++i)
        {
            if (i > 0)
            {
                sql += ",";
            }

            sql +=
                std::to_string(
                    ids[i]
                );
        }


        sql += ")";


        // Do NOT catch here.
        // Owner transaction must see the exception.
        db.exec(
            sql
        );
    }

    void Database::execBatchSettingsReplaceChunk(
        const std::vector<
        std::pair<
        std::string,
        std::string
        >
        >& keyValues)
    {
        if (keyValues.empty())
        {
            return;
        }


        std::string sql =
            "INSERT OR REPLACE INTO settings "
            "(key, value) VALUES ";


        for (size_t i = 0;
            i < keyValues.size();
            ++i)
        {
            if (i > 0)
            {
                sql += ",";
            }

            sql += "(?,?)";
        }


        SQLite::Statement stmt(
            db,
            sql
        );


        for (size_t i = 0;
            i < keyValues.size();
            ++i)
        {
            stmt.bind(
                static_cast<int>(
                    2 * i + 1
                    ),
                keyValues[i].first
            );

            stmt.bind(
                static_cast<int>(
                    2 * i + 2
                    ),
                keyValues[i].second
            );
        }


        // Do NOT catch here.
        stmt.exec();
    }

    void Database::execBatchInsertIntPairsChunk(
        const std::string& table,
        const std::string& colA,
        const std::string& colB,
        const std::vector<int>& pairs)
    {
        if (pairs.size() < 2 ||
            pairs.size() % 2 != 0)
        {
            return;
        }


        std::string sql =
            "INSERT INTO "
            + table
            + " ("
            + colA
            + ","
            + colB
            + ") VALUES ";


        const size_t count =
            pairs.size() / 2;


        for (size_t i = 0;
            i < count;
            ++i)
        {
            if (i > 0)
            {
                sql += ",";
            }

            sql += "(?,?)";
        }


        SQLite::Statement stmt(
            db,
            sql
        );


        for (size_t i = 0;
            i < pairs.size();
            ++i)
        {
            stmt.bind(
                static_cast<int>(
                    i + 1
                    ),
                pairs[i]
            );
        }


        stmt.exec();
    }

    bool Database::insertGroupAtomic(
        const GroupInsertRow& row)
    {
        // =========================================================
        // Validation
        // =========================================================

        if (row.id < 0)
        {
            return false;
        }


        // =========================================================
        // One SQLite connection -> serialize the whole transaction.
        // =========================================================

        std::lock_guard<
            std::recursive_mutex
        > locker(
            db_mutex
        );


        try
        {
            // =====================================================
            // ONE transaction:
            //
            //      groups
            //          +
            //      groups_order
            //
            // Either both rows appear or neither appears.
            // =====================================================

            SQLite::Transaction transaction(
                db,
                SQLite::TransactionBehavior::IMMEDIATE
            );


            // =====================================================
            // Insert Group
            // =====================================================

            SQLite::Statement groupStmt(
                db,

                R"(
                INSERT INTO groups
                (
                    id,
                    archive,
                    skip_auto_update,
                    auto_clear_unavailable,
                    name,
                    url,
                    info,
                    sub_last_update,
                    front_proxy_id,
                    landing_proxy_id,
                    column_width_json,
                    profiles_json,
                    default_profile_order_json,
                    scroll_last_profile,
                    test_sort_by,
                    traffic_sort_by,
                    test_items_to_show
                )
                VALUES
                (
                    ?, ?, ?, ?, ?, ?, ?, ?,
                    ?, ?, ?, ?, ?, ?, ?, ?, ?
                )
                )"
            );


            int index =
                1;


            groupStmt.bind(
                index++,
                row.id
            );

            groupStmt.bind(
                index++,
                row.archive
                ? 1
                : 0
            );

            groupStmt.bind(
                index++,
                row.skip_auto_update
                ? 1
                : 0
            );

            groupStmt.bind(
                index++,
                row.auto_clear_unavailable
                ? 1
                : 0
            );

            groupStmt.bind(
                index++,
                row.name
            );

            groupStmt.bind(
                index++,
                row.url
            );

            groupStmt.bind(
                index++,
                row.info
            );

            groupStmt.bind(
                index++,
                static_cast<int64_t>(
                    row.sub_last_update
                    )
            );

            groupStmt.bind(
                index++,
                row.front_proxy_id
            );

            groupStmt.bind(
                index++,
                row.landing_proxy_id
            );

            groupStmt.bind(
                index++,
                row.column_width_json
            );

            groupStmt.bind(
                index++,
                row.profiles_json
            );

            groupStmt.bind(
                index++,
                row.default_profile_order_json
            );

            groupStmt.bind(
                index++,
                row.scroll_last_profile
            );

            groupStmt.bind(
                index++,
                row.test_sort_by
            );

            groupStmt.bind(
                index++,
                row.traffic_sort_by
            );

            groupStmt.bind(
                index++,
                row.test_items_to_show
            );


            groupStmt.exec();


            // =====================================================
            // Insert tab order.
            //
            // Do not do:
            //
            //      SELECT MAX(...)
            //      INSERT ...
            //
            // as two repository operations.
            //
            // Calculate and insert the order in one SQL statement.
            // =====================================================

            SQLite::Statement orderStmt(
                db,

                R"(
            INSERT INTO groups_order
            (
                group_id,
                display_order
            )
            SELECT
                ?,
                COALESCE(
                    MAX(display_order),
                    -1
                ) + 1
            FROM groups_order
            )"
            );


            orderStmt.bind(
                1,
                row.id
            );


            orderStmt.exec();


            // =====================================================
            // Commit both rows together.
            // =====================================================

            transaction.commit();


            // Count only successfully committed writes.
            maybeCheckpoint(
                2
            );


            return true;
        }
        catch (std::exception& e)
        {
            // SQLite::Transaction rolls back automatically when its
            // destructor runs without commit().

            NotifyError(
                "Database::insertGroupAtomic",
                e
            );


            return false;
        }
    }

    void Database::writeRouteProfile0(
        const RouteProfileSaveRow& row)
    {
        // =========================================================
        // PRECONDITIONS
        //
        // Caller:
        //
        //   1. owns db_mutex;
        //   2. owns active SQLite transaction.
        //
        // This method MUST NOT:
        //
        //   - catch SQLite exceptions;
        //   - commit;
        //   - rollback;
        //   - start another transaction.
        //
        // Exceptions intentionally propagate to transaction owner.
        // =========================================================


        if (row.id < 0)
        {
            throw std::invalid_argument(
                "Database::writeRouteProfile0: "
                "invalid RouteProfile ID"
            );
        }


        // =========================================================
        // UPSERT route profile
        // =========================================================

        SQLite::Statement profileStmt(
            db,

            R"(
        INSERT INTO route_profiles
        (
            id,
            name,
            default_outbound_id
        )
        VALUES
        (
            ?,
            ?,
            ?
        )

        ON CONFLICT(id)
        DO UPDATE SET

            name =
                excluded.name,

            default_outbound_id =
                excluded.default_outbound_id,

            updated_at =
                strftime('%s', 'now')
        )"
        );


        profileStmt.bind(
            1,
            row.id
        );


        profileStmt.bind(
            2,
            row.name
        );


        profileStmt.bind(
            3,
            row.default_outbound_id
        );


        profileStmt.exec();


        // =========================================================
        // Delete old rules.
        //
        // This is safe because the DELETE belongs to the caller's
        // transaction. If a later INSERT fails, DELETE is rolled
        // back together with everything else.
        // =========================================================

        SQLite::Statement deleteRulesStmt(
            db,

            "DELETE FROM route_rules "
            "WHERE route_profile_id = ?"
        );


        deleteRulesStmt.bind(
            1,
            row.id
        );


        deleteRulesStmt.exec();


        // =========================================================
        // Prepared RouteRule INSERT
        // =========================================================

        SQLite::Statement ruleStmt(
            db,

            R"(
        INSERT INTO route_rules
        (
            route_profile_id,
            rule_order,

            name,
            type,

            ip_version,
            network,
            protocol,

            inbound_json,
            domain_json,
            domain_suffix_json,
            domain_keyword_json,
            domain_regex_json,

            source_ip_cidr_json,
            source_ip_is_private,

            ip_cidr_json,
            ip_is_private,

            source_port_json,
            source_port_range_json,

            port_json,
            port_range_json,

            process_name_json,
            process_path_json,
            process_path_regex_json,

            rule_set_json,

            invert,

            outbound_id,

            action,
            reject_method,

            no_drop,

            override_address,
            override_port,

            sniffers_json,

            sniff_override_dest,

            strategy,

            wifi_ssid_json,
            wifi_bssid_json
        )

        VALUES
        (
            ?, ?,

            ?, ?,

            ?, ?, ?,

            ?, ?, ?, ?, ?,

            ?, ?,

            ?, ?,

            ?, ?,

            ?, ?,

            ?, ?, ?,

            ?,

            ?,

            ?,

            ?, ?,

            ?,

            ?, ?,

            ?,

            ?,

            ?,

            ?, ?
        )
        )"
        );


        int ruleOrder =
            0;


        for (const auto& rule :
            row.rules)
        {
            int index =
                1;


            // =====================================================
            // Route profile identity
            // =====================================================

            ruleStmt.bind(
                index++,
                row.id
            );


            ruleStmt.bind(
                index++,
                ruleOrder++
            );


            // =====================================================
            // Basic
            // =====================================================

            ruleStmt.bind(
                index++,
                rule.name
            );


            ruleStmt.bind(
                index++,
                rule.type
            );


            ruleStmt.bind(
                index++,
                rule.ip_version
            );


            ruleStmt.bind(
                index++,
                rule.network
            );


            ruleStmt.bind(
                index++,
                rule.protocol
            );


            // =====================================================
            // Match lists
            // =====================================================

            ruleStmt.bind(
                index++,
                rule.inbound_json
            );


            ruleStmt.bind(
                index++,
                rule.domain_json
            );


            ruleStmt.bind(
                index++,
                rule.domain_suffix_json
            );


            ruleStmt.bind(
                index++,
                rule.domain_keyword_json
            );


            ruleStmt.bind(
                index++,
                rule.domain_regex_json
            );


            ruleStmt.bind(
                index++,
                rule.source_ip_cidr_json
            );


            ruleStmt.bind(
                index++,
                rule.source_ip_is_private
                ? 1
                : 0
            );


            ruleStmt.bind(
                index++,
                rule.ip_cidr_json
            );


            ruleStmt.bind(
                index++,
                rule.ip_is_private
                ? 1
                : 0
            );


            ruleStmt.bind(
                index++,
                rule.source_port_json
            );


            ruleStmt.bind(
                index++,
                rule.source_port_range_json
            );


            ruleStmt.bind(
                index++,
                rule.port_json
            );


            ruleStmt.bind(
                index++,
                rule.port_range_json
            );


            ruleStmt.bind(
                index++,
                rule.process_name_json
            );


            ruleStmt.bind(
                index++,
                rule.process_path_json
            );


            ruleStmt.bind(
                index++,
                rule.process_path_regex_json
            );


            ruleStmt.bind(
                index++,
                rule.rule_set_json
            );


            // =====================================================
            // Behaviour
            // =====================================================

            ruleStmt.bind(
                index++,
                rule.invert
                ? 1
                : 0
            );


            ruleStmt.bind(
                index++,
                rule.outbound_id
            );


            ruleStmt.bind(
                index++,
                rule.action
            );


            ruleStmt.bind(
                index++,
                rule.reject_method
            );


            ruleStmt.bind(
                index++,
                rule.no_drop
                ? 1
                : 0
            );


            // =====================================================
            // Route options
            // =====================================================

            ruleStmt.bind(
                index++,
                rule.override_address
            );


            ruleStmt.bind(
                index++,
                rule.override_port
            );


            // =====================================================
            // Sniff
            // =====================================================

            ruleStmt.bind(
                index++,
                rule.sniffers_json
            );


            ruleStmt.bind(
                index++,
                rule.sniff_override_dest
                ? 1
                : 0
            );


            // =====================================================
            // Resolve
            // =====================================================

            ruleStmt.bind(
                index++,
                rule.strategy
            );


            // =====================================================
            // Wi-Fi
            // =====================================================

            ruleStmt.bind(
                index++,
                rule.wifi_ssid_json
            );


            ruleStmt.bind(
                index++,
                rule.wifi_bssid_json
            );


            // =====================================================
            // Execute rule
            // =====================================================

            ruleStmt.exec();


            ruleStmt.reset();
        }
    }

    bool Database::saveRouteProfileAtomic(
        const RouteProfileSaveRow& row)
    {
        if (row.id < 0)
        {
            return false;
        }


        std::lock_guard<
            std::recursive_mutex
        > locker(
            db_mutex
        );


        try
        {
            SQLite::Transaction transaction(
                db,
                SQLite::TransactionBehavior::IMMEDIATE
            );


            // Perform complete profile write inside this transaction.
            writeRouteProfile0(
                row
            );


            transaction.commit();


            // 1 UPSERT profile
            // 1 DELETE old rules
            // N INSERT rules
            maybeCheckpoint(
                static_cast<int>(
                    2
                    +
                    row.rules.size()
                    )
            );


            return true;
        }
        catch (std::exception& e)
        {
            // SQLite::Transaction automatically rolls back because
            // commit() was not reached.

            NotifyError(
                "Database::saveRouteProfileAtomic",
                e
            );


            return false;
        }
    }

    bool Database::replaceRouteProfilesAtomic(
        const std::vector<RouteProfileSaveRow>& rows)
    {
        // =========================================================
        // Strict validation BEFORE entering SQLite transaction.
        // =========================================================

        std::set<int>
            suppliedIds;


        for (const auto& row :
            rows)
        {
            if (row.id < 0)
            {
                MW_show_log(
                    "Database::replaceRouteProfilesAtomic: "
                    "invalid RouteProfile ID"
                );

                return false;
            }


            const auto [
                it,
                inserted
            ] =
                suppliedIds.insert(
                    row.id
                );


            if (!inserted)
            {
                MW_show_log(
                    "Database::replaceRouteProfilesAtomic: "
                    "duplicate RouteProfile ID "
                    +
                    Int2String(
                        row.id
                    )
                );

                return false;
            }
        }


        // =========================================================
        // Serialize ALL database access for the complete operation.
        // =========================================================

        std::lock_guard<
            std::recursive_mutex
        > locker(
            db_mutex
        );


        try
        {
            // =====================================================
            // ONE transaction for the ENTIRE replacement.
            // =====================================================

            SQLite::Transaction transaction(
                db,
                SQLite::TransactionBehavior::IMMEDIATE
            );


            int committedWrites =
                0;


            // =====================================================
            // UPSERT every supplied RouteProfile and replace all of
            // its rules.
            //
            // writeRouteProfile0() does NOT create nested
            // transactions.
            // =====================================================

            for (const auto& row :
                rows)
            {
                writeRouteProfile0(
                    row
                );


                committedWrites +=
                    static_cast<int>(
                        2
                        +
                        row.rules.size()
                        );
            }


            // =====================================================
            // Delete all profiles not present in the supplied list.
            //
            // route_rules disappear automatically because:
            //
            // FOREIGN KEY(route_profile_id)
            //     REFERENCES route_profiles(id)
            //     ON DELETE CASCADE
            //
            // IMPORTANT:
            //
            // IDs here are integers already validated by our own
            // application. No user-controlled SQL text is used.
            // =====================================================

            if (suppliedIds.empty())
            {
                db.exec(
                    "DELETE FROM route_profiles"
                );
            }
            else
            {
                std::string sql =
                    "DELETE FROM route_profiles "
                    "WHERE id NOT IN (";


                bool first =
                    true;


                for (const int id :
                suppliedIds)
                {
                    if (!first)
                    {
                        sql += ",";
                    }


                    sql +=
                        std::to_string(
                            id
                        );


                    first =
                        false;
                }


                sql += ")";


                db.exec(
                    sql
                );
            }


            ++committedWrites;


            // =====================================================
            // ONLY NOW does the new complete routing set become
            // visible.
            // =====================================================

            transaction.commit();


            if (committedWrites > 0)
            {
                maybeCheckpoint(
                    committedWrites
                );
            }


            return true;
        }
        catch (std::exception& e)
        {
            // =====================================================
            // ANY failure above means no commit().
            //
            // SQLite::Transaction rolls EVERYTHING back:
            //
            // profile 1
            // profile 2
            // ...
            // rules
            // obsolete-profile deletes
            //
            // =====================================================

            NotifyError(
                "Database::replaceRouteProfilesAtomic",
                e
            );


            return false;
        }
    }

    void Database::execBatchInsertProfilesChunk(
        const std::vector<ProfileInsertRow>& rows)
    {
        if (rows.empty())
        {
            return;
        }


        const size_t count =
            rows.size();


        std::string sql =
            "INSERT INTO profiles "
            "("
            "id, "
            "type, "
            "name, "
            "gid, "
            "latency, "
            "dl_speed, "
            "ul_speed, "
            "test_country, "
            "ip_out, "
            "outbound_json, "
            "traffic_dl, "
            "traffic_up"
            ") "
            "VALUES ";


        for (size_t i = 0;
            i < count;
            ++i)
        {
            if (i > 0)
            {
                sql += ",";
            }


            sql +=
                "(?,?,?,?,?,?,?,?,?,?,?,?)";
        }


        SQLite::Statement stmt(
            db,
            sql
        );


        int index = 1;


        for (const auto& row :
            rows)
        {
            stmt.bind(
                index++,
                row.id
            );

            stmt.bind(
                index++,
                row.type
            );

            stmt.bind(
                index++,
                row.name
            );

            stmt.bind(
                index++,
                row.gid
            );

            stmt.bind(
                index++,
                row.latency
            );

            stmt.bind(
                index++,
                row.dl_speed
            );

            stmt.bind(
                index++,
                row.ul_speed
            );

            stmt.bind(
                index++,
                row.test_country
            );

            stmt.bind(
                index++,
                row.ip_out
            );

            stmt.bind(
                index++,
                row.outbound_json
            );

            stmt.bind(
                index++,
                static_cast<int64_t>(
                    row.traffic_dl
                    )
            );

            stmt.bind(
                index++,
                static_cast<int64_t>(
                    row.traffic_up
                    )
            );
        }


        // IMPORTANT:
        //
        // Do not catch SQLite exception here.
        // The owner transaction must see it.
        stmt.exec();
    }

    bool Database::execBatchInsertProfiles(
        const std::vector<ProfileInsertRow>& rows)
    {
        if (rows.empty())
        {
            return true;
        }


        std::lock_guard<
            std::recursive_mutex
        > locker(
            db_mutex
        );


        bool transactionStarted =
            false;


        try
        {
            // =====================================================
            // One SQLite transaction for the WHOLE batch.
            // =====================================================

            db.exec(
                "BEGIN IMMEDIATE"
            );


            transactionStarted =
                true;


            // execBatchInsertProfiles0() may split the batch
            // into multiple SQL statements, but all of them
            // are now inside one SQLite transaction.
            execBatchInsertProfiles0(
                rows
            );


            db.exec(
                "COMMIT"
            );


            transactionStarted =
                false;


            // Count committed writes only after COMMIT.
            maybeCheckpoint(
                static_cast<int>(
                    rows.size()
                    )
            );


            return true;
        }
        catch (std::exception& e)
        {
            if (transactionStarted)
            {
                try
                {
                    db.exec(
                        "ROLLBACK"
                    );
                }
                catch (...)
                {
                    // Preserve the original exception.
                }
            }


            NotifyError(
                "execBatchInsertProfiles",
                e
            );


            return false;
        }
    }

    bool Database::execDeleteByIdIn(
        const std::string& table,
        const std::string& idColumn,
        const std::vector<int>& ids)
    {
        if (ids.empty())
        {
            return true;
        }


        std::lock_guard<
            std::recursive_mutex
        > locker(
            db_mutex
        );


        bool transactionStarted =
            false;


        try
        {
            db.exec(
                "BEGIN IMMEDIATE"
            );

            transactionStarted =
                true;


            execDeleteByIdIn0(
                table,
                idColumn,
                ids
            );


            db.exec(
                "COMMIT"
            );

            transactionStarted =
                false;


            maybeCheckpoint(
                static_cast<int>(
                    ids.size()
                    )
            );


            return true;
        }
        catch (std::exception& e)
        {
            if (transactionStarted)
            {
                try
                {
                    db.exec(
                        "ROLLBACK"
                    );
                }
                catch (...)
                {
                }
            }


            NotifyError(
                "execDeleteByIdIn for "
                + table,
                e
            );


            return false;
        }
    }

    bool Database::execBatchSettingsReplace(
        const std::vector<
        std::pair<
        std::string,
        std::string
        >
        >& keyValues)
    {
        if (keyValues.empty())
        {
            return true;
        }


        std::lock_guard<
            std::recursive_mutex
        > locker(
            db_mutex
        );


        bool transactionStarted =
            false;


        try
        {
            db.exec(
                "BEGIN IMMEDIATE"
            );

            transactionStarted =
                true;


            execBatchSettingsReplace0(
                keyValues
            );


            db.exec(
                "COMMIT"
            );

            transactionStarted =
                false;


            maybeCheckpoint(
                static_cast<int>(
                    keyValues.size()
                    )
            );


            return true;
        }
        catch (std::exception& e)
        {
            if (transactionStarted)
            {
                try
                {
                    db.exec(
                        "ROLLBACK"
                    );
                }
                catch (...)
                {
                }
            }


            NotifyError(
                "execBatchSettingsReplace",
                e
            );


            return false;
        }
    }

    bool Database::execBatchInsertIntPairs(
        const std::string& table,
        const std::string& colA,
        const std::string& colB,
        const std::vector<int>& pairs)
    {
        if (pairs.empty())
        {
            return true;
        }


        if (pairs.size() < 2 ||
            pairs.size() % 2 != 0)
        {
            MW_show_log(
                "Database::execBatchInsertIntPairs: "
                "invalid pair vector"
            );

            return false;
        }


        std::lock_guard<
            std::recursive_mutex
        > locker(
            db_mutex
        );


        bool transactionStarted =
            false;


        try
        {
            db.exec(
                "BEGIN IMMEDIATE"
            );

            transactionStarted =
                true;


            execBatchInsertIntPairs0(
                table,
                colA,
                colB,
                pairs
            );


            db.exec(
                "COMMIT"
            );

            transactionStarted =
                false;


            maybeCheckpoint(
                static_cast<int>(
                    pairs.size() / 2
                    )
            );


            return true;
        }
        catch (std::exception& e)
        {
            if (transactionStarted)
            {
                try
                {
                    db.exec(
                        "ROLLBACK"
                    );
                }
                catch (...)
                {
                }
            }


            NotifyError(
                "execBatchInsertIntPairs for "
                + table,
                e
            );


            return false;
        }
    }

    bool Database::execBatchReplaceProfiles(
        const std::vector<ProfileInsertRow>& rows)
    {
        if (rows.empty())
        {
            return true;
        }


        std::lock_guard<
            std::recursive_mutex
        > locker(
            db_mutex
        );


        bool transactionStarted =
            false;


        try
        {
            db.exec(
                "BEGIN IMMEDIATE"
            );

            transactionStarted =
                true;


            execBatchReplaceProfiles0(
                rows
            );


            db.exec(
                "COMMIT"
            );

            transactionStarted =
                false;


            maybeCheckpoint(
                static_cast<int>(
                    rows.size()
                    )
            );


            return true;
        }
        catch (std::exception& e)
        {
            if (transactionStarted)
            {
                try
                {
                    db.exec(
                        "ROLLBACK"
                    );
                }
                catch (...)
                {
                }
            }


            NotifyError(
                "execBatchReplaceProfiles",
                e
            );


            return false;
        }
    }

    bool Database::deleteProfilesAtomic(
        const std::vector<int>& ids)
    {
        if (ids.empty())
        {
            return true;
        }


        std::lock_guard<
            std::recursive_mutex
        > locker(
            db_mutex
        );


        bool transactionStarted =
            false;


        try
        {
            db.exec(
                "BEGIN IMMEDIATE"
            );


            transactionStarted =
                true;


            constexpr size_t chunkSize =
                500;


            for (size_t offset = 0;
                offset < ids.size();
                offset += chunkSize)
            {
                const size_t end =
                    std::min(
                        offset + chunkSize,
                        ids.size()
                    );


                std::string sql =
                    "DELETE FROM profiles "
                    "WHERE id IN (";


                for (size_t i = offset;
                    i < end;
                    ++i)
                {
                    if (i > offset)
                    {
                        sql += ",";
                    }


                    sql +=
                        std::to_string(
                            ids[i]
                        );
                }


                sql += ")";


                db.exec(
                    sql
                );
            }


            db.exec(
                "COMMIT"
            );


            transactionStarted =
                false;


            maybeCheckpoint(
                static_cast<int>(
                    ids.size()
                    )
            );


            return true;
        }
        catch (std::exception& e)
        {
            if (transactionStarted)
            {
                try
                {
                    db.exec(
                        "ROLLBACK"
                    );
                }
                catch (...)
                {
                }
            }


            NotifyError(
                "deleteProfilesAtomic",
                e
            );


            return false;
        }
    }

    bool Database::replaceGroupsOrderAtomic(
        const std::vector<int>& groupIds)
    {
        // =========================================================
        // PHASE 1
        //
        // Validate input before touching SQLite.
        // =========================================================

        std::set<int>
            uniqueIds;


        for (const int groupId :
        groupIds)
        {
            if (groupId < 0)
            {
                MW_show_log(
                    "Database::replaceGroupsOrderAtomic: "
                    "invalid Group ID "
                    +
                    Int2String(groupId)
                );

                return false;
            }


            const auto [
                iterator,
                inserted
            ] =
                uniqueIds.insert(
                    groupId
                );


            if (!inserted)
            {
                MW_show_log(
                    "Database::replaceGroupsOrderAtomic: "
                    "duplicate Group ID "
                    +
                    Int2String(groupId)
                );

                return false;
            }
        }


        // =========================================================
        // PHASE 2
        //
        // Own the single SQLite connection for the complete
        // operation.
        // =========================================================

        std::lock_guard<
            std::recursive_mutex
        > locker(
            db_mutex
        );


        try
        {
            // =====================================================
            // ONE transaction for the WHOLE replacement.
            // =====================================================

            SQLite::Transaction transaction(
                db,
                SQLite::TransactionBehavior::IMMEDIATE
            );


            // =====================================================
            // Validate that every supplied Group still exists.
            //
            // This prevents groups_order from containing references
            // to Groups which no longer exist.
            // =====================================================

            if (!groupIds.empty())
            {
                SQLite::Statement existsStmt(
                    db,

                    "SELECT 1 "
                    "FROM groups "
                    "WHERE id = ? "
                    "LIMIT 1"
                );


                for (const int groupId :
                groupIds)
                {
                    existsStmt.bind(
                        1,
                        groupId
                    );


                    const bool exists =
                        existsStmt.executeStep();


                    if (!exists)
                    {
                        throw std::runtime_error(
                            "Database::replaceGroupsOrderAtomic: "
                            "Group does not exist: "
                            +
                            std::to_string(
                                groupId
                            )
                        );
                    }


                    // Prepare the statement for the next ID.
                    existsStmt.reset();
                }
            }


            // =====================================================
            // Remove previous order.
            //
            // IMPORTANT:
            //
            // This DELETE is NOT committed yet.
            //
            // If any INSERT below fails, SQLite rolls this DELETE
            // back as well.
            // =====================================================

            SQLite::Statement deleteStmt(
                db,

                "DELETE FROM groups_order"
            );


            deleteStmt.exec();


            // =====================================================
            // Empty order is valid.
            //
            // It simply means groups_order remains empty after
            // COMMIT.
            // =====================================================

            if (!groupIds.empty())
            {
                // =================================================
                // Prepare INSERT once and reuse it.
                // =================================================

                SQLite::Statement insertStmt(
                    db,

                    R"(
                INSERT INTO groups_order
                (
                    group_id,
                    display_order
                )
                VALUES
                (
                    ?,
                    ?
                )
                )"
                );


                for (std::size_t i = 0;
                    i < groupIds.size();
                    ++i)
                {
                    insertStmt.bind(
                        1,
                        groupIds[i]
                    );


                    insertStmt.bind(
                        2,
                        static_cast<int>(
                            i
                            )
                    );


                    insertStmt.exec();


                    insertStmt.reset();
                }
            }


            // =====================================================
            // Every operation succeeded.
            //
            // Old order disappears and new order becomes visible
            // at the same instant.
            // =====================================================

            transaction.commit();


            // =====================================================
            // WAL accounting.
            //
            // One DELETE
            // +
            // N INSERTs
            // =====================================================

            maybeCheckpoint(
                static_cast<int>(
                    1
                    +
                    groupIds.size()
                    )
            );


            return true;
        }
        catch (std::exception& e)
        {
            // =====================================================
            // transaction.commit() was not reached.
            //
            // SQLite::Transaction destructor rolls the complete
            // operation back.
            //
            // Therefore the previous groups_order stays intact.
            // =====================================================

            NotifyError(
                "Database::replaceGroupsOrderAtomic",
                e
            );


            return false;
        }
    }

    bool Database::deleteProfilesWithGroupUpdatesAtomic(
        const std::vector<GroupProfilesUpdateRow>& groupUpdates,
        const std::vector<int>& profileIds)
    {
        // =========================================================
        // Nothing to delete.
        // =========================================================

        if (profileIds.empty())
        {
            return true;
        }


        // =========================================================
        // Validate input BEFORE opening transaction.
        // =========================================================

        std::set<int>
            uniqueGroupIds;


        for (const auto& update :
            groupUpdates)
        {
            if (update.id < 0)
            {
                MW_show_log(
                    "Database::deleteProfilesWithGroupUpdatesAtomic: "
                    "invalid Group ID"
                );

                return false;
            }


            const auto [
                iterator,
                inserted
            ] =
                uniqueGroupIds.insert(
                    update.id
                );


            if (!inserted)
            {
                MW_show_log(
                    "Database::deleteProfilesWithGroupUpdatesAtomic: "
                    "duplicate Group ID "
                    +
                    Int2String(
                        update.id
                    )
                );

                return false;
            }
        }


        // =========================================================
        // The complete cross-table operation owns the SQLite
        // connection until COMMIT / ROLLBACK.
        // =========================================================

        std::lock_guard<
            std::recursive_mutex
        > locker(
            db_mutex
        );


        try
        {
            SQLite::Transaction transaction(
                db,
                SQLite::TransactionBehavior::IMMEDIATE
            );


            // =====================================================
            // PHASE 1
            //
            // Update every affected Group.
            //
            // Only profiles_json is changed.
            //
            // Do NOT replace the complete Group row here because the
            // snapshot used by BatchDeleteProfiles may not contain
            // the latest unrelated settings.
            // =====================================================

            if (!groupUpdates.empty())
            {
                SQLite::Statement groupStmt(
                    db,

                    R"(
                    UPDATE groups
                    SET profiles_json = ?,
                        updated_at = strftime('%s', 'now')
                    WHERE id = ?
                    )"
                    );


                for (const auto& update :
                    groupUpdates)
                {
                    groupStmt.bind(
                        1,
                        update.profiles_json
                    );


                    groupStmt.bind(
                        2,
                        update.id
                    );


                    const int affectedRows =
                        groupStmt.exec();


                    if (affectedRows != 1)
                    {
                        throw std::runtime_error(
                            "Database::deleteProfilesWithGroupUpdatesAtomic: "
                            "Group row not found: "
                            +
                            std::to_string(
                                update.id
                            )
                        );
                    }


                    groupStmt.reset();
                }
            }


            // =====================================================
            // PHASE 2
            //
            // Delete all requested Profiles.
            //
            // execDeleteByIdInChunk() intentionally does NOT catch
            // SQLite exceptions, therefore any failure propagates
            // into this transaction and causes complete rollback.
            // =====================================================

            constexpr std::size_t chunkSize =
                500;


            for (std::size_t offset = 0;
                offset < profileIds.size();
                offset += chunkSize)
            {
                const std::size_t end =
                    std::min(
                        offset + chunkSize,
                        profileIds.size()
                    );


                std::vector<int>
                    chunk(
                        profileIds.begin()
                        +
                        static_cast<std::ptrdiff_t>(
                            offset
                            ),

                        profileIds.begin()
                        +
                        static_cast<std::ptrdiff_t>(
                            end
                            )
                    );


                execDeleteByIdInChunk(
                    "profiles",
                    "id",
                    chunk
                );
            }


            // =====================================================
            // Both:
            //
            //     groups.profiles_json
            //     profiles rows
            //
            // become visible together.
            // =====================================================

            transaction.commit();


            // Count only committed writes.
            maybeCheckpoint(
                static_cast<int>(
                    groupUpdates.size()
                    +
                    profileIds.size()
                    )
            );


            return true;
        }
        catch (std::exception& e)
        {
            // SQLite::Transaction automatically rolls back because
            // commit() was not reached.

            NotifyError(
                "Database::deleteProfilesWithGroupUpdatesAtomic",
                e
            );


            return false;
        }
    }

    void Database::execBatchReplaceProfilesChunk(
        const std::vector<ProfileInsertRow>& rows)
    {
        if (rows.empty())
        {
            return;
        }


        const size_t count =
            rows.size();


        std::string sql =
            "INSERT OR REPLACE INTO profiles "
            "("
            "id, "
            "type, "
            "name, "
            "gid, "
            "latency, "
            "dl_speed, "
            "ul_speed, "
            "test_country, "
            "ip_out, "
            "outbound_json, "
            "traffic_dl, "
            "traffic_up"
            ") VALUES ";


        for (size_t i = 0;
            i < count;
            ++i)
        {
            if (i > 0)
            {
                sql += ",";
            }

            sql +=
                "(?,?,?,?,?,?,?,?,?,?,?,?)";
        }


        SQLite::Statement stmt(
            db,
            sql
        );


        int index = 1;


        for (const auto& row :
            rows)
        {
            stmt.bind(
                index++,
                row.id
            );

            stmt.bind(
                index++,
                row.type
            );

            stmt.bind(
                index++,
                row.name
            );

            stmt.bind(
                index++,
                row.gid
            );

            stmt.bind(
                index++,
                row.latency
            );

            stmt.bind(
                index++,
                row.dl_speed
            );

            stmt.bind(
                index++,
                row.ul_speed
            );

            stmt.bind(
                index++,
                row.test_country
            );

            stmt.bind(
                index++,
                row.ip_out
            );

            stmt.bind(
                index++,
                row.outbound_json
            );

            stmt.bind(
                index++,
                static_cast<int64_t>(
                    row.traffic_dl
                    )
            );

            stmt.bind(
                index++,
                static_cast<int64_t>(
                    row.traffic_up
                    )
            );
        }


        stmt.exec();
    }

    void Database::backupTo(const std::string& destPath) {
        std::lock_guard<std::recursive_mutex>
            locker(db_mutex);
        SQLite::Database destDb(destPath, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        SQLite::Backup backup(destDb, db);
        backup.executeStep(-1);
    }

    void Database::restoreFrom(const std::string& srcPath) {
        std::lock_guard<std::recursive_mutex>
            locker(db_mutex);
        SQLite::Database srcDb(srcPath, SQLite::OPEN_READONLY);
        SQLite::Backup restore(db, srcDb);
        restore.executeStep(-1);
    }

    namespace {
        // Tables that make up each logical category. Delete order matters when
        // foreign keys are on; we copy with them off, but keep child-first order
        // for clarity and so re-enabling FK checks afterwards stays consistent.
        const std::vector<std::string> kProfileTables = {"profiles", "groups_order", "groups"};
        const std::vector<std::string> kRouteTables = {"route_rules", "route_profiles"};
        const std::vector<std::string> kSettingsTables = {"settings"};

        std::vector<std::string> tableColumns(SQLite::Database& d, const std::string& schema, const std::string& table) {
            std::vector<std::string> cols;
            // schema/table are internal constants, not user input -> safe to inline.
            SQLite::Statement q(d, "PRAGMA " + schema + ".table_info(" + table + ")");
            while (q.executeStep()) cols.emplace_back(q.getColumn(1).getText());
            return cols;
        }

        bool tableExists(SQLite::Database& d, const std::string& schema, const std::string& table) {
            SQLite::Statement q(d, "SELECT 1 FROM " + schema + ".sqlite_master WHERE type='table' AND name=?");
            q.bind(1, table);
            return q.executeStep();
        }

        // Replace every row of main.<table> with the rows from bak.<table>,
        // copying only the columns that exist in both schemas.
        void copyTable(SQLite::Database& d, const std::string& table) {
            if (!tableExists(d, "main", table) || !tableExists(d, "bak", table)) return;

            const auto mainCols = tableColumns(d, "main", table);
            const auto bakColsVec = tableColumns(d, "bak", table);
            const std::set<std::string> bakCols(bakColsVec.begin(), bakColsVec.end());

            std::string colList;
            for (const auto& c : mainCols) {
                if (bakCols.count(c) == 0) continue;
                if (!colList.empty()) colList += ",";
                colList += "\"" + c + "\"";
            }
            if (colList.empty()) return;

            d.exec("DELETE FROM main." + table);
            d.exec("INSERT INTO main." + table + " (" + colList + ") SELECT " + colList + " FROM bak." + table);
        }
    }

    void Database::backupSelective(const std::string& destPath, const BackupParts& parts) {
        // Take a full, WAL-safe snapshot first (same mechanism as backupTo),
        // then strip the categories the user did not select. entity_ids is
        // always kept so profile/route IDs stay consistent on restore.
        {
            std::lock_guard<std::recursive_mutex>
                locker(db_mutex);
            SQLite::Database destDb(destPath, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
            SQLite::Backup backup(destDb, db);
            backup.executeStep(-1);
        }

        SQLite::Database dest(destPath, SQLite::OPEN_READWRITE);
        auto wipe = [&](const std::vector<std::string>& tables) {
            for (const auto& t : tables) {
                try {
                    if (tableExists(dest, "main", t)) dest.exec("DELETE FROM " + t);
                } catch (...) {}
            }
        };
        if (!parts.profiles) wipe(kProfileTables);
        if (!parts.routes) wipe(kRouteTables);
        if (!parts.settings) wipe(kSettingsTables);
        try { dest.exec("VACUUM"); } catch (...) {}
    }

    void Database::restoreSelective(
        const std::string& srcPath,
        const BackupParts& parts)
    {
        if (!parts.anyDb()) {
            return;
        }

        // The whole restore operation must be atomic
        // relative to all other users of this Database.
        std::lock_guard<std::recursive_mutex>
            locker(db_mutex);

        // -------------------------------------------------
        // Attach backup database
        // -------------------------------------------------

        {
            SQLite::Statement attach(
                db,
                "ATTACH DATABASE ? AS bak"
            );

            attach.bind(
                1,
                srcPath
            );

            attach.exec();
        }

        try {

            // foreign_keys must be changed outside
            // the transaction.
            db.exec(
                "PRAGMA foreign_keys = OFF"
            );

            db.exec(
                "BEGIN IMMEDIATE"
            );

            // -------------------------------------------------
            // Restore selected tables
            // -------------------------------------------------

            if (parts.profiles) {

                for (const auto& table :
                    kProfileTables)
                {
                    copyTable(
                        db,
                        table
                    );
                }
            }

            if (parts.routes) {

                for (const auto& table :
                    kRouteTables)
                {
                    copyTable(
                        db,
                        table
                    );
                }
            }

            if (parts.settings) {

                for (const auto& table :
                    kSettingsTables)
                {
                    copyTable(
                        db,
                        table
                    );
                }
            }

            // -------------------------------------------------
            // Repair ID counters
            // -------------------------------------------------

            if (parts.profiles ||
                parts.routes)
            {
                const bool bakIds =
                    tableExists(
                        db,
                        "bak",
                        "entity_ids"
                    );

                db.exec(
                    "UPDATE entity_ids SET "

                    "profile_last_id = MAX("
                    "profile_last_id,"
                    "(SELECT COALESCE(MAX(id),0) "
                    "FROM profiles)"
                    +

                    std::string(
                        bakIds
                        ? ",(SELECT COALESCE("
                        "MAX(profile_last_id),0) "
                        "FROM bak.entity_ids)"
                        : ""
                    )

                    + "),"

                    "group_last_id = MAX("
                    "group_last_id,"
                    "(SELECT COALESCE(MAX(id),0) "
                    "FROM groups)"
                    +

                    std::string(
                        bakIds
                        ? ",(SELECT COALESCE("
                        "MAX(group_last_id),0) "
                        "FROM bak.entity_ids)"
                        : ""
                    )

                    + "),"

                    "route_profile_last_id = MAX("
                    "route_profile_last_id,"
                    "(SELECT COALESCE(MAX(id),0) "
                    "FROM route_profiles)"
                    +

                    std::string(
                        bakIds
                        ? ",(SELECT COALESCE("
                        "MAX(route_profile_last_id),0) "
                        "FROM bak.entity_ids)"
                        : ""
                    )

                    + ")"
                );
            }

            db.exec(
                "COMMIT"
            );
        }
        catch (...) {

            try {
                db.exec(
                    "ROLLBACK"
                );
            }
            catch (...) {
            }

            try {
                db.exec(
                    "PRAGMA foreign_keys = ON"
                );
            }
            catch (...) {
            }

            try {
                db.exec(
                    "DETACH DATABASE bak"
                );
            }
            catch (...) {
            }

            throw;
        }

        // -------------------------------------------------
        // Cleanup
        // -------------------------------------------------

        db.exec(
            "PRAGMA foreign_keys = ON"
        );

        db.exec(
            "DETACH DATABASE bak"
        );

        checkpointWal();
    }

    bool Database::execBatchUpdateProfileTraffic(
        const std::vector<ProfileTrafficRow>& rows)
    {
        if (rows.empty())
        {
            return true;
        }


        try
        {
            std::lock_guard<
                std::recursive_mutex
            > dbLocker(
                db_mutex
            );


            SQLite::Transaction transaction(
                db,
                SQLite::TransactionBehavior::IMMEDIATE
            );


            SQLite::Statement stmt(
                db,
                "UPDATE profiles "
                "SET traffic_dl = ?, "
                "traffic_up = ? "
                "WHERE id = ?"
            );


            int updatedRows =
                0;


            for (const auto& row :
                rows)
            {
                if (row.id < 0)
                {
                    continue;
                }


                stmt.bind(
                    1,
                    static_cast<int64_t>(
                        row.traffic_dl
                        )
                );

                stmt.bind(
                    2,
                    static_cast<int64_t>(
                        row.traffic_up
                        )
                );

                stmt.bind(
                    3,
                    row.id
                );


                stmt.exec();

                stmt.reset();

                ++updatedRows;
            }


            transaction.commit();


            if (updatedRows > 0)
            {
                maybeCheckpoint(
                    updatedRows
                );
            }


            return true;
        }
        catch (std::exception& e)
        {
            NotifyError(
                "execBatchUpdateProfileTraffic",
                e
            );


            return false;
        }
    }
}
