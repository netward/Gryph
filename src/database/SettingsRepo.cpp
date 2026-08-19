#include "include/database/SettingsRepo.h"

#include "include/global/Utils.hpp"

#include <string>
#include <utility>
#include <vector>
#include <stdexcept>

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QString>
#include <QStringList>


namespace Configs
{

    // =========================================================
    // Constructor
    // =========================================================

    SettingsRepo::SettingsRepo(
        Database& database)
        :
        db(database)
    {
        initMaps();


        if (!createTables())
        {
            throw std::runtime_error(
                "Failed to create SettingsRepo tables"
            );
        }


        loadAllSettings();
    }

    // =========================================================
    // Setting maps
    // =========================================================

    void SettingsRepo::initMaps()
    {
        boolMap.clear();
        intMap.clear();
        stringMap.clear();
        stringListMap.clear();


        // -----------------------------------------------------
        // bool
        // -----------------------------------------------------

        boolMap.insert(
            "disable_tray",
            &disable_tray
        );

        boolMap.insert(
            "random_inbound_port",
            &random_inbound_port
        );

        boolMap.insert(
            "mux_padding",
            &mux_padding
        );

        boolMap.insert(
            "mux_default_on",
            &mux_default_on
        );

        boolMap.insert(
            "net_use_proxy",
            &net_use_proxy
        );

        boolMap.insert(
            "remember_enable",
            &remember_enable
        );

        boolMap.insert(
            "skip_cert",
            &skip_cert
        );

        boolMap.insert(
            "fakedns",
            &fake_dns
        );

        boolMap.insert(
            "disable_traffic_stats",
            &disable_traffic_stats
        );

        boolMap.insert(
            "vpn_ipv6",
            &vpn_ipv6
        );

        boolMap.insert(
            "vpn_strict_route",
            &vpn_strict_route
        );

        boolMap.insert(
            "sub_clear",
            &sub_clear
        );

        boolMap.insert(
            "net_insecure",
            &net_insecure
        );

        boolMap.insert(
            "sub_send_hwid",
            &sub_send_hwid
        );

        boolMap.insert(
            "start_minimal",
            &start_minimal
        );

        boolMap.insert(
            "enable_ntp",
            &enable_ntp
        );

        boolMap.insert(
            "enable_dns_server",
            &enable_dns_server
        );

        boolMap.insert(
            "dns_server_listen_lan",
            &dns_server_listen_lan
        );

        boolMap.insert(
            "enable_redirect",
            &enable_redirect
        );

        boolMap.insert(
            "system_dns_set",
            &system_dns_set
        );

        boolMap.insert(
            "windows_set_admin",
            &windows_set_admin
        );

        boolMap.insert(
            "disable_win_admin",
            &disable_run_admin
        );

        boolMap.insert(
            "enable_stats",
            &enable_stats
        );

        boolMap.insert(
            "disable_privilege_req",
            &disable_privilege_req
        );

        boolMap.insert(
            "enable_tun_routing",
            &enable_tun_routing
        );

        boolMap.insert(
            "use_mozilla_certs",
            &use_mozilla_certs
        );

        boolMap.insert(
            "allow_beta_update",
            &allow_beta_update
        );

        boolMap.insert(
            "adblock_enable",
            &adblock_enable
        );

        boolMap.insert(
            "show_system_dns",
            &show_system_dns
        );

        boolMap.insert(
            "use_custom_icons",
            &use_custom_icons
        );

        boolMap.insert(
            "xray_mux_default_on",
            &xray_mux_default_on
        );

        boolMap.insert(
            "use_dns_object",
            &use_dns_object
        );

        boolMap.insert(
            "skip_delete_confirmation",
            &skip_delete_confirmation
        );

        boolMap.insert(
            "log_enable_include",
            &log_enable_include
        );

        boolMap.insert(
            "log_enable_exclude",
            &log_enable_exclude
        );

        boolMap.insert(
            "log_auto_scroll",
            &log_auto_scroll
        );

        boolMap.insert(
            "enable_warp",
            &enable_warp
        );

        boolMap.insert(
            "enable_dns_routing",
            &enable_dns_routing
        );

        boolMap.insert(
            "inbound_auth",
            &inbound_auth
        );

        boolMap.insert(
            "allow_stopping_active_profile",
            &allow_stopping_active_profile
        );

        boolMap.insert(
            "disable_mixed_inbound",
            &disable_mixed_inbound
        );

        boolMap.insert(
            "system_proxy_enabled",
            &remember_system_proxy
        );

        boolMap.insert(
            "tun_mode_enabled",
            &remember_tun
        );

        boolMap.insert(
            "reset_proxy_on_disable_sp",
            &reset_proxy_on_disable_sp
        );

        boolMap.insert(
            "dns_disable_cache",
            &dns_disable_cache
        );

        boolMap.insert(
            "dns_disable_expire",
            &dns_disable_expire
        );

        boolMap.insert(
            "dns_reverse_mapping",
            &dns_reverse_mapping
        );


        // -----------------------------------------------------
        // int
        // -----------------------------------------------------

        intMap.insert(
            "current_group",
            &current_group
        );

        intMap.insert(
            "inbound_socks_port",
            &inbound_socks_port
        );

        intMap.insert(
            "mux_concurrency",
            &mux_concurrency
        );

        intMap.insert(
            "test_concurrent",
            &test_concurrent
        );

        intMap.insert(
            "remember_id",
            &remember_id
        );

        intMap.insert(
            "language",
            &language
        );

        intMap.insert(
            "font_size",
            &font_size
        );

        intMap.insert(
            "max_log_line",
            &max_log_line
        );

        intMap.insert(
            "stats_tab",
            &stats_tab
        );

        intMap.insert(
            "sub_auto_update",
            &sub_auto_update
        );

        intMap.insert(
            "vpn_mtu",
            &vpn_mtu
        );

        intMap.insert(
            "ntp_server_port",
            &ntp_server_port
        );

        intMap.insert(
            "dns_server_listen_port",
            &dns_server_listen_port
        );

        intMap.insert(
            "redirect_listen_port",
            &redirect_listen_port
        );

        intMap.insert(
            "core_box_clash_api",
            &core_box_clash_api
        );

        intMap.insert(
            "speed_test_mode",
            &speed_test_mode
        );

        intMap.insert(
            "speed_test_timeout_ms",
            &speed_test_timeout_ms
        );

        intMap.insert(
            "url_test_timeout_ms",
            &url_test_timeout_ms
        );

        intMap.insert(
            "xray_mux_concurrency",
            &xray_mux_concurrency
        );

        intMap.insert(
            "current_route_id",
            &current_route_id
        );

        intMap.insert(
            "sniffing_mode",
            &sniffing_mode
        );

        intMap.insert(
            "ruleset_mirror",
            &ruleset_mirror
        );

        intMap.insert(
            "core_dns_in_port",
            &core_dns_in_port
        );

        intMap.insert(
            "dns_cache_capacity",
            &dns_cache_capacity
        );


        // -----------------------------------------------------
        // QString
        // -----------------------------------------------------

        stringMap.insert(
            "user_agent2",
            &user_agent
        );

        stringMap.insert(
            "test_url",
            &test_latency_url
        );

        stringMap.insert(
            "inbound_address",
            &inbound_address
        );

        stringMap.insert(
            "log_level",
            &log_level
        );

        stringMap.insert(
            "mux_protocol",
            &mux_protocol
        );

        stringMap.insert(
            "theme",
            &theme
        );

        stringMap.insert(
            "custom_inbound",
            &custom_inbound
        );

        stringMap.insert(
            "custom_route",
            &custom_route_global
        );

        stringMap.insert(
            "font",
            &font
        );

        stringMap.insert(
            "hk_mw",
            &hotkey_mainwindow
        );

        stringMap.insert(
            "hk_group",
            &hotkey_group
        );

        stringMap.insert(
            "hk_route",
            &hotkey_route
        );

        stringMap.insert(
            "hk_spmenu",
            &hotkey_system_proxy_menu
        );

        stringMap.insert(
            "hk_toggle",
            &hotkey_toggle_system_proxy
        );

        stringMap.insert(
            "active_routing",
            &active_routing
        );

        stringMap.insert(
            "mw_size",
            &mw_size
        );

        stringMap.insert(
            "vpn_impl",
            &vpn_implementation
        );

        stringMap.insert(
            "vpn_tun_ipv4_cidr",
            &vpn_tun_ipv4_cidr
        );

        stringMap.insert(
            "vpn_tun_ipv6_cidr",
            &vpn_tun_ipv6_cidr
        );

        stringMap.insert(
            "sub_custom_hwid_params",
            &sub_custom_hwid_params
        );

        stringMap.insert(
            "splitter_state",
            &splitter_state
        );

        stringMap.insert(
            "utlsFingerprint",
            &utlsFingerprint
        );

        stringMap.insert(
            "core_box_clash_listen_addr",
            &core_box_clash_listen_addr
        );

        stringMap.insert(
            "core_box_clash_api_secret",
            &core_box_clash_api_secret
        );

        stringMap.insert(
            "core_box_underlying_dns",
            &core_box_underlying_dns
        );

        stringMap.insert(
            "ntp_server_address",
            &ntp_server_address
        );

        stringMap.insert(
            "ntp_interval",
            &ntp_interval
        );

        stringMap.insert(
            "dns_v4_resp",
            &dns_v4_resp
        );

        stringMap.insert(
            "dns_v6_resp",
            &dns_v6_resp
        );

        stringMap.insert(
            "redirect_listen_address",
            &redirect_listen_address
        );

        stringMap.insert(
            "proxy_scheme",
            &proxy_scheme
        );

        stringMap.insert(
            "main_window_geometry",
            &mainWindowGeometry
        );

        stringMap.insert(
            "xray_log_level",
            &xray_log_level
        );

        stringMap.insert(
            "remote_dns",
            &remote_dns
        );

        stringMap.insert(
            "remote_dns_strategy",
            &remote_dns_strategy
        );

        stringMap.insert(
            "direct_dns",
            &direct_dns
        );

        stringMap.insert(
            "direct_dns_strategy",
            &direct_dns_strategy
        );

        stringMap.insert(
            "dns_object",
            &dns_object
        );

        stringMap.insert(
            "dns_final_out",
            &dns_final_out
        );

        stringMap.insert(
            "domain_strategy",
            &resolve_domain_strategy
        );

        stringMap.insert(
            "outbound_domain_strategy",
            &default_domain_strategy
        );

        stringMap.insert(
            "simple_dl_url",
            &simple_dl_url
        );

        stringMap.insert(
            "warp_private_key",
            &warp_private_key
        );

        stringMap.insert(
            "warp_public_key",
            &warp_public_key
        );

        stringMap.insert(
            "warp_ep",
            &warp_ep
        );

        stringMap.insert(
            "inbound_user",
            &inbound_user
        );

        stringMap.insert(
            "inbound_pass",
            &inbound_pass
        );

        stringMap.insert(
            "url_scheme_mirror",
            &url_scheme_mirror
        );


        // -----------------------------------------------------
        // QStringList
        // -----------------------------------------------------

        stringListMap.insert(
            "dns_server_rules",
            &dns_server_rules
        );

        stringListMap.insert(
            "extra_core_paths",
            &extraCorePaths
        );

        stringListMap.insert(
            "log_include_keyword",
            &log_include_keyword
        );

        stringListMap.insert(
            "log_include_regex",
            &log_include_regex
        );

        stringListMap.insert(
            "log_exclude_keyword",
            &log_exclude_keyword
        );

        stringListMap.insert(
            "log_exclude_regex",
            &log_exclude_regex
        );

        stringListMap.insert(
            "warp_ifc_addrs",
            &warp_ifc_addrs
        );

        stringListMap.insert(
            "dial_bind_ifc_history",
            &dial_bind_interface_history
        );

        stringListMap.insert(
            "dial_inet4_bind_history",
            &dial_inet4_bind_address_history
        );

        stringListMap.insert(
            "dial_inet6_bind_history",
            &dial_inet6_bind_address_history
        );
    }


    // =========================================================
    // Database initialization
    // =========================================================

    bool SettingsRepo::createTables() const
    {
        return db.exec(
            R"(
        CREATE TABLE IF NOT EXISTS settings
        (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        )
        )"
        );
    }

    // =========================================================
    // Load
    // =========================================================

    void SettingsRepo::loadAllSettings()
    {
        auto query =
            db.query(
                "SELECT key, value "
                "FROM settings"
            );


        if (!query)
        {
            return;
        }


        while (query->executeStep())
        {
            const QString key =
                QString::fromStdString(
                    query
                    ->getColumn(0)
                    .getText()
                );


            const QString str =
                QString::fromStdString(
                    query
                    ->getColumn(1)
                    .getText()
                );


            // -------------------------------------------------
            // bool
            // -------------------------------------------------

            if (auto it = boolMap.find(key);
                it != boolMap.end())
            {
                *it.value() =
                    str == "true" ||
                    str == "1";

                continue;
            }


            // -------------------------------------------------
            // int
            // -------------------------------------------------

            if (auto it = intMap.find(key);
                it != intMap.end())
            {
                bool ok = false;

                const int value =
                    str.toInt(
                        &ok
                    );


                *it.value() =
                    ok
                    ? value
                    : 0;

                continue;
            }


            // -------------------------------------------------
            // QStringList
            // -------------------------------------------------

            if (auto it = stringListMap.find(key);
                it != stringListMap.end())
            {
                const QJsonDocument doc =
                    QJsonDocument::fromJson(
                        str.toUtf8()
                    );


                if (doc.isArray())
                {
                    QStringList list;


                    for (const QJsonValue& value :
                        doc.array())
                    {
                        list.append(
                            value.toString()
                        );
                    }


                    *it.value() =
                        list;
                }


                continue;
            }


            // -------------------------------------------------
            // QString
            // -------------------------------------------------

            if (auto it = stringMap.find(key);
                it != stringMap.end())
            {
                *it.value() =
                    str;

                continue;
            }


            // -------------------------------------------------
            // Shortcuts
            // -------------------------------------------------

            if (key == "shortcuts")
            {
                const QJsonDocument doc =
                    QJsonDocument::fromJson(
                        str.toUtf8()
                    );


                if (doc.isObject())
                {
                    const QJsonObject object =
                        doc.object();


                    for (const QString& shortcutKey :
                        object.keys())
                    {
                        shortcuts[
                            shortcutKey
                        ] =
                            QKeySequence(
                                object[
                                    shortcutKey
                                ].toString()
                                        );
                    }
                }


                continue;
            }


            // -------------------------------------------------
            // Xray VLESS preference
            // -------------------------------------------------

            if (key ==
                "xray_vless_preference")
            {
                bool ok = false;


                const int value =
                    str.toInt(
                        &ok
                    );


                xray_vless_preference =
                    static_cast<
                    Xray::XrayVlessPreference
                    >(
                        ok
                        ? value
                        : 0
                        );
            }
        }
    }


    // =========================================================
    // Save
    // =========================================================

    bool SettingsRepo::saveAllSettings() const
    {
        if (noSave)
        {
            // noSave is an intentional "do not write" mode,
            // not a database failure.
            return true;
        }


        std::vector<
            std::pair<
            std::string,
            std::string
            >
        > keyValues;


        keyValues.reserve(
            boolMap.size()
            +
            intMap.size()
            +
            stringMap.size()
            +
            stringListMap.size()
            +
            2
        );


        // -----------------------------------------------------
        // bool
        // -----------------------------------------------------

        for (auto it = boolMap.cbegin();
            it != boolMap.cend();
            ++it)
        {
            keyValues.emplace_back(
                it.key().toStdString(),

                *it.value()
                ? "true"
                : "false"
            );
        }


        // -----------------------------------------------------
        // int
        // -----------------------------------------------------

        for (auto it = intMap.cbegin();
            it != intMap.cend();
            ++it)
        {
            keyValues.emplace_back(
                it.key().toStdString(),

                QString::number(
                    *it.value()
                )
                .toStdString()
            );
        }


        // -----------------------------------------------------
        // QString
        // -----------------------------------------------------

        for (auto it = stringMap.cbegin();
            it != stringMap.cend();
            ++it)
        {
            keyValues.emplace_back(
                it.key().toStdString(),
                it.value()->toStdString()
            );
        }


        // -----------------------------------------------------
        // QStringList
        // -----------------------------------------------------

        for (auto it = stringListMap.cbegin();
            it != stringListMap.cend();
            ++it)
        {
            QJsonArray array;


            for (const QString& value :
                *it.value())
            {
                array.append(
                    value
                );
            }


            const QByteArray json =
                QJsonDocument(
                    array
                )
                .toJson(
                    QJsonDocument::Compact
                );


            keyValues.emplace_back(
                it.key().toStdString(),

                QString::fromUtf8(
                    json
                )
                .toStdString()
            );
        }


        // -----------------------------------------------------
        // Shortcuts
        // -----------------------------------------------------

        {
            QJsonObject object;


            for (auto it = shortcuts.cbegin();
                it != shortcuts.cend();
                ++it)
            {
                object[
                    it.key()
                ] =
                    it.value()
                        .toString();
            }


            const QByteArray json =
                QJsonDocument(
                    object
                )
                .toJson(
                    QJsonDocument::Compact
                );


            keyValues.emplace_back(
                "shortcuts",

                QString::fromUtf8(
                    json
                )
                .toStdString()
            );
        }


        // -----------------------------------------------------
        // Xray VLESS preference
        // -----------------------------------------------------

        keyValues.emplace_back(
            "xray_vless_preference",

            QString::number(
                static_cast<int>(
                    xray_vless_preference
                    )
            )
            .toStdString()
        );


        return db.execBatchSettingsReplace(
            keyValues
        );
    }

    bool SettingsRepo::saveSingleSetting(
        const QString& key,
        const QString& value) const
    {
        // =========================================================
        // Validation
        // =========================================================

        if (key.isEmpty())
        {
            MW_show_log(
                "SettingsRepo::saveSingleSetting: "
                "empty setting key"
            );

            return false;
        }


        // =========================================================
        // noSave is an intentional mode.
        //
        // Keep the same semantic contract as saveAllSettings():
        // skipping the physical DB write is not considered an error.
        // =========================================================

        if (noSave)
        {
            return true;
        }


        // =========================================================
        // Write exactly ONE setting.
        //
        // UPSERT is preferable to INSERT OR REPLACE because REPLACE
        // internally has DELETE + INSERT semantics.
        // =========================================================

        const bool persisted =
            db.exec(
                R"(
            INSERT INTO settings
            (
                key,
                value
            )
            VALUES
            (
                ?,
                ?
            )

            ON CONFLICT(key)
            DO UPDATE SET
                value = excluded.value
            )",

                key.toStdString(),
                value.toStdString()
            );


        if (!persisted)
        {
            MW_show_log(
                "SettingsRepo::saveSingleSetting: "
                "failed to persist setting '"
                +
                key
                +
                "'"
            );

            return false;
        }


        return true;
    }

    // =========================================================
    // Runtime state
    // =========================================================

    bool SettingsRepo::UpdateStartedId(
        int id)
    {
        // =========================================================
        // Runtime state.
        //
        // This value describes what is actually happening RIGHT NOW
        // and is intentionally not stored in SQLite.
        //
        // Therefore it must be changed regardless of whether the
        // persistent remember_id write succeeds.
        // =========================================================

        started_id =
            id;


        // =========================================================
        // Persist remember_id FIRST.
        //
        // Do not publish the new remember_id into SettingsRepo memory
        // until SQLite has accepted it.
        //
        // This gives us:
        //
        //      success:
        //          DB == memory
        //
        //      failure:
        //          old remember_id remains both in DB and memory
        //
        // while started_id still correctly describes runtime state.
        // =========================================================

        const bool persisted =
            saveSingleSetting(
                QStringLiteral("remember_id"),
                QString::number(id)
            );


        if (!persisted)
        {
            MW_show_log(
                "SettingsRepo::UpdateStartedId: "
                "started_id was updated to "
                +
                QString::number(id)
                +
                ", but remember_id could not be persisted"
            );


            return false;
        }


        // =========================================================
        // SQLite accepted the new remembered Profile ID.
        //
        // Publish persistent state into memory only now.
        // =========================================================

        remember_id =
            id;


        return true;
    }


    // =========================================================
    // User agent
    // =========================================================

    namespace
    {
        QString SubStrBefore(
            const QString& str,
            const QString& sub)
        {
            if (!str.contains(sub))
            {
                return str;
            }


            return str.left(
                str.indexOf(
                    sub
                )
            );
        }
    }


    QString SettingsRepo::GetUserAgent(
        bool isDefault) const
    {
        if (user_agent.isEmpty())
        {
            isDefault =
                true;
        }


        if (isDefault)
        {
            QString version =
                SubStrBefore(
                    NKR_VERSION,
                    "-"
                );


            if (!version.contains("."))
            {
                version =
                    "1.0.0";
            }


            return
                "Gryph/"
                +
                version;
        }


        return user_agent;
    }


    // =========================================================
    // Public save
    // =========================================================
    bool SettingsRepo::Save()
    {
        const bool persisted =
            saveAllSettings();


        if (!persisted)
        {
            MW_show_log(
                "SettingsRepo::Save: "
                "failed to persist settings"
            );

            return false;
        }


        return true;
    }

    // =========================================================
    // Extra cores
    // =========================================================

    QStringList
        SettingsRepo::GetExtraCorePaths() const
    {
        return extraCorePaths;
    }


    bool SettingsRepo::AddExtraCorePath(
        const QString& path)
    {
        if (extraCorePaths.contains(
            path))
        {
            return false;
        }


        extraCorePaths.append(
            path
        );


        return true;
    }

} // namespace Configs