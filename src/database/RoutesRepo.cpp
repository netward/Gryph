#include "include/database/RoutesRepo.h"
#include "include/global/Configs.hpp"

#include <stdexcept>

#include <QJsonDocument>
#include <QJsonArray>
#include <QMutexLocker>
#include <QSet>

namespace Configs {
    RoutesRepo::RoutesRepo(
        Database& database)
        :
        db(database)
    {
        if (!createTables())
        {
            throw std::runtime_error(
                "Failed to initialize RoutesRepo database schema"
            );
        }
    }


    bool RoutesRepo::createTables() const
    {
        // =========================================================
        // route_profiles
        // =========================================================

        if (!db.exec(
            R"(
            CREATE TABLE IF NOT EXISTS route_profiles
            (
                id INTEGER PRIMARY KEY,
                name TEXT NOT NULL DEFAULT '',
                default_outbound_id INTEGER NOT NULL DEFAULT -1,

                created_at INTEGER NOT NULL
                    DEFAULT (strftime('%s', 'now')),

                updated_at INTEGER NOT NULL
                    DEFAULT (strftime('%s', 'now'))
            )
            )"))
        {
            MW_show_log(
                "RoutesRepo::createTables: "
                "failed to create route_profiles table"
            );

            return false;
        }


        // =========================================================
        // route_rules
        // =========================================================

        if (!db.exec(
            R"(
            CREATE TABLE IF NOT EXISTS route_rules
            (
                route_profile_id INTEGER NOT NULL,
                rule_order INTEGER NOT NULL,

                name TEXT NOT NULL DEFAULT '',
                type INTEGER NOT NULL DEFAULT 0,

                ip_version TEXT,
                network TEXT,
                protocol TEXT,

                inbound_json TEXT,
                domain_json TEXT,
                domain_suffix_json TEXT,
                domain_keyword_json TEXT,
                domain_regex_json TEXT,

                source_ip_cidr_json TEXT,
                source_ip_is_private INTEGER NOT NULL DEFAULT 0,

                ip_cidr_json TEXT,
                ip_is_private INTEGER NOT NULL DEFAULT 0,

                source_port_json TEXT,
                source_port_range_json TEXT,

                port_json TEXT,
                port_range_json TEXT,

                process_name_json TEXT,
                process_path_json TEXT,
                process_path_regex_json TEXT,

                rule_set_json TEXT,

                invert INTEGER NOT NULL DEFAULT 0,

                outbound_id INTEGER NOT NULL DEFAULT -2,

                action TEXT NOT NULL DEFAULT 'route',
                reject_method TEXT,

                no_drop INTEGER NOT NULL DEFAULT 0,

                override_address TEXT,
                override_port TEXT,

                sniffers_json TEXT,

                sniff_override_dest INTEGER NOT NULL DEFAULT 0,

                strategy TEXT,

                wifi_ssid_json TEXT,
                wifi_bssid_json TEXT,

                PRIMARY KEY
                (
                    route_profile_id,
                    rule_order
                ),

                FOREIGN KEY(route_profile_id)
                    REFERENCES route_profiles(id)
                    ON DELETE CASCADE
            )
            )"))
        {
            MW_show_log(
                "RoutesRepo::createTables: "
                "failed to create route_rules table"
            );

            return false;
        }


        // =========================================================
        // Migration: wifi_ssid_json
        // =========================================================

        if (!routeRulesColumnExists(
            "wifi_ssid_json"))
        {
            if (!db.exec(
                "ALTER TABLE route_rules "
                "ADD COLUMN wifi_ssid_json TEXT"))
            {
                MW_show_log(
                    "RoutesRepo::createTables: "
                    "failed to add wifi_ssid_json"
                );

                return false;
            }
        }


        // =========================================================
        // Migration: wifi_bssid_json
        // =========================================================

        if (!routeRulesColumnExists(
            "wifi_bssid_json"))
        {
            if (!db.exec(
                "ALTER TABLE route_rules "
                "ADD COLUMN wifi_bssid_json TEXT"))
            {
                MW_show_log(
                    "RoutesRepo::createTables: "
                    "failed to add wifi_bssid_json"
                );

                return false;
            }
        }


        return true;
    }

    bool RoutesRepo::routeRulesColumnExists(const char* columnName) const {
        auto pragma = db.query("PRAGMA table_info(route_rules)");
        if (!pragma) return false;
        while (pragma->executeStep()) {
            if (pragma->getColumn(1).getText() == std::string(columnName)) return true;
        }
        return false;
    }

    QJsonObject RoutesRepo::routeRuleToJson(const RouteRule* rule) const {
        QJsonObject json;
        
        json["name"] = rule->name;
        json["type"] = rule->type;
        json["ip_version"] = rule->ip_version;
        json["network"] = rule->network;
        json["protocol"] = rule->protocol;
        json["inbound"] = QListStr2QJsonArray(rule->inbound);
        json["domain"] = QListStr2QJsonArray(rule->domain);
        json["domain_suffix"] = QListStr2QJsonArray(rule->domain_suffix);
        json["domain_keyword"] = QListStr2QJsonArray(rule->domain_keyword);
        json["domain_regex"] = QListStr2QJsonArray(rule->domain_regex);
        json["source_ip_cidr"] = QListStr2QJsonArray(rule->source_ip_cidr);
        json["source_ip_is_private"] = rule->source_ip_is_private;
        json["ip_cidr"] = QListStr2QJsonArray(rule->ip_cidr);
        json["ip_is_private"] = rule->ip_is_private;
        json["source_port"] = QListStr2QJsonArray(rule->source_port);
        json["source_port_range"] = QListStr2QJsonArray(rule->source_port_range);
        json["port"] = QListStr2QJsonArray(rule->port);
        json["port_range"] = QListStr2QJsonArray(rule->port_range);
        json["process_name"] = QListStr2QJsonArray(rule->process_name);
        json["process_path"] = QListStr2QJsonArray(rule->process_path);
        json["process_path_regex"] = QListStr2QJsonArray(rule->process_path_regex);
        json["wifi_ssid"] = QListStr2QJsonArray(rule->wifi_ssid);
        json["wifi_bssid"] = QListStr2QJsonArray(rule->wifi_bssid);
        json["rule_set"] = QListStr2QJsonArray(rule->rule_set);
        json["invert"] = rule->invert;
        json["outboundID"] = rule->outboundID;
        json["action"] = rule->action;
        json["rejectMethod"] = rule->rejectMethod;
        json["no_drop"] = rule->no_drop;
        json["override_address"] = rule->override_address;
        json["override_port"] = rule->override_port;
        json["sniffers"] = QListStr2QJsonArray(rule->sniffers);
        json["sniffOverrideDest"] = rule->sniffOverrideDest;
        json["strategy"] = rule->strategy;
        
        return json;
    }

    std::shared_ptr<RouteRule> RoutesRepo::routeRuleFromJson(const QJsonObject& json) const {
        auto rule = std::make_shared<RouteRule>();
        
        rule->name = json["name"].toString();
        rule->type = json["type"].toInt();
        rule->ip_version = json["ip_version"].toString();
        rule->network = json["network"].toString();
        rule->protocol = json["protocol"].toString();
        rule->inbound = QJsonArray2QListString(json["inbound"].toArray());
        rule->domain = QJsonArray2QListString(json["domain"].toArray());
        rule->domain_suffix = QJsonArray2QListString(json["domain_suffix"].toArray());
        rule->domain_keyword = QJsonArray2QListString(json["domain_keyword"].toArray());
        rule->domain_regex = QJsonArray2QListString(json["domain_regex"].toArray());
        rule->source_ip_cidr = QJsonArray2QListString(json["source_ip_cidr"].toArray());
        rule->source_ip_is_private = json["source_ip_is_private"].toBool();
        rule->ip_cidr = QJsonArray2QListString(json["ip_cidr"].toArray());
        rule->ip_is_private = json["ip_is_private"].toBool();
        rule->source_port = QJsonArray2QListString(json["source_port"].toArray());
        rule->source_port_range = QJsonArray2QListString(json["source_port_range"].toArray());
        rule->port = QJsonArray2QListString(json["port"].toArray());
        rule->port_range = QJsonArray2QListString(json["port_range"].toArray());
        rule->process_name = QJsonArray2QListString(json["process_name"].toArray());
        rule->process_path = QJsonArray2QListString(json["process_path"].toArray());
        rule->process_path_regex = QJsonArray2QListString(json["process_path_regex"].toArray());
        rule->wifi_ssid = QJsonArray2QListString(json["wifi_ssid"].toArray());
        rule->wifi_bssid = QJsonArray2QListString(json["wifi_bssid"].toArray());
        rule->rule_set = QJsonArray2QListString(json["rule_set"].toArray());
        rule->invert = json["invert"].toBool();
        rule->outboundID = json["outboundID"].toInt();
        rule->action = json["action"].toString();
        rule->rejectMethod = json["rejectMethod"].toString();
        rule->no_drop = json["no_drop"].toBool();
        rule->override_address = json["override_address"].toString();
        rule->override_port = json["override_port"].toString();
        rule->sniffers = QJsonArray2QListString(json["sniffers"].toArray());
        rule->sniffOverrideDest = json["sniffOverrideDest"].toBool();
        rule->strategy = json["strategy"].toString();
        
        return rule;
    }

    QJsonObject RoutesRepo::routeProfileToJson(const RouteProfile* routeProfile) const {
        QJsonObject json;
        
        json["id"] = routeProfile->id;
        json["name"] = routeProfile->name;
        json["defaultOutboundID"] = routeProfile->defaultOutboundID;
        
        QJsonArray rulesArray;
        for (const auto& rule : routeProfile->Rules) {
            rulesArray.append(routeRuleToJson(rule.get()));
        }
        json["rules"] = rulesArray;
        
        return json;
    }

    std::shared_ptr<RouteProfile> RoutesRepo::routeProfileFromJson(const QJsonObject& json) const {
        auto routeProfile = std::make_shared<RouteProfile>();
        
        routeProfile->id = json["id"].toInt();
        routeProfile->name = json["name"].toString();
        routeProfile->defaultOutboundID = json["defaultOutboundID"].toInt();
        
        // Load rules
        if (json.contains("rules") && json["rules"].isArray()) {
            QJsonArray rulesArray = json["rules"].toArray();
            for (const auto& ruleValue : rulesArray) {
                if (ruleValue.isObject()) {
                    auto rule = routeRuleFromJson(ruleValue.toObject());
                    routeProfile->Rules.append(rule);
                }
            }
        }
        
        return routeProfile;
    }

    bool RoutesRepo::saveToDatabase(
        const RouteProfile* routeProfile,
        int id) const
    {
        // =========================================================
        // Validation
        // =========================================================

        if (!routeProfile ||
            id < 0)
        {
            return false;
        }


        // =========================================================
        // Small helper:
        //
        // QList<QString>
        //      ->
        // compact JSON string
        //
        // The DB layer deliberately knows nothing about Qt.
        // =========================================================

        const auto serializeStringList =
            [](
                const QList<QString>& values
                ) -> std::string
            {
                const QJsonArray array =
                    QListStr2QJsonArray(
                        values
                    );


                const QByteArray json =
                    QJsonDocument(
                        array
                    )
                    .toJson(
                        QJsonDocument::Compact
                    );


                return QString::fromUtf8(
                    json
                )
                    .toStdString();
            };


        // =========================================================
        // Freeze RouteProfile into DB-only data.
        //
        // After this stage the Database code doesn't need to know
        // anything about RouteProfile / RouteRule / QString.
        // =========================================================

        RouteProfileSaveRow row;


        row.id =
            id;


        row.name =
            routeProfile
            ->name
            .toStdString();


        row.default_outbound_id =
            routeProfile
            ->defaultOutboundID;


        row.rules.reserve(
            static_cast<size_t>(
                routeProfile
                ->Rules
                .size()
                )
        );


        // =========================================================
        // Serialize every rule before entering SQLite transaction.
        //
        // This keeps transaction duration short.
        // =========================================================

        for (const auto& rule :
            routeProfile->Rules)
        {
            if (!rule)
            {
                continue;
            }


            RouteRuleInsertRow dbRule;


            // -----------------------------------------------------
            // Basic fields
            // -----------------------------------------------------

            dbRule.name =
                rule
                ->name
                .toStdString();


            dbRule.type =
                rule->type;


            dbRule.ip_version =
                rule
                ->ip_version
                .toStdString();


            dbRule.network =
                rule
                ->network
                .toStdString();


            dbRule.protocol =
                rule
                ->protocol
                .toStdString();


            // -----------------------------------------------------
            // Match lists
            // -----------------------------------------------------

            dbRule.inbound_json =
                serializeStringList(
                    rule->inbound
                );


            dbRule.domain_json =
                serializeStringList(
                    rule->domain
                );


            dbRule.domain_suffix_json =
                serializeStringList(
                    rule->domain_suffix
                );


            dbRule.domain_keyword_json =
                serializeStringList(
                    rule->domain_keyword
                );


            dbRule.domain_regex_json =
                serializeStringList(
                    rule->domain_regex
                );


            dbRule.source_ip_cidr_json =
                serializeStringList(
                    rule->source_ip_cidr
                );


            dbRule.source_ip_is_private =
                rule->source_ip_is_private;


            dbRule.ip_cidr_json =
                serializeStringList(
                    rule->ip_cidr
                );


            dbRule.ip_is_private =
                rule->ip_is_private;


            dbRule.source_port_json =
                serializeStringList(
                    rule->source_port
                );


            dbRule.source_port_range_json =
                serializeStringList(
                    rule->source_port_range
                );


            dbRule.port_json =
                serializeStringList(
                    rule->port
                );


            dbRule.port_range_json =
                serializeStringList(
                    rule->port_range
                );


            dbRule.process_name_json =
                serializeStringList(
                    rule->process_name
                );


            dbRule.process_path_json =
                serializeStringList(
                    rule->process_path
                );


            dbRule.process_path_regex_json =
                serializeStringList(
                    rule->process_path_regex
                );


            dbRule.rule_set_json =
                serializeStringList(
                    rule->rule_set
                );


            // -----------------------------------------------------
            // Behavior
            // -----------------------------------------------------

            dbRule.invert =
                rule->invert;


            dbRule.outbound_id =
                rule->outboundID;


            dbRule.action =
                rule
                ->action
                .toStdString();


            dbRule.reject_method =
                rule
                ->rejectMethod
                .toStdString();


            dbRule.no_drop =
                rule->no_drop;


            // -----------------------------------------------------
            // Route options
            // -----------------------------------------------------

            dbRule.override_address =
                rule
                ->override_address
                .toStdString();


            dbRule.override_port =
                rule
                ->override_port
                .toStdString();


            // -----------------------------------------------------
            // Sniff
            // -----------------------------------------------------

            dbRule.sniffers_json =
                serializeStringList(
                    rule->sniffers
                );


            dbRule.sniff_override_dest =
                rule->sniffOverrideDest;


            // -----------------------------------------------------
            // Resolve
            // -----------------------------------------------------

            dbRule.strategy =
                rule
                ->strategy
                .toStdString();


            // -----------------------------------------------------
            // Wi-Fi
            // -----------------------------------------------------

            dbRule.wifi_ssid_json =
                serializeStringList(
                    rule->wifi_ssid
                );


            dbRule.wifi_bssid_json =
                serializeStringList(
                    rule->wifi_bssid
                );


            // -----------------------------------------------------
            // Publish immutable DB row.
            // -----------------------------------------------------

            row.rules.push_back(
                std::move(
                    dbRule
                )
            );
        }


        // =========================================================
        // ONE atomic DB operation.
        //
        // Database guarantees:
        //
        // route_profiles UPSERT
        //       +
        // old route_rules DELETE
        //       +
        // every new rule INSERT
        //
        // either all commit or all rollback.
        // =========================================================

        return db.saveRouteProfileAtomic(
            row
        );
    }

    QJsonObject RoutesRepo::ruleJsonFromRow(SQLite::Statement& stmt, int baseCol) const {
        QJsonObject ruleJson;
        ruleJson["name"] = QString::fromStdString(stmt.getColumn(baseCol + 0).getText());
        ruleJson["type"] = stmt.getColumn(baseCol + 1).getInt();
        ruleJson["ip_version"] = QString::fromStdString(stmt.getColumn(baseCol + 2).getText());
        ruleJson["network"] = QString::fromStdString(stmt.getColumn(baseCol + 3).getText());
        ruleJson["protocol"] = QString::fromStdString(stmt.getColumn(baseCol + 4).getText());
        
        auto parseJsonArray = [](const std::string& s) {
            QJsonDocument doc = QJsonDocument::fromJson(QString::fromStdString(s).toUtf8());
            return doc.isArray() ? doc.array() : QJsonArray();
        };
        ruleJson["inbound"] = parseJsonArray(stmt.getColumn(baseCol + 5).getText());
        ruleJson["domain"] = parseJsonArray(stmt.getColumn(baseCol + 6).getText());
        ruleJson["domain_suffix"] = parseJsonArray(stmt.getColumn(baseCol + 7).getText());
        ruleJson["domain_keyword"] = parseJsonArray(stmt.getColumn(baseCol + 8).getText());
        ruleJson["domain_regex"] = parseJsonArray(stmt.getColumn(baseCol + 9).getText());
        ruleJson["source_ip_cidr"] = parseJsonArray(stmt.getColumn(baseCol + 10).getText());
        ruleJson["source_ip_is_private"] = stmt.getColumn(baseCol + 11).getInt() != 0;
        ruleJson["ip_cidr"] = parseJsonArray(stmt.getColumn(baseCol + 12).getText());
        ruleJson["ip_is_private"] = stmt.getColumn(baseCol + 13).getInt() != 0;
        ruleJson["source_port"] = parseJsonArray(stmt.getColumn(baseCol + 14).getText());
        ruleJson["source_port_range"] = parseJsonArray(stmt.getColumn(baseCol + 15).getText());
        ruleJson["port"] = parseJsonArray(stmt.getColumn(baseCol + 16).getText());
        ruleJson["port_range"] = parseJsonArray(stmt.getColumn(baseCol + 17).getText());
        ruleJson["process_name"] = parseJsonArray(stmt.getColumn(baseCol + 18).getText());
        ruleJson["process_path"] = parseJsonArray(stmt.getColumn(baseCol + 19).getText());
        ruleJson["process_path_regex"] = parseJsonArray(stmt.getColumn(baseCol + 20).getText());
        ruleJson["rule_set"] = parseJsonArray(stmt.getColumn(baseCol + 21).getText());
        ruleJson["invert"] = stmt.getColumn(baseCol + 22).getInt() != 0;
        ruleJson["outboundID"] = stmt.getColumn(baseCol + 23).getInt();
        ruleJson["action"] = QString::fromStdString(stmt.getColumn(baseCol + 24).getText());
        ruleJson["rejectMethod"] = QString::fromStdString(stmt.getColumn(baseCol + 25).getText());
        ruleJson["no_drop"] = stmt.getColumn(baseCol + 26).getInt() != 0;
        ruleJson["override_address"] = QString::fromStdString(stmt.getColumn(baseCol + 27).getText());
        ruleJson["override_port"] = QString::fromStdString(stmt.getColumn(baseCol + 28).getText());
        ruleJson["sniffers"] = parseJsonArray(stmt.getColumn(baseCol + 29).getText());
        ruleJson["sniffOverrideDest"] = stmt.getColumn(baseCol + 30).getInt() != 0;
        ruleJson["strategy"] = QString::fromStdString(stmt.getColumn(baseCol + 31).getText());
        ruleJson["wifi_ssid"] = parseJsonArray(stmt.getColumn(baseCol + 32).getText());
        ruleJson["wifi_bssid"] = parseJsonArray(stmt.getColumn(baseCol + 33).getText());
        return ruleJson;
    }

    std::shared_ptr<RouteProfile> RoutesRepo::routeProfileFromProfileRow(SQLite::Statement& stmt) const {
        QJsonObject json;
        json["id"] = stmt.getColumn(0).getInt();
        json["name"] = QString::fromStdString(stmt.getColumn(1).getText());
        json["defaultOutboundID"] = stmt.getColumn(2).getInt();
        json["rules"] = QJsonArray();
        return routeProfileFromJson(json);
    }

    void RoutesRepo::loadRulesForProfileIdsChunk(const QList<int>& profileIds, std::map<int, std::shared_ptr<RouteProfile>>& byId) const {
        if (profileIds.isEmpty()) return;
        QString idList;
        for (int i = 0; i < profileIds.size(); ++i) {
            if (i > 0) idList += ",";
            idList += QString::number(profileIds[i]);
        }
        std::string sql =
            "SELECT route_profile_id, name, type, ip_version, network, protocol, "
            "inbound_json, domain_json, domain_suffix_json, domain_keyword_json, domain_regex_json, "
            "source_ip_cidr_json, source_ip_is_private, ip_cidr_json, ip_is_private, "
            "source_port_json, source_port_range_json, port_json, port_range_json, "
            "process_name_json, process_path_json, process_path_regex_json, rule_set_json, "
            "invert, outbound_id, action, reject_method, no_drop, "
            "override_address, override_port, sniffers_json, sniff_override_dest, strategy, "
            "wifi_ssid_json, wifi_bssid_json "
            "FROM route_rules WHERE route_profile_id IN (" + idList.toStdString() + ") ORDER BY route_profile_id, rule_order";
        auto rulesQuery = db.query(sql);
        if (!rulesQuery) return;
        while (rulesQuery->executeStep()) {
            int profileId = rulesQuery->getColumn(0).getInt();
            auto it = byId.find(profileId);
            if (it != byId.end()) {
                it->second->Rules.append(routeRuleFromJson(ruleJsonFromRow(*rulesQuery, 1)));
            }
        }
    }

    std::shared_ptr<RouteProfile> RoutesRepo::loadFromDatabase(int id) const {
        auto profileQuery = db.query(R"(
            SELECT id, name, default_outbound_id
            FROM route_profiles WHERE id = ?
        )", id);
        if (!profileQuery || !profileQuery->executeStep()) {
            return nullptr;
        }
        
        auto routeProfile = routeProfileFromProfileRow(*profileQuery);
        
        auto rulesQuery = db.query(R"(
            SELECT name, type, ip_version, network, protocol,
                   inbound_json, domain_json, domain_suffix_json, domain_keyword_json, domain_regex_json,
                   source_ip_cidr_json, source_ip_is_private, ip_cidr_json, ip_is_private,
                   source_port_json, source_port_range_json, port_json, port_range_json,
                   process_name_json, process_path_json, process_path_regex_json, rule_set_json,
                   invert, outbound_id, action, reject_method, no_drop,
                   override_address, override_port, sniffers_json, sniff_override_dest, strategy,
                   wifi_ssid_json, wifi_bssid_json
            FROM route_rules WHERE route_profile_id = ? ORDER BY rule_order
        )", id);
        if (rulesQuery) {
            while (rulesQuery->executeStep()) {
                routeProfile->Rules.append(routeRuleFromJson(ruleJsonFromRow(*rulesQuery, 0)));
            }
        }
        
        return routeProfile;
    }

    std::shared_ptr<RouteProfile> RoutesRepo::NewRouteProfile() {
        return std::make_shared<RouteProfile>();
    }

    bool RoutesRepo::AddRouteProfile(
        std::shared_ptr<RouteProfile>& routeProfile)
    {
        if (!routeProfile)
        {
            return false;
        }


        if (routeProfile->id >= 0)
        {
            return false;
        }


        const int newId =
            NewRouteProfileID();


        if (newId <= 0)
        {
            return false;
        }


        routeProfile->id =
            newId;


        // Persist FIRST.
        if (!saveToDatabase(
            routeProfile.get(),
            newId))
        {
            routeProfile->id =
                -1;

            return false;
        }


        {
            std::lock_guard<std::mutex>
                locker(
                    mutex
                );


            identityMap[newId] =
                std::weak_ptr<RouteProfile>(
                    routeProfile
                );
        }


        return true;
    }

    std::shared_ptr<RouteProfile> RoutesRepo::GetRouteProfile(int id) const {
        QMutexLocker locker(&mutex);
        if (auto it = identityMap.find(id); it != identityMap.end()) {
            if (auto shared = it->second.lock()) return shared;
            identityMap.erase(it);
        }
        auto routeProfile = loadFromDatabase(id);
        if (!routeProfile) return nullptr;
        identityMap[id] = std::weak_ptr<RouteProfile>(routeProfile);
        return routeProfile;
    }

    bool RoutesRepo::DeleteRouteProfile(
        int id)
    {
        // =========================================================
        // Validation
        // =========================================================

        if (id < 0)
        {
            MW_show_log(
                "RoutesRepo::DeleteRouteProfile: "
                "invalid route profile ID"
            );

            return false;
        }


        // =========================================================
        // Serialize repository mutation.
        // =========================================================

        std::lock_guard<std::mutex>
            locker(
                mutex
            );


        // =========================================================
        // Persist FIRST.
        //
        // route_rules are removed automatically because
        // route_rules.route_profile_id has ON DELETE CASCADE.
        // =========================================================

        const bool deleted =
            db.exec(
                "DELETE FROM route_profiles "
                "WHERE id = ?",
                id
            );


        if (!deleted)
        {
            MW_show_log(
                "RoutesRepo::DeleteRouteProfile: "
                "failed to delete route profile "
                +
                Int2String(id)
            );

            return false;
        }


        // =========================================================
        // Update memory only after SQLite accepted the DELETE.
        // =========================================================

        identityMap.erase(
            id
        );


        return true;
    }

    bool RoutesRepo::UpdateRouteProfiles(
        const QList<
        std::shared_ptr<RouteProfile>
        >& routeProfiles)
    {
        // =========================================================
        // PHASE 1
        //
        // Read currently persisted IDs.
        //
        // Do this BEFORE taking RoutesRepo::mutex.
        // =========================================================

        QSet<int>
            existingIds;


        {
            auto query =
                db.query(
                    "SELECT id "
                    "FROM route_profiles"
                );


            if (!query)
            {
                MW_show_log(
                    "RoutesRepo::UpdateRouteProfiles: "
                    "failed to read existing route profile IDs"
                );

                return false;
            }


            try
            {
                while (query->executeStep())
                {
                    existingIds.insert(
                        query
                        ->getColumn(0)
                        .getInt()
                    );
                }
            }
            catch (const std::exception& e)
            {
                MW_show_log(
                    "RoutesRepo::UpdateRouteProfiles: "
                    "failed while reading existing IDs: "
                    +
                    QString::fromUtf8(
                        e.what()
                    )
                );

                return false;
            }
        }


        // =========================================================
        // PHASE 2
        //
        // Validate input.
        // =========================================================

        QSet<int>
            newIds;


        struct AssignedNewId
        {
            std::shared_ptr<RouteProfile>
                profile;

            int id =
                -1;
        };


        QList<AssignedNewId>
            newlyAssigned;


        newlyAssigned.reserve(
            routeProfiles.size()
        );


        // =========================================================
        // PHASE 3
        //
        // Serialize RoutesRepo changes.
        // =========================================================

        std::lock_guard<std::mutex>
            locker(
                mutex
            );


        // =========================================================
        // Persist every supplied RouteProfile.
        // =========================================================

        for (const auto& routeProfile :
            routeProfiles)
        {
            if (!routeProfile)
            {
                MW_show_log(
                    "RoutesRepo::UpdateRouteProfiles: "
                    "null RouteProfile in input"
                );

                // Undo IDs which we assigned during this call.
                for (auto it =
                    newlyAssigned.rbegin();
                    it !=
                    newlyAssigned.rend();
                    ++it)
                {
                    if (it->profile &&
                        it->profile->id ==
                        it->id)
                    {
                        it->profile->id =
                            -1;
                    }
                }

                return false;
            }


            // =====================================================
            // Allocate ID for a new profile.
            // =====================================================

            if (routeProfile->id < 0)
            {
                const int newId =
                    NewRouteProfileID();


                if (newId <= 0)
                {
                    MW_show_log(
                        "RoutesRepo::UpdateRouteProfiles: "
                        "failed to allocate RouteProfile ID"
                    );


                    for (auto it =
                        newlyAssigned.rbegin();
                        it !=
                        newlyAssigned.rend();
                        ++it)
                    {
                        if (it->profile &&
                            it->profile->id ==
                            it->id)
                        {
                            it->profile->id =
                                -1;
                        }
                    }


                    return false;
                }


                routeProfile->id =
                    newId;


                newlyAssigned.append(
                    {
                        routeProfile,
                        newId
                    }
                );
            }


            const int id =
                routeProfile->id;


            // =====================================================
            // Duplicate IDs in input are invalid.
            // =====================================================

            if (newIds.contains(
                id))
            {
                MW_show_log(
                    "RoutesRepo::UpdateRouteProfiles: "
                    "duplicate RouteProfile ID "
                    +
                    Int2String(id)
                );


                for (auto it =
                    newlyAssigned.rbegin();
                    it !=
                    newlyAssigned.rend();
                    ++it)
                {
                    if (it->profile &&
                        it->profile->id ==
                        it->id)
                    {
                        it->profile->id =
                            -1;
                    }
                }


                return false;
            }


            newIds.insert(
                id
            );


            // =====================================================
            // Persist FIRST.
            //
            // This assumes saveToDatabase() has already been changed
            // from void -> bool as described in the RoutesRepo
            // migration.
            // =====================================================

            if (!saveToDatabase(
                routeProfile.get(),
                id))
            {
                MW_show_log(
                    "RoutesRepo::UpdateRouteProfiles: "
                    "failed to persist RouteProfile "
                    +
                    Int2String(id)
                );


                // Roll back only IDs assigned in memory.
                //
                // NOTE:
                // This does NOT yet make the complete multi-profile
                // DB operation atomic. That will require one SQLite
                // transaction around the complete update.
                for (auto it =
                    newlyAssigned.rbegin();
                    it !=
                    newlyAssigned.rend();
                    ++it)
                {
                    if (it->profile &&
                        it->profile->id ==
                        it->id)
                    {
                        it->profile->id =
                            -1;
                    }
                }


                return false;
            }
        }


        // =========================================================
        // Determine obsolete profiles.
        // =========================================================

        std::vector<int>
            toDelete;


        toDelete.reserve(
            static_cast<size_t>(
                existingIds.size()
                )
        );


        for (const int id :
        existingIds)
        {
            if (!newIds.contains(
                id))
            {
                toDelete.push_back(
                    id
                );
            }
        }


        // =========================================================
        // Delete obsolete persistent rows BEFORE changing cache.
        // =========================================================

        if (!toDelete.empty())
        {
            if (!db.execDeleteByIdIn(
                "route_profiles",
                "id",
                toDelete))
            {
                MW_show_log(
                    "RoutesRepo::UpdateRouteProfiles: "
                    "failed to delete obsolete route profiles"
                );


                return false;
            }
        }


        // =========================================================
        // PHASE 4
        //
        // DB operations succeeded.
        //
        // Publish identity map changes.
        // =========================================================

        for (const auto& routeProfile :
            routeProfiles)
        {
            if (!routeProfile ||
                routeProfile->id < 0)
            {
                continue;
            }


            identityMap[
                routeProfile->id
            ] =
                std::weak_ptr<RouteProfile>(
                    routeProfile
                );
        }


        for (const int id :
        toDelete)
        {
            identityMap.erase(
                id
            );
        }


        return true;
    }

    QList<int> RoutesRepo::GetAllRouteProfileIds() const {
        QList<int> ids;
        auto query = db.query("SELECT id FROM route_profiles ORDER BY id");
        if (query) {
            while (query->executeStep()) {
                ids.append(query->getColumn(0).getInt());
            }
        }
        return ids;
    }

    QList<std::shared_ptr<RouteProfile>>
        RoutesRepo::GetAllRouteProfiles() const
    {
        QList<std::shared_ptr<RouteProfile>>
            routeProfiles;

        std::map<
            int,
            std::shared_ptr<RouteProfile>
        > byId;

        QList<int> idsInOrder;

        QSet<int> cachedProfiles;

        // Global lock order:
        //
        // repository mutex
        //       ↓
        // Database db_mutex
        QMutexLocker locker(
            &mutex
        );

        auto profileQuery =
            db.query(
                "SELECT id, name, "
                "default_outbound_id "
                "FROM route_profiles "
                "ORDER BY id"
            );

        if (!profileQuery) {
            return routeProfiles;
        }

        while (
            profileQuery->executeStep())
        {
            const int id =
                profileQuery
                ->getColumn(0)
                .getInt();

            auto it =
                identityMap.find(id);

            if (it !=
                identityMap.end())
            {
                if (auto shared =
                    it->second.lock())
                {
                    byId[id] =
                        shared;

                    idsInOrder.append(
                        id
                    );

                    cachedProfiles.insert(
                        id
                    );

                    continue;
                }

                identityMap.erase(
                    it
                );
            }

            auto profile =
                routeProfileFromProfileRow(
                    *profileQuery
                );

            byId[id] =
                profile;

            idsInOrder.append(
                id
            );

            identityMap[id] =
                std::weak_ptr<RouteProfile>(
                    profile
                );
        }

        // Release the large SELECT statement before
        // starting subsequent queries.
        profileQuery = {};

        if (byId.empty()) {
            return routeProfiles;
        }

        for (
            int off = 0;
            off < idsInOrder.size();
            off += Configs::BATCH_LIMIT_READ)
        {
            const int end =
                std::min(
                    off
                    + Configs::BATCH_LIMIT_READ,
                    static_cast<int>(
                        idsInOrder.size()
                        )
                );

            QList<int> chunk;

            for (int i = off;
                i < end;
                ++i)
            {
                if (!cachedProfiles.contains(
                    idsInOrder[i]))
                {
                    chunk.append(
                        idsInOrder[i]
                    );
                }
            }

            if (chunk.isEmpty()) {
                continue;
            }

            loadRulesForProfileIdsChunk(
                chunk,
                byId
            );
        }

        for (int id : idsInOrder) {

            routeProfiles.append(
                byId[id]
            );
        }

        return routeProfiles;
    }

    int RoutesRepo::NewRouteProfileID() const {
        // Atomically increment and get the new ID (DB atomic, no lock required)
        auto query = db.query("UPDATE entity_ids SET route_profile_last_id = route_profile_last_id + 1 RETURNING route_profile_last_id");
        if (query && query->executeStep()) {
            return query->getColumn(0).getInt();
        }
        
        // Fallback if RETURNING is not supported (shouldn't happen with modern SQLite)
        return 0;
    }

    bool RoutesRepo::Save(
        const std::shared_ptr<RouteProfile>& routeProfile)
    {
        if (!routeProfile ||
            routeProfile->id < 0)
        {
            return false;
        }


        const int id =
            routeProfile->id;


        if (!saveToDatabase(
            routeProfile.get(),
            id))
        {
            return false;
        }


        {
            std::lock_guard<std::mutex>
                locker(
                    mutex
                );


            identityMap[id] =
                std::weak_ptr<RouteProfile>(
                    routeProfile
                );
        }


        return true;
    }
}
