#include "include/configs/Generate.h"
#include "include/api/RPC.h"
#include "include/global/Configs.hpp"

#include <QApplication>
#include <QFileInfo>

#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/RoutesRepo.h"

#include "include/database/entities/Profile.h"
#include "include/sys/linux/systemChecks.h"

#include <algorithm>
#include <string_view>

namespace {
    // Single binary search over the sorted ruleSetList — replaces the
    // ruleSetMap.contains + ruleSetMap.at lookups from the former std::map.
    std::string_view ruleSetUrl(std::string_view key) {
        auto it = std::lower_bound(ruleSetList.begin(), ruleSetList.end(), key,
            [](const auto& e, std::string_view k) { return e.first < k; });
        return (it != ruleSetList.end() && it->first == key) ? it->second : std::string_view{};
    }
}

namespace Configs {

    QString genTunName() {
        auto tun_name = "Gryph-tun";
#ifdef Q_OS_MACOS
        tun_name = "";
#endif
        return tun_name;
    }

    void MergeJson(const QJsonObject& custom, QJsonObject& outbound) {
        if (custom.isEmpty()) return;
        for (const auto& key : custom.keys()) {
            if (outbound.contains(key)) {
                auto v = custom[key];
                auto v_orig = outbound[key];
                if (v.isObject() && v_orig.isObject()) {
                    auto vo = v.toObject();
                    QJsonObject vo_orig = v_orig.toObject();
                    MergeJson(vo, vo_orig);
                    outbound[key] = vo_orig;
                }
                else {
                    outbound[key] = v;
                }
            }
            else {
                outbound[key] = custom[key];
            }
        }
    }

    QStringList getChainDomains(
        const std::shared_ptr<Profile>& ent,
        QString& error)
    {
        QStringList domains;

        if (!ent)
        {
            error =
                "Null profile in getChainDomains";
            return domains;
        }

        const auto chain =
            ent->OutboundCloneAs<Configs::chain>();

        if (!chain)
        {
            error =
                "Ent is nullptr after cast to chain "
                "in getChainDomains, data is corrupted";
            return domains;
        }

        for (const int id : chain->list)
        {
            const auto subEnt =
                Configs::dataManager
                ->profilesRepo
                ->GetProfile(id);

            if (!subEnt)
            {
                continue;
            }

            const auto outbound =
                subEnt->OutboundClone();

            if (!outbound)
            {
                continue;
            }

            if (outbound->IsExtraCore())
            {
                continue;
            }

            const QString address =
                outbound->GetAddress();

            if (!address.isEmpty() &&
                !IsIpAddress(address))
            {
                domains.append(address);
            }
        }

        return domains;
    }


    QStringList getEntDomains(
        const QList<int>& entIDs,
        QString& error)
    {
        QStringList domains;

        for (const int id : entIDs)
        {
            const auto ent =
                Configs::dataManager
                ->profilesRepo
                ->GetProfile(id);

            if (!ent)
            {
                continue;
            }

            const auto outbound =
                ent->OutboundClone();

            if (!outbound)
            {
                continue;
            }

            if (outbound->IsExtraCore())
            {
                continue;
            }

            if (ent->Type() == "chain")
            {
                domains << getChainDomains(
                    ent,
                    error
                );

                if (!error.isEmpty())
                {
                    return domains;
                }

                continue;
            }

            const QString address =
                outbound->GetAddress();

            if (!address.isEmpty() &&
                !IsIpAddress(address))
            {
                domains.append(address);
            }
        }

        return domains;
    }


    std::shared_ptr<Profile> getWarpProfile()
    {
        auto warpProfile =
            ProfilesRepo::NewProfile(
                "wireguard"
            );

        if (!warpProfile)
        {
            return nullptr;
        }

        const QString warpEndpoint =
            dataManager
            ->settingsRepo
            ->warp_ep;

        const bool configured =
            warpProfile
            ->MutateOutbound<
            Configs::wireguard
            >(
                [&](Configs::wireguard& outbound) -> bool
                {
                    outbound.name =
                        "warp";

                    outbound.server =
                        warpEndpoint.contains(":")
                        ? SubStrBefore(
                            warpEndpoint,
                            ":"
                        )
                        : warpEndpoint;

                    outbound.server_port =
                        warpEndpoint.contains(":")
                        ? SubStrAfter(
                            warpEndpoint,
                            ":"
                        ).toInt()
                        : 2408;

                    outbound.private_key =
                        dataManager
                        ->settingsRepo
                        ->warp_private_key;

                    outbound.address =
                        dataManager
                        ->settingsRepo
                        ->warp_ifc_addrs;

                    auto peer =
                        std::make_shared<Peer>();

                    peer->public_key =
                        dataManager
                        ->settingsRepo
                        ->warp_public_key;

                    peer->address =
                        outbound.server;

                    peer->port =
                        outbound.server_port;

                    outbound.peer =
                        std::move(peer);

                    outbound.mtu =
                        1280;

                    return true;
                }
            );

        if (!configured)
        {
            return nullptr;
        }

        // Synthetic profile; it is never persisted.
        warpProfile->LoadIdentity(
            warpProfileID,
            -1
        );

        return warpProfile;
    }


    void CalculatePrerequisities(
        std::shared_ptr<BuildSingBoxConfigContext>& ctx)
    {
        if (!ctx ||
            !ctx->ent)
        {
            if (ctx)
            {
                ctx->error =
                    "Cannot calculate prerequisites for a null profile";
            }
            return;
        }

        ctx->tunEnabled =
            Configs::dataManager
            ->settingsRepo
            ->spmode_vpn;

        ctx->os =
            getOS();

        if (ctx->os == Linux)
        {
            ctx->isResolvedUsed =
                isSystemdResolvedDefaultResolver();
        }

        auto preReqs =
            ctx->buildPrerequisities;

        if (!preReqs)
        {
            ctx->error =
                "Build prerequisites are not initialized";
            return;
        }

        auto routeChain =
            Configs::dataManager
            ->routesRepo
            ->GetRouteProfile(
                Configs::dataManager
                ->settingsRepo
                ->current_route_id
            );

        if (!routeChain)
        {
            ctx->error =
                "Routing profile does not exist, "
                "try resetting the route profile in Routing Settings";
            return;
        }

        auto neededOutbounds =
            routeChain->get_used_outbounds();

        auto neededRuleSets =
            routeChain->get_used_rule_sets();

        preReqs->routingDeps->defaultOutboundID =
            routeChain->defaultOutboundID;

        preReqs->routingDeps->outboundMap[-1] =
            "proxy";

        preReqs->routingDeps->outboundMap[-2] =
            "direct";

        preReqs->routingDeps->outboundMap[warpBypassID] =
            dataManager->settingsRepo->enable_warp
            ? "warp-bypass"
            : "proxy";

        int suffix = 0;

        const auto isCustomFullConfig =
            [](const std::shared_ptr<Profile>& profile)
            {
                if (!profile ||
                    profile->Type() != "custom")
                {
                    return false;
                }

                const auto custom =
                    profile->OutboundCloneAs<Configs::Custom>();

                return
                    custom &&
                    custom->type ==
                    Custom::CustomFullConfig;
            };

        const auto isXrayFullConfig =
            [](const std::shared_ptr<Profile>& profile)
            {
                if (!profile)
                {
                    return false;
                }

                const auto outbound =
                    profile->OutboundClone();

                return
                    outbound &&
                    outbound->IsXrayFullConfig();
            };

        if (neededOutbounds)
        {
            for (const int item : *neededOutbounds)
            {
                if (item < 0)
                {
                    continue;
                }

                const auto neededEnt =
                    Configs::dataManager
                    ->profilesRepo
                    ->GetProfile(item);

                if (!neededEnt)
                {
                    ctx->error =
                        "The routing profile is referencing "
                        "outbounds that no longer exist, "
                        "consider revising your settings";
                    return;
                }

                const auto neededOutbound =
                    neededEnt->OutboundClone();

                if (!neededOutbound)
                {
                    ctx->error =
                        "Routing profile references a profile "
                        "with a null outbound";
                    return;
                }

                if (neededOutbound->IsExtraCore() ||
                    isCustomFullConfig(neededEnt) ||
                    isXrayFullConfig(neededEnt))
                {
                    ctx->error =
                        "Outbounds used in routing profile cannot "
                        "use an extra core or be a custom full config";
                    return;
                }

                if (neededEnt->Type() == "chain")
                {
                    const auto chain =
                        neededEnt->OutboundCloneAs<Configs::chain>();

                    if (!chain ||
                        chain->list.isEmpty())
                    {
                        ctx->error =
                            "Chain outbound in routing profile "
                            "is empty or corrupted";
                        return;
                    }

                    for (const int hopID : chain->list)
                    {
                        const auto hopEnt =
                            Configs::dataManager
                            ->profilesRepo
                            ->GetProfile(hopID);

                        if (!hopEnt)
                        {
                            ctx->error =
                                "Chain outbound in routing profile "
                                "contains a missing profile";
                            return;
                        }

                        const auto hopOutbound =
                            hopEnt->OutboundClone();

                        if (!hopOutbound)
                        {
                            ctx->error =
                                "Chain outbound in routing profile "
                                "contains a null outbound";
                            return;
                        }

                        if (hopOutbound->IsExtraCore() ||
                            isCustomFullConfig(hopEnt) ||
                            isXrayFullConfig(hopEnt) ||
                            hopEnt->Type() == "chain")
                        {
                            ctx->error =
                                "Chain hops in routing profile cannot "
                                "use an extra core, a custom full config, "
                                "or be of type chain";
                            return;
                        }

                        const auto addrs =
                            getEntDomains(
                                { hopID },
                                ctx->error
                            );

                        if (!ctx->error.isEmpty())
                        {
                            return;
                        }

                        for (const auto& addr : addrs)
                        {
                            preReqs
                                ->dnsDeps
                                ->directDomains
                                << addr;
                        }

                        if (!addrs.isEmpty())
                        {
                            preReqs
                                ->dnsDeps
                                ->needDirectDnsRules =
                                true;
                        }
                    }

                    preReqs
                        ->routingDeps
                        ->outboundMap[item] =
                        "route-" +
                        Int2String(suffix);

                    QList<int> reversedHops;

                    reversedHops.reserve(
                        chain->list.size()
                    );

                    for (int idx =
                        chain->list.size() - 1;
                        idx >= 0;
                        --idx)
                    {
                        reversedHops <<
                            chain->list[idx];
                    }

                    preReqs
                        ->routingDeps
                        ->routeOutboundGroups
                        << RoutingDeps::RouteOutboundGroup{
                            reversedHops,
                            neededEnt
                    };

                    suffix +=
                        chain->list.size();
                }
                else
                {
                    const auto entAddrs =
                        getEntDomains(
                            { neededEnt->Id() },
                            ctx->error
                        );

                    if (!ctx->error.isEmpty())
                    {
                        return;
                    }

                    for (const auto& addr : entAddrs)
                    {
                        preReqs
                            ->dnsDeps
                            ->directDomains
                            << addr;
                    }

                    if (!entAddrs.isEmpty())
                    {
                        preReqs
                            ->dnsDeps
                            ->needDirectDnsRules =
                            true;
                    }

                    preReqs
                        ->routingDeps
                        ->outboundMap[item] =
                        "route-" +
                        Int2String(suffix++);

                    preReqs
                        ->routingDeps
                        ->routeOutboundGroups
                        << RoutingDeps::RouteOutboundGroup{
                            QList<int>{item},
                            nullptr
                    };
                }
            }
        }

        if (neededRuleSets)
        {
            for (const auto& item : *neededRuleSets)
            {
                preReqs
                    ->routingDeps
                    ->neededRuleSets
                    << item;
            }
        }

        if (dataManager
            ->settingsRepo
            ->enable_dns_routing)
        {
            const auto sets =
                routeChain->get_direct_sites();

            for (const auto& item : sets)
            {
                if (item.startsWith("ruleset:"))
                    preReqs->dnsDeps->directRuleSets << item.mid(8);

                if (item.startsWith("domain:"))
                    preReqs->dnsDeps->directDomains << item.mid(7);

                if (item.startsWith("suffix:"))
                    preReqs->dnsDeps->directSuffixes << item.mid(7);

                if (item.startsWith("keyword:"))
                    preReqs->dnsDeps->directKeywords << item.mid(8);

                if (item.startsWith("regex:"))
                    preReqs->dnsDeps->directRegexes << item.mid(6);

                preReqs
                    ->dnsDeps
                    ->needDirectDnsRules =
                    true;
            }

            const auto proxySets =
                routeChain->get_proxy_sites();

            for (const auto& item : proxySets)
            {
                if (item.startsWith("ruleset:"))
                    preReqs->dnsDeps->proxyRuleSets << item.mid(8);

                if (item.startsWith("domain:"))
                    preReqs->dnsDeps->proxyDomains << item.mid(7);

                if (item.startsWith("suffix:"))
                    preReqs->dnsDeps->proxySuffixes << item.mid(7);

                if (item.startsWith("keyword:"))
                    preReqs->dnsDeps->proxyKeywords << item.mid(8);

                if (item.startsWith("regex:"))
                    preReqs->dnsDeps->proxyRegexes << item.mid(6);

                preReqs
                    ->dnsDeps
                    ->needProxyDnsRules =
                    true;
            }
        }

        const auto entAddrs =
            getEntDomains(
                { ctx->ent->Id() },
                ctx->error
            );

        if (!ctx->error.isEmpty())
        {
            return;
        }

        for (const auto& addr : entAddrs)
        {
            preReqs
                ->dnsDeps
                ->directDomains
                << addr;
        }

        if (!entAddrs.isEmpty())
        {
            preReqs
                ->dnsDeps
                ->needDirectDnsRules =
                true;
        }

        const auto group =
            Configs::dataManager
            ->groupsRepo
            ->GetGroup(
                ctx->ent->GroupId()
            );

        if (group)
        {
            const auto groupSnapshot =
                group->Snapshot();

            QList<int> groupEnts;

            if (groupSnapshot.front_proxy_id >= 0)
            {
                groupEnts <<
                    groupSnapshot.front_proxy_id;
            }

            if (groupSnapshot.landing_proxy_id >= 0)
            {
                groupEnts <<
                    groupSnapshot.landing_proxy_id;
            }

            const auto addrs =
                getEntDomains(
                    groupEnts,
                    ctx->error
                );

            if (!ctx->error.isEmpty())
            {
                return;
            }

            for (const auto& addr : addrs)
            {
                preReqs
                    ->dnsDeps
                    ->directDomains
                    << addr;
            }
        }

        if (Configs::dataManager
            ->settingsRepo
            ->enable_dns_server)
        {
            for (const auto& rule :
                Configs::dataManager
                ->settingsRepo
                ->dns_server_rules)
            {
                if (rule.startsWith("ruleset:"))
                    preReqs->hijackDeps->hijackGeoAssets << rule.mid(8);

                if (rule.startsWith("domain:"))
                    preReqs->hijackDeps->hijackDomains << rule.mid(7);

                if (rule.startsWith("suffix:"))
                    preReqs->hijackDeps->hijackDomainSuffix << rule.mid(7);

                if (rule.startsWith("regex:"))
                    preReqs->hijackDeps->hijackDomainRegex << rule.mid(6);
            }
        }

        for (const auto& ruleSet :
            preReqs->hijackDeps->hijackGeoAssets)
        {
            if (!preReqs
                ->routingDeps
                ->neededRuleSets
                .contains(
                    ruleSet.toString()
                ))
            {
                preReqs
                    ->routingDeps
                    ->neededRuleSets
                    .append(
                        ruleSet.toString()
                    );
            }
        }

        const auto directIPraw =
            routeChain->get_direct_ips();

        for (const auto& item : directIPraw)
        {
            if (item.startsWith("ruleset:"))
                preReqs->tunDeps->directIPSets << item.mid(8);

            if (item.startsWith("ip:"))
                preReqs->tunDeps->directIPCIDRs << item.mid(3);
        }

        std::shared_ptr<Profile> extraCoreEnt;

        const auto rootOutbound =
            ctx->ent->OutboundClone();

        if (rootOutbound &&
            rootOutbound->IsExtraCore())
        {
            extraCoreEnt =
                ctx->ent;
        }
        else if (ctx->ent->Type() == "chain")
        {
            const auto chain =
                ctx->ent->OutboundCloneAs<Configs::chain>();

            if (chain &&
                !chain->list.isEmpty())
            {
                const auto firstEnt =
                    Configs::dataManager
                    ->profilesRepo
                    ->GetProfile(
                        chain->list[0]
                    );

                if (firstEnt)
                {
                    const auto firstOutbound =
                        firstEnt->OutboundClone();

                    if (firstOutbound &&
                        firstOutbound->IsExtraCore())
                    {
                        extraCoreEnt =
                            firstEnt;
                    }
                }
            }
        }

        if (extraCoreEnt)
        {
            const auto outbound =
                extraCoreEnt->OutboundCloneAs<Configs::extracore>();

            if (!outbound)
            {
                MW_show_log(
                    "INVALID ENT TYPE, "
                    "NEEDED EXTRACORE GOT NULLPTR"
                );

                ctx->error =
                    "failed to cast to extracore, type is: "
                    + extraCoreEnt->Type();
                return;
            }

            ctx->buildConfigResult
                ->extraCoreData
                ->path =
                QFileInfo(
                    outbound->extraCorePath
                ).canonicalFilePath();

            ctx->buildConfigResult
                ->extraCoreData
                ->args =
                outbound->extraCoreArgs;

            ctx->buildConfigResult
                ->extraCoreData
                ->config =
                outbound->extraCoreConf;

            ctx->buildConfigResult
                ->extraCoreData
                ->noLog =
                outbound->noLogs;
        }
    }


    void buildLogSections(std::shared_ptr<BuildSingBoxConfigContext>& ctx) {
        ctx->buildConfigResult->coreConfig.insert("log", QJsonObject{ {"level", Configs::dataManager->settingsRepo->log_level} });
    }

    void buildNTPSection(std::shared_ptr<BuildSingBoxConfigContext>& ctx) {
        if (Configs::dataManager->settingsRepo->enable_ntp) {
            QJsonObject ntpObj;
            ntpObj["enabled"] = true;
            ntpObj["server"] = Configs::dataManager->settingsRepo->ntp_server_address;
            ntpObj["server_port"] = Configs::dataManager->settingsRepo->ntp_server_port;
            ntpObj["interval"] = Configs::dataManager->settingsRepo->ntp_interval;
            ctx->buildConfigResult->coreConfig["ntp"] = ntpObj;
        }
    }

    void buildCertificateSection(std::shared_ptr<BuildSingBoxConfigContext>& ctx) {
        ctx->buildConfigResult->coreConfig.insert("certificate", QJsonObject{ {"store", Configs::dataManager->settingsRepo->use_mozilla_certs ? "mozilla" : "system"} });
    }

    QJsonObject buildDnsObj(QString address, std::shared_ptr<BuildSingBoxConfigContext>& ctx) {
        if (address.startsWith("local")) {
            if (ctx->tunEnabled && ctx->isResolvedUsed) {
                return { {"type", "underlying"} };
            }
            if (ctx->tunEnabled && ctx->os == Darwin) {
                return {
                    {"type", "udp"},
                    {"server", Configs::dataManager->settingsRepo->core_box_underlying_dns}
                };
            }
            return { {"type", "local"} };
        }
        if (address.startsWith("dhcp://")) {
            auto ifcName = address.replace("dhcp://", "");
            if (ifcName == "auto") ifcName = "";
            return {
                {"type", "dhcp"},
                {"interface", ifcName},
            };
        }
        QString addr = address;
        int port = -1;
        QString type = "udp";
        QString path = "";
        if (address.startsWith("tcp://")) {
            type = "tcp";
            addr = addr.replace("tcp://", "");
        }
        if (address.startsWith("tls://")) {
            type = "tls";
            addr = addr.replace("tls://", "");
        }
        if (address.startsWith("quic://")) {
            type = "quic";
            addr = addr.replace("quic://", "");
        }
        if (address.startsWith("https://")) {
            type = "https";
            addr = addr.replace("https://", "");
            auto slashIndex = addr.indexOf("/");
            if (slashIndex != -1) {
                path = addr.mid(slashIndex);
                addr = addr.left(slashIndex);
            }
        }
        if (address.startsWith("h3://")) {
            type = "h3";
            addr = addr.replace("h3://", "");
            auto slashIndex = addr.indexOf("/");
            if (slashIndex != -1) {
                path = addr.mid(slashIndex);
                addr = addr.left(slashIndex);
            }
        }
        if (addr.contains(":")) {
            auto spl = addr.split(":");
            addr = spl[0];
            port = spl[1].toInt();
        }
        QJsonObject res = {
            {"type", type},
            {"server", addr},
        };
        if (port != -1) res["server_port"] = port;
        if (!path.isEmpty()) res["path"] = path;
        return res;
    }

    void buildDNSSection(std::shared_ptr<BuildSingBoxConfigContext>& ctx, bool useDnsObj) {
        if (getOS() == Darwin && Configs::dataManager->settingsRepo->core_box_underlying_dns.isEmpty() && Configs::dataManager->settingsRepo->spmode_vpn)
        {
            ctx->error = QObject::tr("Local DNS and Tun mode do not work together, please set an IP to be used as the Local DNS server in the Routing Settings -> Local override");
            return;
        }

        if (Configs::dataManager->settingsRepo->use_dns_object && useDnsObj) {
            ctx->buildConfigResult->coreConfig["dns"] = QString2QJsonObject(Configs::dataManager->settingsRepo->dns_object);
            return;
        }

        auto dnsDeps = ctx->buildPrerequisities->dnsDeps;
        auto hijackDeps = ctx->buildPrerequisities->hijackDeps;
        bool isTailscale = ctx->ent && ctx->ent->Type() == "tailscale";
        bool independentCache = false;
        QJsonArray servers;
        QJsonArray rules;
        // remote
        if (!ctx->forTest) {
            auto remoteDnsObj = buildDnsObj(Configs::dataManager->settingsRepo->remote_dns, ctx);
            remoteDnsObj["tag"] = "dns-remote";
            remoteDnsObj["domain_resolver"] = "dns-local";
            remoteDnsObj["detour"] = "proxy";
            servers += remoteDnsObj;

            if (isTailscale)
            {
                auto tailscale = ctx->ent->OutboundCloneAs<Configs::tailscale>();
                if (tailscale != nullptr)
                {
                    // Add an additional DNS server for Tailscale MagicDNS
                    servers += QJsonObject{
                        {"type", "tailscale"},
                        {"tag", "dns-tailscale"},
                        {"endpoint", "proxy"},
                        {"accept_default_resolvers", tailscale->globalDNS},
                    };

                    // Route Tailscale internal domains to MagicDNS
                    rules.prepend(QJsonObject{
                        {"domain_suffix", QJsonArray{"ts.net", "tailscale.net"}},
                        {"action", "route"},
                        {"server", "dns-tailscale"},
                        });
                }

                // Add direct bootstrap rules for tailscale control plane and services
                rules.prepend(QJsonObject{
                    {"domain", QJsonArray{
                        "controlplane.tailscale.com",
                        "login.tailscale.com",
                        "log.tailscale.io"
                    }},
                    {"domain_suffix", QJsonArray{
                        "tailscale.com",
                        "tailscale.net",
                        "tailscale.io"
                    }},
                    {"action", "route"},
                    {"server", "dns-direct"},
                    });
            }
        }

        // direct
        auto directDnsObj = buildDnsObj(Configs::dataManager->settingsRepo->direct_dns, ctx);
        directDnsObj["tag"] = "dns-direct";
        directDnsObj["domain_resolver"] = "dns-local";
        servers.append(directDnsObj);

        // Handle localhost
        if (!ctx->forTest) {
            rules += QJsonObject{
                    {"domain", "localhost"},
                    {"action", "predefined"},
                    {"query_type", "A"},
                    {"rcode", "NOERROR"},
                    {"answer", "localhost. IN A 127.0.0.1"},
            };

            rules += QJsonObject{
                    {"domain", "localhost"},
                    {"action", "predefined"},
                    {"query_type", "AAAA"},
                    {"rcode", "NOERROR"},
                    {"answer", "localhost. IN AAAA ::1"},
            };
        }

        // process_path matching forces sing-box's process finder, which is very
        // heavy on Windows (large latency spikes). It's only needed to keep an
        // extra core's egress out of the proxy/TUN loop — sing-box's own egress
        // is handled by auto_detect_interface and xray by its loopback bridges —
        // so only emit the direct-DNS carve-out when an extra core is present.
        if (!ctx->forTest && !ctx->buildConfigResult->extraCoreData->path.isEmpty())
        {
            QJsonArray coreProcessPaths;
            auto extraCorePath = ctx->buildConfigResult->extraCoreData->path;
#ifdef Q_OS_WIN
            extraCorePath.replace("/", "\\");
#endif
            coreProcessPaths.append(extraCorePath);
            rules += QJsonObject{
                {"process_path", coreProcessPaths},
                {"action", "route"},
                {"strategy", dataManager->settingsRepo->direct_dns_strategy},
                {"server", "dns-direct"},
            };
        }

        // HijackRules
        if (Configs::dataManager->settingsRepo->enable_dns_server && !ctx->forTest)
        {
            rules += QJsonObject{
                        {"rule_set", hijackDeps->hijackGeoAssets},
                        {"domain", hijackDeps->hijackDomains},
                        {"domain_suffix", hijackDeps->hijackDomainSuffix},
                        {"domain_regex", hijackDeps->hijackDomainRegex},
                        {"query_type", "A"},
                        {"action", "predefined"},
                        {"rcode", "NOERROR"},
                        {"answer", QString("* IN A %1").arg(Configs::dataManager->settingsRepo->dns_v4_resp)},
            };

            if (!Configs::dataManager->settingsRepo->dns_v6_resp.isEmpty())
            {
                rules += QJsonObject{
                            {"rule_set", hijackDeps->hijackGeoAssets},
                            {"domain", hijackDeps->hijackDomains},
                            {"domain_suffix", hijackDeps->hijackDomainSuffix},
                            {"domain_regex", hijackDeps->hijackDomainRegex},
                            {"query_type", "AAAA"},
                            {"action", "predefined"},
                            {"rcode", "NOERROR"},
                            {"answer", QString("* IN AAAA %1").arg(Configs::dataManager->settingsRepo->dns_v6_resp)},
                };
            }
        }

        // FakeIP
        if (Configs::dataManager->settingsRepo->fake_dns) {
            servers += QJsonObject{
                    {"tag", "dns-fake"},
                    {"type", "fakeip"},
                    {"inet4_range", "198.18.0.0/15"},
                    {"inet6_range", "fc00::/18"},
            };
            rules += QJsonObject{
                    {"query_type", QJsonArray{
                        "A",
                        "AAAA"
                    }},
                 {"action", "route"},
                 {"server", "dns-fake"}
            };
            independentCache = true;
        }

        if (dnsDeps->needDirectDnsRules) {
            rules += QJsonObject{
                    {"rule_set", dnsDeps->directRuleSets},
                    {"domain", dnsDeps->directDomains},
                    {"domain_suffix", dnsDeps->directSuffixes},
                    {"domain_keyword", dnsDeps->directKeywords},
                    {"domain_regex", dnsDeps->directRegexes},
                    {"action", "route"},
                    {"strategy", dataManager->settingsRepo->direct_dns_strategy},
                    {"server", "dns-direct"},
            };
        }

        const bool useDirectFinalDNS = dataManager->settingsRepo->dns_final_out == "direct";

        // Symmetric to the direct carve-out above: when the final DNS is direct,
        // proxy-routed sites would otherwise resolve via direct DNS, so route
        // them to remote DNS. "proxy" and "remote" both use remote DNS; only
        // explicit "direct" should fall back to local DNS.
        if (dnsDeps->needProxyDnsRules && useDirectFinalDNS) {
            rules += QJsonObject{
                    {"rule_set", dnsDeps->proxyRuleSets},
                    {"domain", dnsDeps->proxyDomains},
                    {"domain_suffix", dnsDeps->proxySuffixes},
                    {"domain_keyword", dnsDeps->proxyKeywords},
                    {"domain_regex", dnsDeps->proxyRegexes},
                    {"action", "route"},
                    {"strategy", dataManager->settingsRepo->remote_dns_strategy},
                    {"server", "dns-remote"},
            };
        }

        // final rule: proxy
        auto finalStrategy = useDirectFinalDNS ? dataManager->settingsRepo->direct_dns_strategy : dataManager->settingsRepo->remote_dns_strategy;
        auto finalDNS = useDirectFinalDNS ? "dns-direct" : "dns-remote";
        rules += QJsonObject{
            {"strategy", finalStrategy},
            {"action", "route"},
            {"server", finalDNS},
        };

        // Local
        auto dnsLocalAddress = Configs::dataManager->settingsRepo->core_box_underlying_dns.isEmpty() ? "local" : Configs::dataManager->settingsRepo->core_box_underlying_dns;
        auto dnsLocalObj = buildDnsObj(dnsLocalAddress, ctx);
        dnsLocalObj["tag"] = "dns-local";
        servers += dnsLocalObj;

        auto dnsObj = QJsonObject{
            {"servers", servers},
            {"rules", rules},
            {"cache_capacity", dataManager->settingsRepo->dns_cache_capacity},
        };
        if (dataManager->settingsRepo->dns_disable_cache) dnsObj["disable_cache"] = true;
        if (dataManager->settingsRepo->dns_disable_expire) dnsObj["disable_expire"] = true;
        if (dataManager->settingsRepo->dns_reverse_mapping) dnsObj["reverse_mapping"] = true;
        if (independentCache) dnsObj["independent_cache"] = true;
        ctx->buildConfigResult->coreConfig["dns"] = dnsObj;
    }

    void buildInboundSection(std::shared_ptr<BuildSingBoxConfigContext>& ctx) {
        if (ctx->forTest) return;
        auto tunDeps = ctx->buildPrerequisities->tunDeps;
        QJsonArray inbounds;

        // mixed
        QJsonObject inboundObj;

        if (!Configs::dataManager->settingsRepo->disable_mixed_inbound) {
            inboundObj["tag"] = "mixed-in";
            inboundObj["type"] = "mixed";
            inboundObj["listen"] = Configs::dataManager->settingsRepo->inbound_address;
            inboundObj["listen_port"] = Configs::dataManager->settingsRepo->inbound_socks_port;
            if (Configs::dataManager->settingsRepo->inbound_auth) {
                inboundObj["users"] = QJsonArray{
                    QJsonObject{
                                            {"username", Configs::dataManager->settingsRepo->inbound_user},
                                            {"password", Configs::dataManager->settingsRepo->inbound_pass}
                    }
                };
            }
            inbounds += inboundObj;
        }

        // Tun
        if (Configs::dataManager->settingsRepo->spmode_vpn) {
            QJsonObject inboundObj;
            inboundObj["tag"] = "tun-in";
            inboundObj["type"] = "tun";
            inboundObj["interface_name"] = genTunName();
            inboundObj["auto_route"] = true;
            inboundObj["mtu"] = Configs::dataManager->settingsRepo->vpn_mtu;
            inboundObj["stack"] = Configs::dataManager->settingsRepo->vpn_implementation;
            inboundObj["strict_route"] = Configs::dataManager->settingsRepo->vpn_strict_route;
            const auto tunIPv4CIDR = Configs::dataManager->settingsRepo->vpn_tun_ipv4_cidr;
            const auto tunIPv6CIDR = Configs::dataManager->settingsRepo->vpn_tun_ipv6_cidr;
            ctx->buildConfigResult->tunIPv4CIDR = tunIPv4CIDR;
            auto tunAddress = QJsonArray{ tunIPv4CIDR };
            if (Configs::dataManager->settingsRepo->vpn_ipv6) tunAddress += tunIPv6CIDR;
            inboundObj["address"] = tunAddress;

            QJsonArray routeExcludeAddrs = { "127.0.0.0/8" };
            QJsonArray routeExcludeSets;
            if (Configs::dataManager->settingsRepo->enable_tun_routing)
            {
                for (auto item : tunDeps->directIPCIDRs) routeExcludeAddrs << item;
                for (auto item : tunDeps->directIPSets) routeExcludeSets << item;
            }
            inboundObj["route_exclude_address"] = routeExcludeAddrs;
            if (!routeExcludeSets.isEmpty()) inboundObj["route_exclude_address_set"] = routeExcludeSets;
            inbounds += inboundObj;
        }

        // dns-in
        inbounds.prepend(QJsonObject{
            {"tag", "dns-in"},
            {"type", "direct"},
            {"listen", "127.0.0.1"},
            {"listen_port", dataManager->settingsRepo->core_dns_in_port}
            });

        // Hijack
        if (Configs::dataManager->settingsRepo->enable_redirect) {
            inbounds.prepend(QJsonObject{
                {"tag", "hijack"},
                {"type", "direct"},
                {"listen", Configs::dataManager->settingsRepo->redirect_listen_address},
                {"listen_port", Configs::dataManager->settingsRepo->redirect_listen_port},
                });
        }
        if (Configs::dataManager->settingsRepo->enable_dns_server) {
            inbounds.prepend(QJsonObject{
                {"tag", "hijack-dns"},
                {"type", "direct"},
                {"listen", Configs::dataManager->settingsRepo->dns_server_listen_lan ? "0.0.0.0" : "127.1.1.1"},
                {"listen_port", Configs::dataManager->settingsRepo->dns_server_listen_port},
                });
        }

        // custom
        QJSONARRAY_ADD(inbounds, QString2QJsonObject(Configs::dataManager->settingsRepo->custom_inbound)["inbounds"].toArray())
            ctx->buildConfigResult->coreConfig["inbounds"] = inbounds;
    }

    void entIDListtoEntList(
        std::shared_ptr<BuildSingBoxConfigContext>& ctx,
        const QList<int>& entIDs,
        QList<std::shared_ptr<Profile>>& ents,
        QString& error)
    {
        int extracoreCount = 0;
        int extracoreIdx = -1;
        int xrayFullConfigCount = 0;
        int xrayFullConfigIdx = -1;
        int xrayHopCount = 0;
        bool hasCustomFullConfig = false;
        int coreTransitions = 0;
        bool inXray = false;

        for (const int id : entIDs)
        {
            if (id == warpProfileID)
            {
                if (inXray)
                {
                    ctx->xrayToSingTransitioned =
                        true;
                    ++coreTransitions;
                }

                inXray =
                    false;

                auto warpProfile =
                    getWarpProfile();

                if (!warpProfile)
                {
                    error =
                        "Failed to create synthetic WARP profile";
                    return;
                }

                ents.append(
                    std::move(warpProfile)
                );

                continue;
            }

            const auto ent =
                Configs::dataManager
                ->profilesRepo
                ->GetProfile(id);

            if (!ent)
            {
                error =
                    "Null proxy in chain, "
                    "you may want to check your configs";
                return;
            }

            const auto outbound =
                ent->OutboundClone();

            if (!outbound)
            {
                error =
                    "Null outbound in chain, "
                    "data is corrupted";
                return;
            }

            const bool isXray =
                outbound->IsXray();

            if (!inXray &&
                isXray)
            {
                ctx->singToXrayTransitioned =
                    true;
                ++coreTransitions;
            }

            if (inXray &&
                !isXray)
            {
                ctx->xrayToSingTransitioned =
                    true;
                ++coreTransitions;
            }

            inXray =
                isXray;

            if (ent->Type() == "chain")
            {
                error =
                    "Chain in Chain is not allowed";
                return;
            }

            if (outbound->IsExtraCore())
            {
                ++extracoreCount;
                extracoreIdx =
                    ents.size();
            }

            if (outbound->IsXrayFullConfig())
            {
                ++xrayFullConfigCount;
                xrayFullConfigIdx =
                    ents.size();
            }

            if (isXray)
            {
                ++xrayHopCount;
            }

            if (ent->Type() == "custom")
            {
                const auto custom =
                    ent->OutboundCloneAs<Configs::Custom>();

                if (custom &&
                    custom->type ==
                    Custom::CustomFullConfig)
                {
                    hasCustomFullConfig =
                        true;
                }
            }

            ents.append(ent);
        }

        if (hasCustomFullConfig)
        {
            error =
                "Custom full config profiles cannot be used "
                "in a chain; only custom outbound profiles "
                "are chainable";
            return;
        }

        if (extracoreCount > 1)
        {
            error =
                "Only one extra-core profile is allowed "
                "in a chain";
            return;
        }

        if (xrayFullConfigCount > 1)
        {
            error =
                "Only one custom Xray full config profile "
                "is allowed in a chain";
            return;
        }

        if (extracoreCount > 0 &&
            xrayFullConfigCount > 0)
        {
            error =
                "Extra-core and custom Xray full config "
                "profiles cannot be combined in a chain";
            return;
        }

        // Do not count the Xray full-config profile itself as
        // an ordinary Xray hop when validating this combination.
        if (xrayFullConfigCount > 0 &&
            xrayHopCount > 0)
        {
            error =
                "Custom Xray full config cannot be combined "
                "with other Xray hops in a chain "
                "(only one Xray instance is supported at a time)";
            return;
        }

        if (extracoreCount == 1 &&
            ents.size() > 1 &&
            extracoreIdx != ents.size() - 1)
        {
            error =
                "Extra-core profiles can only be the final hop "
                "in a chain (top of the chain editor)";
            return;
        }

        if (xrayFullConfigCount == 1 &&
            ents.size() > 1 &&
            xrayFullConfigIdx != ents.size() - 1)
        {
            error =
                "Custom Xray full config can only be the final "
                "hop in a chain (top of the chain editor)";
            return;
        }

        if (coreTransitions > 2)
        {
            error =
                "Too many core transitions, the valid sequence is: "
                "(optional sing-box chain)->(optional xray chain)"
                "->(optional sing-box chain)";
        }
    }


    QList<int> unwrapChain(
        int entID)
    {
        const auto ent =
            Configs::dataManager
            ->profilesRepo
            ->GetProfile(entID);

        if (!ent)
        {
            return {};
        }

        if (ent->Type() != "chain")
        {
            return { entID };
        }

        const auto chain =
            ent->OutboundCloneAs<Configs::chain>();

        if (!chain)
        {
            return {};
        }

        QList<int> reversed;

        reversed.reserve(
            chain->list.size()
        );

        for (int idx =
            chain->list.size() - 1;
            idx >= 0;
            --idx)
        {
            reversed.append(
                chain->list[idx]
            );
        }

        return reversed;
    }


    void buildSingboxChain(
        std::shared_ptr<BuildSingBoxConfigContext>& ctx,
        QList<std::shared_ptr<Profile>>& ents,
        const QString& prefix,
        bool includeProxy,
        bool link,
        int startSuffix = 0,
        bool markIngress = false,
        bool warpWrap = false)
    {
        for (int idx = 0;
            idx < ents.size();
            ++idx)
        {
            QString tag =
                prefix
                + "-"
                + Int2String(
                    startSuffix + idx
                );

            QString nextTag;

            if (idx < ents.size() - 1)
            {
                nextTag =
                    prefix
                    + "-"
                    + Int2String(
                        startSuffix + idx + 1
                    );
            }

            if (includeProxy &&
                idx == 0)
            {
                tag =
                    "proxy";
            }

            if (warpWrap &&
                idx == 1)
            {
                tag =
                    "warp-bypass";
            }

            if (markIngress &&
                idx == 0)
            {
                ctx->singIngressTags <<
                    tag;
            }

            const auto& ent =
                ents[idx];

            if (!ent)
            {
                ctx->error =
                    "Null profile in sing-box chain";
                return;
            }

            const auto outbound =
                ent->OutboundClone();

            if (!outbound)
            {
                ctx->error =
                    "Null outbound in sing-box chain";
                return;
            }

            auto [object, error] =
                outbound->Build();

            if (!error.isEmpty())
            {
                ctx->error +=
                    error;
                return;
            }

            object["tag"] =
                tag;

            if (!nextTag.isEmpty() &&
                link)
            {
                object["detour"] =
                    nextTag;
            }

            if (warpWrap &&
                idx == 0)
            {
                object["detour"] =
                    "warp-bypass";
            }

            if (outbound->IsEndpoint())
            {
                ctx->endpoints.append(
                    object
                );
            }
            else
            {
                ctx->outbounds.append(
                    object
                );
            }
        }
    }


    void buildXrayChain(
        std::shared_ptr<BuildSingBoxConfigContext>& ctx,
        QList<std::shared_ptr<Profile>>& ents,
        const QString& prefix,
        bool includeProxy,
        bool link,
        int startSuffix = 0,
        coreBridgeConfig bridgeConfig = {})
    {
        for (int idx = 0;
            idx < ents.size();
            ++idx)
        {
            QString tag =
                prefix
                + "-"
                + Int2String(
                    startSuffix + idx
                );

            QString nextTag;

            if (idx < ents.size() - 1 ||
                bridgeConfig.needed)
            {
                nextTag =
                    prefix
                    + "-"
                    + Int2String(
                        startSuffix + idx + 1
                    );
            }

            if (includeProxy &&
                idx == 0)
            {
                tag =
                    "proxy";
            }

            if (idx == 0)
            {
                ctx->xrayIngressTags <<
                    tag;
            }

            const auto& ent =
                ents[idx];

            if (!ent)
            {
                ctx->error =
                    "Null profile in Xray chain";
                return;
            }

            const auto outbound =
                ent->OutboundClone();

            if (!outbound)
            {
                ctx->error =
                    "Null outbound in Xray chain";
                return;
            }

            auto [object, error] =
                outbound->BuildXray();

            if (!error.isEmpty())
            {
                ctx->error +=
                    error;
                return;
            }

            object["tag"] =
                tag;

            if (bridgeConfig.loopbackProtect)
            {
                QJsonObject streamSettings =
                    object["streamSettings"]
                    .toObject();

                const QString network =
                    streamSettings["network"]
                    .toString();

                if (network == "xhttp" ||
                    network == "splithttp")
                {
                    QJsonObject xhttpSettings =
                        streamSettings[
                            "xhttpSettings"
                        ].toObject();

                    if (xhttpSettings.isEmpty())
                    {
                        xhttpSettings =
                            streamSettings[
                                "splithttpSettings"
                            ].toObject();
                    }

                    const QJsonObject extraSettings =
                        xhttpSettings["extra"]
                        .toObject();

                    if (extraSettings.contains(
                        "downloadSettings"))
                    {
                        QJsonObject sockopt =
                            streamSettings["sockopt"]
                            .toObject();

                        sockopt["penetrate"] =
                            true;

                        streamSettings["sockopt"] =
                            sockopt;

                        object["streamSettings"] =
                            streamSettings;
                    }
                }
            }

            if (!nextTag.isEmpty() &&
                (link ||
                    bridgeConfig.needed))
            {
                object["proxySettings"] =
                    QJsonObject{
                        {
                            "tag",
                            nextTag
                        },
                        {
                            "transportLayer",
                            true
                        }
                };
            }

            ctx->xrayOutbounds.append(
                object
            );
        }

        if (bridgeConfig.needed)
        {
            QJsonObject socksSettings =
            {
                {
                    "address",
                    bridgeConfig.host
                },
                {
                    "port",
                    bridgeConfig.port
                },
                {
                    "user",
                    bridgeConfig.auth
                },
                {
                    "pass",
                    bridgeConfig.auth
                }
            };

            QJsonObject socksOutbound =
            {
                {
                    "tag",
                    prefix
                    + "-"
                    + Int2String(
                        startSuffix
                        + ents.size()
                    )
                },
                {
                    "protocol",
                    "socks"
                },
                {
                    "settings",
                    socksSettings
                }
            };

            ctx->xrayOutbounds.append(
                socksOutbound
            );
        }
    }


    void buildOutboundChain(
        std::shared_ptr<BuildSingBoxConfigContext>& ctx,
        const QList<int>& entIDs,
        const QString& prefix,
        bool includeProxy,
        bool link,
        int singToXrayPort = -1,
        int xrayToSingPort = -1,
        int startSuffix = 0,
        bool warpWrap = false)
    {
        ctx->singToXrayTransitioned =
            false;

        ctx->xrayToSingTransitioned =
            false;

        QList<std::shared_ptr<Profile>> ents;

        entIDListtoEntList(
            ctx,
            entIDs,
            ents,
            ctx->error
        );

        if (!ctx->error.isEmpty())
        {
            return;
        }

        // Build using detached copies whenever transient build-only
        // state must be written. Keep `ents` unchanged so traffic
        // accounting still points to the live Profile instances.
        QList<std::shared_ptr<Profile>> buildEnts =
            ents;

        if (!buildEnts.isEmpty())
        {
            const auto terminalOutbound =
                buildEnts
                .last()
                ->OutboundClone();

            if (terminalOutbound &&
                terminalOutbound->IsXrayFullConfig())
            {
                auto buildProfile =
                    buildEnts
                    .last()
                    ->CloneForEditing();

                if (!buildProfile)
                {
                    ctx->error =
                        "Failed to clone Custom Xray "
                        "full-config profile for build";
                    return;
                }

                const auto customSnapshot =
                    buildProfile
                    ->OutboundCloneAs<
                    Configs::Custom
                    >();

                if (!customSnapshot)
                {
                    ctx->error =
                        "Failed to cast to Custom for "
                        "Xray full config hop";
                    return;
                }

                QJsonObject userXrayConfig =
                    QString2QJsonObject(
                        customSnapshot->config
                    );

                if (userXrayConfig.isEmpty())
                {
                    ctx->error =
                        "Custom Xray full config "
                        "is not valid JSON";
                    return;
                }

                const auto bridgePorts =
                    MkManyPorts(1);

                if (bridgePorts.isEmpty())
                {
                    ctx->error =
                        "Failed to allocate a bridge "
                        "port for Custom Xray full config";
                    return;
                }

                const int bridgePort =
                    bridgePorts[0];

                const QString bridgeAuth =
                    GetRandomString(32);

                const QString bridgeHost =
                    GenRandomLoopback();

                // These fields are build-only state.
                // Write them only into the detached build profile.
                const bool bridgeConfigured =
                    buildProfile
                    ->MutateOutbound<
                    Configs::Custom
                    >(
                        [&](Configs::Custom& custom) -> bool
                        {
                            custom.bridgePort =
                                bridgePort;

                            custom.bridgeAuth =
                                bridgeAuth;

                            custom.bridgeHost =
                                bridgeHost;

                            return true;
                        }
                    );

                if (!bridgeConfigured)
                {
                    ctx->error =
                        "Failed to configure Custom Xray "
                        "full-config bridge";
                    return;
                }

                QJsonArray inbounds =
                    userXrayConfig[
                        "inbounds"
                    ].toArray();

                inbounds.prepend(
                    QJsonObject{
                        {
                            "tag",
                            "Gryph-bridge"
                        },
                        {
                            "listen",
                            bridgeHost
                        },
                        {
                            "port",
                            bridgePort
                        },
                        {
                            "protocol",
                            "socks"
                        },
                        {
                            "settings",
                            QJsonObject{
                                {
                                    "auth",
                                    "password"
                                },
                                {
                                    "udp",
                                    true
                                },
                                {
                                    "accounts",
                                    QJsonArray{
                                        QJsonObject{
                                            {
                                                "user",
                                                bridgeAuth
                                            },
                                            {
                                                "pass",
                                                bridgeAuth
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                );

                userXrayConfig[
                    "inbounds"
                ] =
                    inbounds;

                    ctx->buildConfigResult
                        ->xrayConfig =
                        userXrayConfig;

                    ctx->buildConfigResult
                        ->isXrayNeeded =
                        true;

                    buildEnts.last() =
                        std::move(
                            buildProfile
                        );
            }
        }

        QList<std::shared_ptr<Profile>>
            initialSingEnts;

        QList<std::shared_ptr<Profile>>
            xrayEnts;

        QList<std::shared_ptr<Profile>>
            tailingSingEnts;

        for (const auto& ent : buildEnts)
        {
            if (!ent)
            {
                ctx->error =
                    "Null profile while splitting "
                    "outbound chain";
                return;
            }

            const auto outbound =
                ent->OutboundClone();

            if (!outbound)
            {
                ctx->error =
                    "Null outbound while splitting "
                    "outbound chain";
                return;
            }

            if (outbound->IsXray())
            {
                xrayEnts.append(
                    ent
                );
            }
            else
            {
                if (xrayEnts.isEmpty())
                {
                    initialSingEnts.append(
                        ent
                    );
                }
                else
                {
                    tailingSingEnts.append(
                        ent
                    );
                }
            }
        }

        const auto ports =
            MkManyPorts(2);

        const bool xrayFinalEgressLoopback =
            !xrayEnts.isEmpty()
            &&
            tailingSingEnts.isEmpty()
            &&
            ctx->tunEnabled
            &&
            !ctx->xrayToSingTransitioned;

        if ((ctx->singToXrayTransitioned ||
            ctx->xrayToSingTransitioned ||
            xrayFinalEgressLoopback) &&
            ports.size() < 2)
        {
            ctx->error =
                "Failed to allocate core bridge ports";
            return;
        }

        if (ctx->singToXrayTransitioned)
        {
            const coreBridgeConfig
                singToXrayBridgeConf =
            {
                true,
                singToXrayPort == -1
                    ? ports[0]
                    : singToXrayPort,
                GetRandomString(32),
                false,
                GenRandomLoopback()
            };

            ctx->singToXrayBridges <<
                singToXrayBridgeConf;

            auto bridgeEnt =
                ProfilesRepo::NewProfile(
                    "socks"
                );

            if (!bridgeEnt)
            {
                ctx->error =
                    "Failed to create sing-to-Xray "
                    "bridge profile";
                return;
            }

            const bool socksConfigured =
                bridgeEnt
                ->MutateOutbound<
                Configs::socks
                >(
                    [&](Configs::socks& socksOutbound) -> bool
                    {
                        socksOutbound.username =
                            singToXrayBridgeConf.auth;

                        socksOutbound.password =
                            singToXrayBridgeConf.auth;

                        socksOutbound.server =
                            singToXrayBridgeConf.host;

                        socksOutbound.server_port =
                            singToXrayBridgeConf.port;

                        return true;
                    }
                );

            if (!socksConfigured)
            {
                ctx->error =
                    "Failed to create sing-to-Xray "
                    "SOCKS outbound";
                return;
            }

            initialSingEnts <<
                bridgeEnt;
        }

        coreBridgeConfig
            xrayToSingBridgeConf;

        if (ctx->xrayToSingTransitioned)
        {
            xrayToSingBridgeConf =
            {
                true,
                xrayToSingPort == -1
                    ? ports[1]
                    : xrayToSingPort,
                GetRandomString(32),
                false,
                GenRandomLoopback()
            };

            ctx->xrayToSingBridges <<
                xrayToSingBridgeConf;
        }
        else if (xrayFinalEgressLoopback)
        {
            xrayToSingBridgeConf =
            {
                true,
                xrayToSingPort == -1
                    ? ports[1]
                    : xrayToSingPort,
                GetRandomString(32),
                true,
                GenRandomLoopback()
            };

            ctx->xrayToSingBridges <<
                xrayToSingBridgeConf;
        }

        if (!initialSingEnts.isEmpty())
        {
            buildSingboxChain(
                ctx,
                initialSingEnts,
                prefix,
                includeProxy,
                link,
                startSuffix,
                false,
                warpWrap
            );

            if (!ctx->error.isEmpty())
            {
                return;
            }
        }

        if (!xrayEnts.isEmpty())
        {
            buildXrayChain(
                ctx,
                xrayEnts,
                prefix,
                includeProxy,
                link,
                startSuffix,
                xrayToSingBridgeConf
            );

            if (!ctx->error.isEmpty())
            {
                return;
            }
        }

        if (!tailingSingEnts.isEmpty())
        {
            buildSingboxChain(
                ctx,
                tailingSingEnts,
                prefix,
                false,
                link,
                startSuffix
                + initialSingEnts.size(),
                true
            );

            if (!ctx->error.isEmpty())
            {
                return;
            }
        }

        if (!ents.isEmpty())
        {
            TrafficChainGroup group;

            // Use live profiles, not detached build copies.
            group.profiles =
                ents;

            if (!tailingSingEnts.isEmpty())
            {
                group.watchTag =
                    prefix
                    + "-"
                    + Int2String(
                        startSuffix
                        + initialSingEnts.size()
                    );
            }
            else if (includeProxy)
            {
                group.watchTag =
                    "proxy";
            }
            else
            {
                group.watchTag =
                    prefix
                    + "-"
                    + Int2String(
                        startSuffix
                    );
            }

            ctx->buildConfigResult
                ->chainGroups
                .append(
                    group
                );
        }
    }


    void buildOutboundsSection(std::shared_ptr<BuildSingBoxConfigContext>& ctx) {
        // First, our own ent
        QList<int> entIDs;
        auto group =
            Configs::dataManager
            ->groupsRepo
            ->GetGroup(
                ctx->ent->GroupId()
            );


        if (!group)
        {
            ctx->error =
                "No group found for ent, "
                "data is corrupted";

            return;
        }


        const auto groupSnapshot =
            group->Snapshot();


        if (groupSnapshot.landing_proxy_id >= 0)
        {
            entIDs.prepend(
                groupSnapshot.landing_proxy_id
            );
        }


        if (ctx->ent->Type() == "chain")
        {
            auto chain =
                ctx->ent->OutboundCloneAs<Configs::chain>();


            if (!chain)
            {
                ctx->error =
                    "Ent is nullptr after cast "
                    "to chain, data is corrupted";

                return;
            }


            for (int idx =
                chain->list.size() - 1;
                idx >= 0;
                --idx)
            {
                entIDs.append(
                    chain->list[idx]
                );
            }
        }
        else
        {
            entIDs.append(
                ctx->ent->Id()
            );
        }


        if (groupSnapshot.front_proxy_id >= 0)
        {
            entIDs.append(
                groupSnapshot.front_proxy_id
            );
        }


        const bool warpWrap =
            dataManager
            ->settingsRepo
            ->enable_warp;


        if (warpWrap)
        {
            entIDs.prepend(
                warpProfileID
            );
        }


        buildOutboundChain(
            ctx,
            entIDs,
            "config",
            true,
            true,
            -1,
            -1,
            0,
            warpWrap
        );
        // A chain-typed profile wrapper isn't in entIDs (only its hops are),
        // so the chainGroup just built doesn't include it. Add it so the
        // chain's row in the proxy list also accumulates traffic.
        if (ctx->ent->Type() == "chain" && !ctx->buildConfigResult->chainGroups.isEmpty()) {
            ctx->buildConfigResult->chainGroups.last().profiles.append(ctx->ent);
        }

        // Now, build the outbounds needed by the route profile
        int routeSuffix = 0;
        for (const auto& routeGroup : ctx->buildPrerequisities->routingDeps->routeOutboundGroups) {
            bool linked = routeGroup.hopIDs.size() > 1;
            buildOutboundChain(ctx, routeGroup.hopIDs, "route", false, linked, -1, -1, routeSuffix);
            // Same as main chain: credit the chain wrapper if the route rule's
            // referenced outbound was a chain.
            if (routeGroup.chainWrapper != nullptr && !ctx->buildConfigResult->chainGroups.isEmpty()) {
                ctx->buildConfigResult->chainGroups.last().profiles.append(routeGroup.chainWrapper);
            }
            routeSuffix += routeGroup.hopIDs.size();
        }

        // Also add the needed socks inbound bridges. Loopback-protect bridges
        // have no sing-box ingress to pair with (their inbound routes straight
        // to the hidden `xray-direct`), so the singIngressTags index only
        // advances for normal xray->sing bridges.
        int loopbackBridgeCount = 0;
        for (const auto& b : ctx->xrayToSingBridges) if (b.loopbackProtect) loopbackBridgeCount++;
        if (ctx->xrayToSingBridges.size() - loopbackBridgeCount != ctx->singIngressTags.size()) {
            ctx->error = "xray to sing-box bridges count does not match ingress tags count";
            return;
        }
        QJsonArray inboundArr;
        if (ctx->buildConfigResult->coreConfig.contains("inbounds")) {
            inboundArr = ctx->buildConfigResult->coreConfig["inbounds"].toArray();
        }
        int singIngressIdx = 0;
        for (auto idx = 0; idx < ctx->xrayToSingBridges.size(); idx++) {
            auto bridgeConf = ctx->xrayToSingBridges[idx];
            QString bridgeTag = bridgeConf.loopbackProtect
                ? QString("bridge-loopback-") + Int2String(bridgeConf.port)
                : QString("bridge-") + ctx->singIngressTags[singIngressIdx++];
            QJsonObject userObj = {
                {"username", bridgeConf.auth},
                {"password", bridgeConf.auth}
            };
            QJsonObject socksBridge = {
                {"type", "socks"},
                {"tag", bridgeTag},
                {"listen", bridgeConf.host},
                {"listen_port", bridgeConf.port},
                {"users", QJsonArray{userObj}}
            };
            inboundArr.append(socksBridge);
        }
        ctx->buildConfigResult->coreConfig["inbounds"] = inboundArr;

        if (loopbackBridgeCount > 0) {
            ctx->outbounds.append(QJsonObject{
                {"type", "direct"},
                {"tag", "xray-direct"}
                });
        }

        // Add the direct outbound
        ctx->outbounds.append(QJsonObject{
        {"type", "direct"},
        {"tag", "direct"}
            });

        ctx->buildConfigResult->coreConfig["endpoints"] = ctx->endpoints;
        ctx->buildConfigResult->coreConfig["outbounds"] = ctx->outbounds;
    }

    void buildRouteSection(std::shared_ptr<BuildSingBoxConfigContext>& ctx) {
        auto routeChain = Configs::dataManager->routesRepo->GetRouteProfile(Configs::dataManager->settingsRepo->current_route_id);
        if (routeChain == nullptr) {
            ctx->error = "Routing profile does not exist, try resetting the route profile in Routing Settings";
            return;
        }
        routeChain = std::make_shared<RouteProfile>(*routeChain);
        auto routeDeps = ctx->buildPrerequisities->routingDeps;

        // hijack
        if (Configs::dataManager->settingsRepo->enable_dns_server && !ctx->forTest)
        {
            auto sniffRule = std::make_shared<RouteRule>();
            sniffRule->action = "sniff";
            sniffRule->inbound = { "hijack-dns" };

            auto redirRule = std::make_shared<RouteRule>();
            redirRule->action = "hijack-dns";
            redirRule->inbound = { "hijack-dns" };

            routeChain->Rules.prepend(redirRule);
            routeChain->Rules.prepend(sniffRule);
        }
        if (Configs::dataManager->settingsRepo->enable_redirect && !ctx->forTest) {
            auto sniffRule = std::make_shared<RouteRule>();
            sniffRule->action = "sniff";
            sniffRule->sniffOverrideDest = true;
            sniffRule->inbound = { "hijack" };
            routeChain->Rules.prepend(sniffRule);
        }

        // sniff and resolve
        if (!Configs::dataManager->settingsRepo->resolve_domain_strategy.isEmpty())
        {
            auto resolveRule = std::make_shared<RouteRule>();
            resolveRule->action = "resolve";
            resolveRule->strategy = Configs::dataManager->settingsRepo->resolve_domain_strategy;
            resolveRule->inbound = { "mixed-in", "tun-in" };
            routeChain->Rules.prepend(resolveRule);
        }
        if (Configs::dataManager->settingsRepo->sniffing_mode != SniffingMode::DISABLE)
        {
            auto sniffRule = std::make_shared<RouteRule>();
            sniffRule->action = "sniff";
            sniffRule->inbound = { "mixed-in", "tun-in" };
            routeChain->Rules.prepend(sniffRule);
        }

        // rules
        auto routeRules = routeChain->get_route_rules(false, routeDeps->outboundMap);
        // See buildDNSSection: only carve the core's own egress out to `direct`
        // via process_path when an extra core is in the chain. Otherwise we'd
        // force the (Windows-heavy) process finder even though sing-box loopback
        // is handled by auto_detect_interface and xray by its loopback bridges.
        if (!ctx->buildConfigResult->extraCoreData->path.isEmpty())
        {
            QJsonArray coreProcessPaths;
            auto extraCorePath = ctx->buildConfigResult->extraCoreData->path;
#ifdef Q_OS_WIN
            extraCorePath.replace("/", "\\");
#endif
            coreProcessPaths.append(extraCorePath);
            routeRules.prepend(QJsonObject{
                {"action", "route"},
                {"process_path", coreProcessPaths},
                {"outbound", "direct"},
                });
        }
        if (!ctx->forTest) {
            routeRules.prepend(QJsonObject{
                {"inbound", "dns-in"},
                {"action", "reject"},
                });
            routeRules.prepend(QJsonObject{
            {"action", "hijack-dns"},
            {"protocol", "dns"},
            {"inbound", "dns-in"},
                });
            routeRules.prepend(QJsonObject{
                {"action", "sniff"},
                {"inbound", "dns-in"},
                });
        }

        // rulesets
        auto ruleSetArray = QJsonArray();
        for (const auto& item : routeDeps->neededRuleSets) {
            if (auto url = QUrl(item); url.isValid() && url.fileName().contains(".srs")) {
                ruleSetArray += QJsonObject{
                            {"type", "remote"},
                            {"tag", get_rule_set_name(item)},
                            {"format", "binary"},
                            {"url", item},
                };
            }
            else
                if (auto url = ruleSetUrl(item.toStdString()); !url.empty()) {
                    ruleSetArray += QJsonObject{
                                {"type", "remote"},
                                {"tag", item},
                                {"format", "binary"},
                                {"url", get_jsdelivr_link(QString::fromUtf8(url.data(), url.size()))},
                    };
                }
        }

        // add block
        if (Configs::dataManager->settingsRepo->adblock_enable) {
            ruleSetArray += QJsonObject{
                        {"type", "remote"},
                        {"tag", "Gryph-adblocksingbox"},
                        {"format", "binary"},
                        {"url", get_jsdelivr_link("https://raw.githubusercontent.com/217heidai/adblockfilters/main/rules/adblocksingbox.srs")},
            };
        }

        // map ingress socks inbounds to their corresponding outbounds.
        // Loopback-protect bridges route to hidden `xray-direct`
        // (auto_detect_interface bypasses TUN); normal bridges route to the
        // paired sing-box ingress.
        int routeLoopbackBridgeCount = 0;
        for (const auto& b : ctx->xrayToSingBridges) if (b.loopbackProtect) routeLoopbackBridgeCount++;
        if (ctx->xrayToSingBridges.size() - routeLoopbackBridgeCount != ctx->singIngressTags.size()) {
            ctx->error = "xray to sing-box bridges count does not match ingress tags count";
            return;
        }
        int routeSingIngressIdx = 0;
        for (auto idx = 0; idx < ctx->xrayToSingBridges.size(); idx++) {
            auto bridgeConf = ctx->xrayToSingBridges[idx];
            QString inboundTag, outboundTag;
            if (bridgeConf.loopbackProtect) {
                inboundTag = "bridge-loopback-" + Int2String(bridgeConf.port);
                outboundTag = "xray-direct";
            }
            else {
                inboundTag = "bridge-" + ctx->singIngressTags[routeSingIngressIdx];
                outboundTag = ctx->singIngressTags[routeSingIngressIdx];
                routeSingIngressIdx++;
            }
            QJsonObject rule = {
                {"inbound", inboundTag},
                {"action", "route"},
                {"outbound", outboundTag},
            };
            routeRules.prepend(rule);
        }

        // apply
        const int defOut = routeChain->defaultOutboundID;
        // block-by-default: append a catch-all reject so any unmatched connection is
        // dropped. (DNS is left resolving as usual; it's the connection that's blocked.)
        if (defOut == blockID) {
            routeRules.append(QJsonObject{ {"action", "reject"} });
        }

        QJsonObject route;
        route["rules"] = routeRules;
        route["rule_set"] = ruleSetArray;
        if (defOut == blockID) {
            // Unreachable thanks to the catch-all reject above, but `final` must still
            // name a real outbound; `direct` is always present.
            route["final"] = "direct";
        }
        else if (defOut == warpBypassID) {
            route["final"] = dataManager->settingsRepo->enable_warp ? "warp-bypass" : "proxy";
        }
        else {
            route["final"] = outboundIDToString(defOut);
        }
        if (Configs::dataManager->settingsRepo->enable_stats)  route["find_process"] = true;
        route["default_domain_resolver"] = QJsonObject{
                                {"server", "dns-direct"},
                                {"strategy", Configs::dataManager->settingsRepo->default_domain_strategy} };
        if (Configs::dataManager->settingsRepo->spmode_vpn) route["auto_detect_interface"] = true;

        ctx->buildConfigResult->coreConfig["route"] = route;
    }

    void buildExperimentalSection(std::shared_ptr<BuildSingBoxConfigContext>& ctx) {
        if (ctx->forTest) return;

        QJsonObject experimentalObj;
        QJsonObject clash_api = {
            {"default_mode", ""} // dummy to make sure it is created
        };
        if (Configs::dataManager->settingsRepo->core_box_clash_api > 0) {
            clash_api = {
                {"external_controller", Configs::dataManager->settingsRepo->core_box_clash_listen_addr + ":" + Int2String(Configs::dataManager->settingsRepo->core_box_clash_api)},
                {"secret", Configs::dataManager->settingsRepo->core_box_clash_api_secret},
                {"external_ui", "dashboard"},
            };
        }
        if (Configs::dataManager->settingsRepo->core_box_clash_api > 0 || Configs::dataManager->settingsRepo->enable_stats)
        {
            experimentalObj["clash_api"] = clash_api;
        }

        QJsonObject cache_file = {
            {"enabled", true},
            {"store_fakeip", true},
            {"store_rdrc", true}
        };
        experimentalObj["cache_file"] = cache_file;

        if (experimentalObj.isEmpty()) return;

        // apply
        ctx->buildConfigResult->coreConfig["experimental"] = experimentalObj;
    }

    void buildXrayConfig(std::shared_ptr<BuildSingBoxConfigContext>& ctx) {
        if (ctx->xrayOutbounds.isEmpty()) return;
        ctx->buildConfigResult->isXrayNeeded = true;
        QJsonObject dnsObj;
        QJsonArray inbounds;
        QJsonArray routeRules;
        int dnsPort = dataManager->settingsRepo->core_dns_in_port;

        if (!ctx->forTest) {
            dnsObj = {
                {"servers", QJsonArray{
                    QJsonObject{
                        {"address", "127.0.0.1"},
                        {"port", dnsPort},
                        {"queryStrategy", "UseIPv4"},
                        {"skipFallBack", true}
                    },
                    QJsonObject{
                            {"address", "127.0.0.1"},
                            {"port", dnsPort},
                            {"queryStrategy", "UseIPv6"},
                            {"skipFallBack", true}
                    }
                }}
            };
        }

        if (ctx->xrayIngressTags.size() != ctx->singToXrayBridges.size()) {
            ctx->error = "xray ingress tags size does not match bridge count!";
            return;
        }

        for (int i = 0; i < ctx->xrayIngressTags.size(); i++) {
            auto outboundTag = ctx->xrayIngressTags[i];
            auto bridgeConf = ctx->singToXrayBridges[i];
            auto inboundTag = outboundTag + "-" + "inbound";
            inbounds << QJsonObject{
                {"tag", inboundTag},
                {"listen", bridgeConf.host},
                {"port", bridgeConf.port},
                {"protocol", "socks"},
                {"settings", QJsonObject{
                    {"auth", "password"},
                    {"udp", true},
                    {"accounts", QJsonArray{
                        QJsonObject{
                            {"user", bridgeConf.auth},
                            {"pass", bridgeConf.auth}
                        }
                    }}
                }}
            };
            routeRules << QJsonObject{
                {"type", "field"},
                {"inboundTag", QJsonArray{inboundTag}},
                {"outboundTag", outboundTag}
            };
        }

        // dnsRouting
        if (!ctx->forTest) {
            ctx->xrayOutbounds << QJsonObject{
                {"tag", "direct"},
                {"protocol", "freedom"},
            };
            routeRules << QJsonObject{
                {"type", "field"},
                {"ip", QJsonArray{"127.0.0.1"}},
                {"port", dnsPort},
                {"outboundTag", "direct"},
            };
        }

        ctx->buildConfigResult->xrayConfig["log"] = QJsonObject{
        {"loglevel", Configs::dataManager->settingsRepo->xray_log_level},
        {"access", Configs::dataManager->settingsRepo->xray_log_level == "info" ? "" : "none"}
        };
        ctx->buildConfigResult->xrayConfig["dns"] = dnsObj;
        ctx->buildConfigResult->xrayConfig["inbounds"] = inbounds;
        ctx->buildConfigResult->xrayConfig["outbounds"] = ctx->xrayOutbounds;
        ctx->buildConfigResult->xrayConfig["routing"] = QJsonObject{
            {"domainStrategy", "AsIs"},
            {"rules", routeRules},
        };
    }

    std::shared_ptr<BuildConfigResult> BuildSingBoxConfig(
        const std::shared_ptr<Profile>& ent)
    {
        if (!ent)
        {
            auto res =
                std::make_shared<BuildConfigResult>();

            res->error =
                "Cannot build config for a null profile";

            return res;
        }

        if (ent->Type() == "custom")
        {
            auto res = std::make_shared<BuildConfigResult>();
            auto custom = ent->OutboundCloneAs<Configs::Custom>();
            if (custom == nullptr)
            {
                res->error = "Corrupted data, needed custom ent, got nullptr";
                return res;
            }
            if (custom->type == Custom::CustomFullConfig)
            {
                res->coreConfig = custom->Build().object;
                return res;
            }
        }

        auto ctx = std::make_shared<BuildSingBoxConfigContext>();
        ctx->ent = ent;

        CalculatePrerequisities(ctx);

        if (!ctx->error.isEmpty())
        {
            MW_show_log(
                "Config build error:"
                + ctx->error
            );

            ctx->buildConfigResult
                ->error =
                ctx->error;

            return
                ctx->buildConfigResult;
        }

        // log
        buildLogSections(ctx);
        // ntp
        buildNTPSection(ctx);
        // DNS
        buildDNSSection(ctx);
        if (!ctx->error.isEmpty())
        {
            MW_show_log("Config build error:" + ctx->error);
            ctx->buildConfigResult->error = ctx->error;
            return ctx->buildConfigResult;
        }
        // certificate
        buildCertificateSection(ctx);
        // Inbound
        buildInboundSection(ctx);
        if (!ctx->error.isEmpty())
        {
            MW_show_log("Config build error:" + ctx->error);
            ctx->buildConfigResult->error = ctx->error;
            return ctx->buildConfigResult;
        }
        // outbound
        buildOutboundsSection(ctx);
        if (!ctx->error.isEmpty())
        {
            MW_show_log("Config build error:" + ctx->error);
            ctx->buildConfigResult->error = ctx->error;
            return ctx->buildConfigResult;
        }
        // Route
        buildRouteSection(ctx);
        if (!ctx->error.isEmpty())
        {
            MW_show_log("Config build error:" + ctx->error);
            ctx->buildConfigResult->error = ctx->error;
            return ctx->buildConfigResult;
        }
        // experimental
        buildExperimentalSection(ctx);
        if (!ctx->error.isEmpty())
        {
            MW_show_log("Config build error:" + ctx->error);
            ctx->buildConfigResult->error = ctx->error;
            return ctx->buildConfigResult;
        }
        // xray
        buildXrayConfig(ctx);
        if (!ctx->error.isEmpty()) {
            MW_show_log("Config build error:" + ctx->error);
            ctx->buildConfigResult->error = ctx->error;
            return ctx->buildConfigResult;
        }
        return ctx->buildConfigResult;
    }

    bool IsValid(
        const std::shared_ptr<Profile>& ent)
    {
        if (!ent)
        {
            MW_show_log(
                "Null profile in validator"
            );
            return false;
        }

        if (ent->Type() == "chain")
        {
            const auto chain =
                ent->OutboundCloneAs<Configs::chain>();

            if (!chain)
            {
                MW_show_log(
                    "Corrupted data, needed chain ent, "
                    "got nullptr"
                );
                return false;
            }

            for (const int eId :
            chain->list)
            {
                const auto e =
                    Configs::dataManager
                    ->profilesRepo
                    ->GetProfile(eId);

                if (!e)
                {
                    MW_show_log(
                        "Null ent in validator"
                    );
                    return false;
                }

                if (!IsValid(e))
                {
                    MW_show_log(
                        "Invalid ent in chain: ID="
                        + QString::number(eId)
                    );
                    return false;
                }
            }

            return true;
        }

        QJsonObject conf;
        bool fullConf = false;

        if (ent->Type() == "custom")
        {
            const auto custom =
                ent->OutboundCloneAs<Configs::Custom>();

            if (!custom)
            {
                MW_show_log(
                    "Corrupted data in IsValid, "
                    "needed custom ent, got nullptr"
                );
                return false;
            }

            if (custom->type ==
                Custom::CustomFullConfig)
            {
                conf =
                    QString2QJsonObject(
                        custom->config
                    );

                fullConf =
                    true;
            }

            if (custom->type ==
                Custom::CustomXrayFullConfig)
            {
                if (QString2QJsonObject(
                    custom->config
                ).isEmpty())
                {
                    MW_show_log(
                        "Custom Xray full config "
                        "is not valid JSON"
                    );
                    return false;
                }

                return true;
            }
        }

        if (!fullConf)
        {
            const auto outbound =
                ent->OutboundClone();

            if (!outbound)
            {
                MW_show_log(
                    "Null outbound in validator"
                );
                return false;
            }

            const auto out =
                outbound->Build();

            if (!out.error.isEmpty())
            {
                MW_show_log(
                    "Failed to build profile for validation: "
                    + out.error
                );
                return false;
            }

            const QJsonArray outArr{
                out.object
            };

            const QString key =
                outbound->IsEndpoint()
                ? "endpoints"
                : "outbounds";

            conf.insert(
                key,
                outArr
            );
        }

        bool ok = false;

        conf.insert(
            "log",
            QJsonObject{
                {
                    "level",
                    dataManager
                    ->settingsRepo
                    ->log_level
                }
            }
        );

        const auto resp =
            API::defaultClient
            ->CheckConfig(
                &ok,
                QJsonObject2QString(
                    conf,
                    true
                )
            );

        if (!ok)
        {
            MW_show_log(
                "Failed to Call the Core: "
                + resp
            );
            return false;
        }

        if (resp.isEmpty())
        {
            return true;
        }

        MW_show_log(
            "Invalid ent "
            + ent->Name()
            + ": "
            + resp
        );

        return false;
    }


    std::shared_ptr<BuildTestConfigResult>
        BuildTestConfig(
            const QList<
            std::shared_ptr<Profile>
            >& profiles)
    {
        auto res =
            std::make_shared<
            BuildTestConfigResult
            >();

        auto ctx =
            std::make_shared<
            BuildSingBoxConfigContext
            >();

        ctx->forTest =
            true;

        QList<int> entIDs;

        entIDs.reserve(
            profiles.size()
        );

        for (const auto& proxy :
            profiles)
        {
            if (!proxy)
            {
                continue;
            }

            const int id =
                proxy->Id();

            if (id >= 0)
            {
                entIDs <<
                    id;
            }
        }

        ctx->buildPrerequisities
            ->dnsDeps
            ->directDomains =
            QListStr2QJsonArray(
                getEntDomains(
                    entIDs,
                    ctx->error
                )
            );

        if (!ctx->error.isEmpty())
        {
            res->error =
                ctx->error;
            return res;
        }

        if (!ctx
            ->buildPrerequisities
            ->dnsDeps
            ->directDomains
            .isEmpty())
        {
            ctx->buildPrerequisities
                ->dnsDeps
                ->needDirectDnsRules =
                true;
        }

        buildDNSSection(ctx);

        if (!ctx->error.isEmpty())
        {
            res->error =
                ctx->error;
            return res;
        }

        buildLogSections(ctx);
        buildCertificateSection(ctx);
        buildNTPSection(ctx);

        int suffix = 1;
        int xrayPortIdx = 0;
        int xrayCount = 0;
        int chainCount = 0;

        for (const auto& proxy :
            profiles)
        {
            if (!proxy)
            {
                continue;
            }

            const auto outbound =
                proxy->OutboundClone();

            if (outbound &&
                outbound->IsXray())
            {
                ++xrayCount;
            }

            if (proxy->Type() ==
                "chain")
            {
                ++chainCount;
            }
        }

        // Two ports per chain are reserved as an upper bound:
        // sing->xray and xray->sing.
        const auto xrayPorts =
            MkManyPorts(
                xrayCount
                + 2 * chainCount
            );

        const auto takeXrayPort =
            [&]() -> int
            {
                if (xrayPortIdx < 0 ||
                    xrayPortIdx >=
                    xrayPorts.size())
                {
                    return -1;
                }

                return
                    xrayPorts[
                        xrayPortIdx++
                    ];
            };

        for (const auto& item :
            profiles)
        {
            if (!item)
            {
                continue;
            }

            const auto outbound =
                item->OutboundClone();

            if (!outbound)
            {
                MW_show_log(
                    "Skipping profile with null outbound"
                );

                item->SetLatency(-1);
                continue;
            }

            if (outbound->IsExtraCore())
            {
                MW_show_log(
                    "Skipping extra-core conf"
                );
                continue;
            }

            if (outbound->IsXrayFullConfig())
            {
                MW_show_log(
                    "Skipping custom Xray full config "
                    "(cannot batch-test)"
                );
                continue;
            }

            const QString itemType =
                item->Type();

            if (itemType == "chain")
            {
                bool chainHasTerminal =
                    false;

                const auto chain =
                    item->OutboundCloneAs<Configs::chain>();

                if (!chain)
                {
                    MW_show_log(
                        "Skipping corrupted chain profile"
                    );

                    item->SetLatency(-1);
                    continue;
                }

                for (const int hopID :
                chain->list)
                {
                    const auto hopEnt =
                        Configs::dataManager
                        ->profilesRepo
                        ->GetProfile(hopID);

                    if (!hopEnt)
                    {
                        chainHasTerminal =
                            true;
                        break;
                    }

                    const auto hopOutbound =
                        hopEnt
                        ->OutboundClone();

                    if (!hopOutbound)
                    {
                        chainHasTerminal =
                            true;
                        break;
                    }

                    if (hopOutbound
                        ->IsExtraCore()
                        ||
                        hopOutbound
                        ->IsXrayFullConfig())
                    {
                        chainHasTerminal =
                            true;
                        break;
                    }
                }

                if (chainHasTerminal)
                {
                    MW_show_log(
                        "Skipping chain with terminal "
                        "(extra-core or Xray full config) "
                        "hop (cannot test)"
                    );
                    continue;
                }
            }

            if (itemType == "tailscale")
            {
                MW_show_log(
                    "Skipping Tailscale conf"
                );
                continue;
            }

            if (!IsValid(item))
            {
                MW_show_log(
                    "Skipping invalid config: "
                    + item->Name()
                );

                item->SetLatency(-1);
                continue;
            }

            if (itemType == "custom")
            {
                const auto custom =
                    item->OutboundCloneAs<Configs::Custom>();

                if (!custom)
                {
                    MW_show_log(
                        "Corrupted data in build test config"
                    );

                    res->error =
                        "Corrupted data in build test config";

                    return res;
                }

                if (custom->type ==
                    Custom::CustomFullConfig)
                {
                    auto obj =
                        QString2QJsonObject(
                            custom->config
                        );

                    obj["inbounds"] =
                        QJsonArray();

                    res->fullConfigs[
                        item->Id()
                    ] =
                        QJsonObject2QString(
                            obj,
                            true
                        );

                        continue;
                }
            }

            auto IDs =
                unwrapChain(
                    item->Id()
                );

            if (IDs.isEmpty())
            {
                MW_show_log(
                    "Skipping profile with empty "
                    "or invalid chain"
                );

                item->SetLatency(-1);
                continue;
            }

            const auto group =
                Configs::dataManager
                ->groupsRepo
                ->GetGroup(
                    item->GroupId()
                );

            if (!group)
            {
                res->error =
                    "Null group on profile, "
                    "data is corrupted";
                return res;
            }

            const auto groupSnapshot =
                group->Snapshot();

            if (groupSnapshot
                .landing_proxy_id >= 0)
            {
                IDs.prepend(
                    groupSnapshot
                    .landing_proxy_id
                );
            }

            if (groupSnapshot
                .front_proxy_id >= 0)
            {
                IDs.append(
                    groupSnapshot
                    .front_proxy_id
                );
            }

            int singToXrayPort = -1;
            int xrayToSingPort = -1;

            if (outbound->IsXray())
            {
                singToXrayPort =
                    takeXrayPort();

                if (singToXrayPort < 0)
                {
                    res->error =
                        "Failed to allocate Xray test bridge port";
                    return res;
                }
            }

            if (itemType == "chain")
            {
                singToXrayPort =
                    takeXrayPort();

                xrayToSingPort =
                    takeXrayPort();

                if (singToXrayPort < 0 ||
                    xrayToSingPort < 0)
                {
                    res->error =
                        "Failed to allocate chain test bridge ports";
                    return res;
                }
            }

            buildOutboundChain(
                ctx,
                IDs,
                "proxy-"
                + Int2String(suffix),
                false,
                true,
                singToXrayPort,
                xrayToSingPort
            );

            if (!ctx->error.isEmpty())
            {
                res->error =
                    ctx->error;
                return res;
            }

            const QString tag =
                "proxy-"
                + Int2String(suffix)
                + "-0";

            res->outboundTags <<
                tag;

            res->tag2entID.insert(
                tag,
                item->Id()
            );

            ++suffix;
        }

        buildXrayConfig(ctx);

        if (!ctx->error.isEmpty())
        {
            res->error =
                ctx->error;
            return res;
        }

        ctx->outbounds <<
            QJsonObject{
                {
                    "type",
                    "direct"
                },
                {
                    "tag",
                    "direct"
                }
        };

        ctx->buildConfigResult
            ->coreConfig[
                "outbounds"
            ] =
            ctx->outbounds;

                ctx->buildConfigResult
                    ->coreConfig[
                        "endpoints"
                    ] =
                    ctx->endpoints;

                        ctx->buildConfigResult
                            ->coreConfig[
                                "route"
                            ] =
                            QJsonObject{
                                {
                                    "auto_detect_interface",
                                    true
                                },
                                {
                                    "default_domain_resolver",
                                    QJsonObject{
                                        {
                                            "server",
                                            "dns-direct"
                                        },
                                        {
                                            "strategy",
                                            Configs::dataManager
                                            ->settingsRepo
                                            ->default_domain_strategy
                                        }
                                    }
                                }
                                };

                                QJsonArray inboundArr;

                                for (const auto& bridgeConf :
                                    ctx->xrayToSingBridges)
                                {
                                    const QJsonObject userObj =
                                    {
                                        {
                                            "username",
                                            bridgeConf.auth
                                        },
                                        {
                                            "password",
                                            bridgeConf.auth
                                        }
                                    };

                                    const QJsonObject socksBridge =
                                    {
                                        {
                                            "type",
                                            "socks"
                                        },
                                        {
                                            "tag",
                                            "bridge-"
                                            + Int2String(
                                                bridgeConf.port
                                            )
                                        },
                                        {
                                            "listen",
                                            bridgeConf.host
                                        },
                                        {
                                            "listen_port",
                                            bridgeConf.port
                                        },
                                        {
                                            "users",
                                            QJsonArray{
                                                userObj
                                            }
                                        }
                                    };

                                    inboundArr.append(
                                        socksBridge
                                    );
                                }

                                ctx->buildConfigResult
                                    ->coreConfig[
                                        "inbounds"
                                    ] =
                                    inboundArr;

                                        res->coreConfig =
                                            ctx->buildConfigResult
                                            ->coreConfig;

                                        res->xrayConfig =
                                            ctx->buildConfigResult
                                            ->xrayConfig;

                                        res->isXrayNeeded =
                                            ctx->buildConfigResult
                                            ->isXrayNeeded;

                                        return res;
    }

}
