#include "include/database/entities/Profile.h"
#include "include/global/HTTPRequestHelper.hpp"
#include "include/global/TaskExecutor.hpp"

#include "include/configs/sub/GroupUpdater.hpp"
#include "include/configs/sub/Clash.hpp"

#include <atomic>
#include <utility>

#include <QDateTime>
#include <QSet>
#include <QHash>
#include <QInputDialog>
#include <QUrlQuery>
#include <QJsonDocument>

#include "include/configs/common/utils.h"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"

namespace Subscription {

    GroupUpdater* groupUpdater = new GroupUpdater;

    template <typename T, typename Mutator>
    bool mutateProfileOutbound(
        const std::shared_ptr<Configs::Profile>& profile,
        Mutator&& mutator)
    {
        if (!profile)
        {
            return false;
        }

        return profile->MutateOutbound<T>(
            std::forward<Mutator>(mutator)
        );
    }

    int JsonEndIdx(const QString& str, int begin) {
        int sz = str.length();
        int counter = 1;
        for (int i = begin + 1; i < sz; i++) {
            if (str[i] == '{') counter++;
            if (str[i] == '}') counter--;
            if (counter == 0) return i;
        }
        return -1;
    }

    QList<QString> Disect(const QString& str) {
        QList<QString> res = QList<QString>();
        int idx = 0;
        int sz = str.size();
        while (idx < sz) {
            if (str[idx] == '\n') {
                idx++;
                continue;
            }
            if (str[idx] == '{') {
                int endIdx = JsonEndIdx(str, idx);
                if (endIdx == -1) return res;
                res.append(str.mid(idx, endIdx - idx + 1));
                idx = endIdx + 1;
                continue;
            }
            int nlineIdx = str.indexOf('\n', idx);
            if (nlineIdx == -1) nlineIdx = sz;
            res.append(str.mid(idx, nlineIdx - idx));
            idx = nlineIdx + 1;
        }
        return res;
    }

    SingBoxSubType getSingBoxSubType(const QJsonDocument& doc) {
        if (doc.isObject()) {
            auto obj = doc.object();
            bool hasInbound = obj.contains("inbounds");
            bool hasOutbound = obj.contains("outbounds") || obj.contains("endpoints");
            // if (hasInbound && hasOutbound) return SingBoxSubType::fullConfig;
            if (hasOutbound) return SingBoxSubType::outboundInJson;
            if (obj.contains("type")) return SingBoxSubType::outboundObject;
            return SingBoxSubType::invalid;
        }
        if (doc.isArray() && !doc.array().empty()) {
            auto arr = doc.array();
            auto firstRaw = arr.first();
            if (firstRaw.isObject()) {
                auto obj = firstRaw.toObject();
                if (obj.contains("type")) return SingBoxSubType::outboundJsonArray;
            }
            return SingBoxSubType::invalid;
        }
        return SingBoxSubType::invalid;
    }

    // Xray uses "protocol" instead of sing-box's "type" field on outbounds, so
    // we can disambiguate by inspecting individual outbound objects rather than
    // the wrapper.
    XraySubType getXraySubType(const QJsonDocument& doc) {
        if (doc.isObject()) {
            auto obj = doc.object();
            if (obj.contains("outbounds")) {
                for (const auto& item : obj["outbounds"].toArray()) {
                    if (item.isObject() && item.toObject().contains("protocol")) {
                        return XraySubType::outboundInJson;
                    }
                }
            }
            if (obj.contains("protocol")) return XraySubType::outboundObject;
            return XraySubType::invalid;
        }
        if (doc.isArray() && !doc.array().empty()) {
            auto first = doc.array().first();
            if (first.isObject() && first.toObject().contains("protocol")) {
                return XraySubType::outboundJsonArray;
            }
        }
        return XraySubType::invalid;
    }

    // Convert a real Xray VLESS outbound (settings.vnext[0].address etc.) into
    // the simplified shape Gryph's xrayVless::ParseFromJson expects. Returns
    // an empty object if the input doesn't have the expected structure.
    QJsonObject normalizeXrayVlessForParse(const QJsonObject& out) {
        if (out["protocol"].toString() != "vless") return {};
        auto settings = out["settings"].toObject();
        // Already in simplified form.
        if (settings.contains("address") && !settings.contains("vnext")) return out;
        auto vnext = settings["vnext"].toArray();
        if (vnext.isEmpty()) return {};
        auto first = vnext.first().toObject();
        if (first.isEmpty()) return {};
        auto users = first["users"].toArray();
        if (users.isEmpty()) return {};
        auto user = users.first().toObject();
        QJsonObject simpleSettings;
        simpleSettings["address"] = first["address"];
        simpleSettings["port"] = first["port"];
        simpleSettings["id"] = user["id"];
        simpleSettings["encryption"] = user.contains("encryption") ? user["encryption"] : QJsonValue("none");
        simpleSettings["flow"] = user["flow"];
        QJsonObject normalized = out;
        normalized["settings"] = simpleSettings;
        return normalized;
    }

    std::shared_ptr<Configs::Profile>
        makeProfileForXrayOutbound(
            const QJsonObject& out)
    {
        if (out.isEmpty())
        {
            return nullptr;
        }

        const QString protocol =
            out["protocol"].toString();

        // System protocols do not make sense as user profiles.
        if (protocol == "freedom" ||
            protocol == "blackhole" ||
            protocol == "dns" ||
            protocol == "loopback")
        {
            return nullptr;
        }

        if (protocol == "vless")
        {
            const auto normalized =
                normalizeXrayVlessForParse(out);

            if (!normalized.isEmpty())
            {
                auto ent =
                    Configs::ProfilesRepo::NewProfile(
                        "xrayvless"
                    );

                const bool parsed =
                    mutateProfileOutbound<
                    Configs::xrayVless
                    >(
                        ent,
                        [&](Configs::xrayVless& outbound)
                        {
                            return outbound.ParseFromJson(
                                normalized
                            );
                        }
                    );

                if (parsed)
                {
                    return ent;
                }
            }
        }

        auto ent =
            Configs::ProfilesRepo::NewProfile(
                "custom"
            );

        if (!ent)
        {
            return nullptr;
        }

        const QString tag =
            out["tag"].toString();

        const QString config =
            QJsonObject2QString(
                out,
                false
            );

        const bool configured =
            mutateProfileOutbound<
            Configs::Custom
            >(
                ent,
                [&](Configs::Custom& custom)
                {
                    custom.type =
                        Configs::Custom::
                        CustomXrayOutbound;

                    custom.config =
                        config;

                    if (!tag.isEmpty())
                    {
                        custom.name =
                            tag;
                    }

                    return true;
                }
            );

        return configured
            ? ent
            : nullptr;
    }

    void RawUpdater::update(const QString& str, bool needParse, bool isBase64Decoded) {
        // Base64 encoded subscription
        if (!isBase64Decoded) {
            if (auto str2 = DecodeB64IfValid(str); !str2.isEmpty()) {
                update(str2, true, true);
                return;
            }
        }

        std::shared_ptr<Configs::Profile> ent;

        // Json
        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(str.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError) {
            // Xray (checked first since its outbounds are tagged with
            // "protocol", which lets us cleanly disambiguate from sing-box
            // configs that share the "outbounds" wrapper).
            auto xrayType = getXraySubType(doc);
            if (xrayType == XraySubType::outboundObject) {
                if (auto e = makeProfileForXrayOutbound(doc.object()); e != nullptr) {
                    updated_order += e;
                }
                return;
            }
            if (xrayType == XraySubType::outboundInJson || xrayType == XraySubType::outboundJsonArray) {
                updateXray(doc, xrayType);
                return;
            }

            // SingBox
            auto subType = getSingBoxSubType(doc);
            if (subType == SingBoxSubType::fullConfig)
            {
                ent =
                    Configs::ProfilesRepo::NewProfile(
                        "custom"
                    );

                const bool configured =
                    mutateProfileOutbound<
                    Configs::Custom
                    >(
                        ent,
                        [&](Configs::Custom& custom)
                        {
                            custom.type =
                                Configs::Custom::
                                CustomFullConfig;

                            custom.config =
                                str;

                            return true;
                        }
                    );

                if (configured)
                {
                    updated_order += ent;
                }
            }
            else if (subType == SingBoxSubType::outboundObject)
            {
                ent =
                    Configs::ProfilesRepo::NewProfile(
                        "custom"
                    );

                const bool configured =
                    mutateProfileOutbound<
                    Configs::Custom
                    >(
                        ent,
                        [&](Configs::Custom& custom)
                        {
                            custom.type =
                                Configs::Custom::
                                CustomOutbound;

                            custom.config =
                                str;

                            return true;
                        }
                    );

                if (configured)
                {
                    updated_order += ent;
                }
            }
            else if (subType == SingBoxSubType::outboundInJson ||
                subType == SingBoxSubType::outboundJsonArray) {
                updateSingBox(doc, subType);
                return;
            }

            // SIP008
            if (str.contains("version") && str.contains("servers"))
            {
                updateSIP008(str);
                return;
            }

            return;
        }

        // Clash
        if (str.contains("proxies:")) {
            updateClash(str);
            return;
        }

        // Wireguard Config
        if (str.contains("[Interface]") && str.contains("[Peer]"))
        {
            updateWireguardFileConfig(str);
            return;
        }

        // Multi line
        if (str.count("\n") > 0 && needParse) {
            auto list = Disect(str);
            for (const auto& str2 : list) {
                update(str2.trimmed(), false);
            }
            return;
        }

        // is comment or too short
        if (str.startsWith("//") || str.startsWith("#") || str.length() < 2) {
            return;
        }

        // Json base64 link format
        if (str.startsWith("json://")) {
            auto link = QUrl(str);
            if (!link.isValid()) return;
            auto dataBytes = DecodeB64IfValid(link.fragment().toUtf8(), QByteArray::Base64UrlEncoding);
            if (dataBytes.isEmpty()) return;
            auto data = QJsonDocument::fromJson(dataBytes).object();
            if (data.isEmpty()) return;
            if (data.contains("protocol")) {
                ent = Configs::ProfilesRepo::NewProfile("xray" + data["protocol"].toString());
            }
            else {
                ent = data["type"].toString() == "hysteria2" ? Configs::ProfilesRepo::NewProfile("hysteria") : Configs::ProfilesRepo::NewProfile(data["type"].toString());
            }
            const bool parsed =
                mutateProfileOutbound<
                Configs::outbound
                >(
                    ent,
                    [&](Configs::outbound& outbound)
                    {
                        if (outbound.invalid)
                        {
                            return false;
                        }

                        return outbound.ParseFromJson(
                            data
                        );
                    }
                );

            if (!parsed)
            {
                return;
            }
        }

        // Json
        if (str.startsWith('{'))
        {
            const auto obj =
                QString2QJsonObject(
                    str
                );

            QString customType;

            if (obj.contains("outbounds"))
            {
                customType =
                    Configs::Custom::
                    CustomFullConfig;
            }
            else if (obj.contains("server"))
            {
                customType =
                    Configs::Custom::
                    CustomOutbound;
            }
            else
            {
                return;
            }

            ent =
                Configs::ProfilesRepo::NewProfile(
                    "custom"
                );

            const bool configured =
                mutateProfileOutbound<
                Configs::Custom
                >(
                    ent,
                    [&](Configs::Custom& custom)
                    {
                        custom.type =
                            customType;

                        custom.config =
                            str;

                        return true;
                    }
                );

            if (!configured)
            {
                return;
            }
        }

        // SOCKS
        if (str.startsWith("socks5://") || str.startsWith("socks4://") ||
            str.startsWith("socks4a://") || str.startsWith("socks://")) {
            ent = Configs::ProfilesRepo::NewProfile("socks");
            const bool ok =
                mutateProfileOutbound<
                Configs::socks
                >(
                    ent,
                    [&](Configs::socks& outbound)
                    {
                        return outbound.ParseFromLink(
                            str
                        );
                    }
                );
            if (!ok) return;
        }

        // HTTP
        if (str.startsWith("http://") || str.startsWith("https://")) {
            ent = Configs::ProfilesRepo::NewProfile("http");
            const bool ok =
                mutateProfileOutbound<
                Configs::http
                >(
                    ent,
                    [&](Configs::http& outbound)
                    {
                        return outbound.ParseFromLink(
                            str
                        );
                    }
                );
            if (!ok) return;
        }

        // ShadowSocks
        if (str.startsWith("ss://")) {
            ent = Configs::ProfilesRepo::NewProfile("shadowsocks");
            const bool ok =
                mutateProfileOutbound<
                Configs::shadowsocks
                >(
                    ent,
                    [&](Configs::shadowsocks& outbound)
                    {
                        return outbound.ParseFromLink(
                            str
                        );
                    }
                );
            if (!ok) return;
        }

        // VMess
        if (str.startsWith("vmess://")) {
            ent = Configs::ProfilesRepo::NewProfile("vmess");
            const bool ok =
                mutateProfileOutbound<
                Configs::vmess
                >(
                    ent,
                    [&](Configs::vmess& outbound)
                    {
                        return outbound.ParseFromLink(
                            str
                        );
                    }
                );
            if (!ok) return;
        }

        // VLESS
        if (str.startsWith("vless://")) {
            if (Configs::useXrayVless(str)) {
                ent = Configs::ProfilesRepo::NewProfile("xrayvless");
                const bool ok =
                    mutateProfileOutbound<
                    Configs::xrayVless
                    >(
                        ent,
                        [&](Configs::xrayVless& outbound)
                        {
                            return outbound.ParseFromLink(
                                str
                            );
                        }
                    );
                if (!ok) return;
            }
            else {
                ent = Configs::ProfilesRepo::NewProfile("vless");
                const bool ok =
                    mutateProfileOutbound<
                    Configs::vless
                    >(
                        ent,
                        [&](Configs::vless& outbound)
                        {
                            return outbound.ParseFromLink(
                                str
                            );
                        }
                    );
                if (!ok) return;
            }
        }

        // Trojan
        if (str.startsWith("trojan://")) {
            ent = Configs::ProfilesRepo::NewProfile("trojan");
            const bool ok =
                mutateProfileOutbound<
                Configs::Trojan
                >(
                    ent,
                    [&](Configs::Trojan& outbound)
                    {
                        return outbound.ParseFromLink(
                            str
                        );
                    }
                );
            if (!ok) return;
        }

        // AnyTLS
        if (str.startsWith("anytls://")) {
            ent = Configs::ProfilesRepo::NewProfile("anytls");
            const bool ok =
                mutateProfileOutbound<
                Configs::anyTLS
                >(
                    ent,
                    [&](Configs::anyTLS& outbound)
                    {
                        return outbound.ParseFromLink(
                            str
                        );
                    }
                );
            if (!ok) return;
        }

        // Hysteria
        if (str.startsWith("hysteria://") || str.startsWith("hysteria2://") || str.startsWith("hy2://")) {
            ent = Configs::ProfilesRepo::NewProfile("hysteria");
            const bool ok =
                mutateProfileOutbound<
                Configs::hysteria
                >(
                    ent,
                    [&](Configs::hysteria& outbound)
                    {
                        return outbound.ParseFromLink(
                            str
                        );
                    }
                );
            if (!ok) return;
        }

        // TUIC
        if (str.startsWith("tuic://")) {
            ent = Configs::ProfilesRepo::NewProfile("tuic");
            const bool ok =
                mutateProfileOutbound<
                Configs::tuic
                >(
                    ent,
                    [&](Configs::tuic& outbound)
                    {
                        return outbound.ParseFromLink(
                            str
                        );
                    }
                );
            if (!ok) return;
        }

        // Juicity
        if (str.startsWith("juicity://")) {
            ent = Configs::ProfilesRepo::NewProfile("juicity");
            const bool ok =
                mutateProfileOutbound<
                Configs::juicity
                >(
                    ent,
                    [&](Configs::juicity& outbound)
                    {
                        return outbound.ParseFromLink(
                            str
                        );
                    }
                );
            if (!ok) return;
        }

        // TrustTunnel
        if (str.startsWith("tt://")) {
            ent = Configs::ProfilesRepo::NewProfile("trusttunnel");
            const bool ok =
                mutateProfileOutbound<
                Configs::trusttunnel
                >(
                    ent,
                    [&](Configs::trusttunnel& outbound)
                    {
                        return outbound.ParseFromLink(
                            str
                        );
                    }
                );
            if (!ok) return;
        }

        // ShadowTLS
        if (str.startsWith("shadowtls://")) {
            ent = Configs::ProfilesRepo::NewProfile("shadowtls");
            const bool ok =
                mutateProfileOutbound<
                Configs::shadowtls
                >(
                    ent,
                    [&](Configs::shadowtls& outbound)
                    {
                        return outbound.ParseFromLink(
                            str
                        );
                    }
                );
            if (!ok) return;
        }

        // Wireguard
        if (str.startsWith("wg://")) {
            ent = Configs::ProfilesRepo::NewProfile("wireguard");
            const bool ok =
                mutateProfileOutbound<
                Configs::wireguard
                >(
                    ent,
                    [&](Configs::wireguard& outbound)
                    {
                        return outbound.ParseFromLink(
                            str
                        );
                    }
                );
            if (!ok) return;
        }

        // SSH
        if (str.startsWith("ssh://")) {
            ent = Configs::ProfilesRepo::NewProfile("ssh");
            const bool ok =
                mutateProfileOutbound<
                Configs::ssh
                >(
                    ent,
                    [&](Configs::ssh& outbound)
                    {
                        return outbound.ParseFromLink(
                            str
                        );
                    }
                );
            if (!ok) return;
        }

        // Naive
        if (str.startsWith("naive+https://") || str.startsWith("naive+quic://")) {
            ent = Configs::ProfilesRepo::NewProfile("naive");
            const bool ok =
                mutateProfileOutbound<
                Configs::naive
                >(
                    ent,
                    [&](Configs::naive& outbound)
                    {
                        return outbound.ParseFromLink(
                            str
                        );
                    }
                );
            if (!ok) return;
        }

        if (ent == nullptr) return;

        // End
        updated_order += ent;
    }

    void RawUpdater::updateSingBox(const QJsonDocument& doc, SingBoxSubType type)
    {
        QJsonArray outbounds, endpoints;
        if (type == SingBoxSubType::outboundInJson) {
            auto json = doc.object();
            outbounds = json["outbounds"].toArray();
            endpoints = json["endpoints"].toArray();
        }
        else if (type == SingBoxSubType::outboundJsonArray) {
            outbounds = doc.array();
        }
        else {
            return;
        }
        QJsonArray items;
        for (const auto& outbound : outbounds)
        {
            if (!outbound.isObject()) continue;
            items.append(outbound.toObject());
        }
        for (const auto& endpoint : endpoints)
        {
            if (!endpoint.isObject()) continue;
            items.append(endpoint.toObject());
        }

        for (const auto& o : items)
        {
            auto out = o.toObject();
            if (out.isEmpty())
            {
                MW_show_log("invalid outbound of type: " + o.type());
                continue;
            }

            std::shared_ptr<Configs::Profile> ent;

            // SOCKS
            if (out["type"] == "socks") {
                ent = Configs::ProfilesRepo::NewProfile("socks");
                const bool ok =
                    mutateProfileOutbound<
                    Configs::socks
                    >(
                        ent,
                        [&](Configs::socks& outbound)
                        {
                            return outbound.ParseFromJson(
                                out
                            );
                        }
                    );
                if (!ok) continue;
            }

            // HTTP
            if (out["type"] == "http") {
                ent = Configs::ProfilesRepo::NewProfile("http");
                const bool ok =
                    mutateProfileOutbound<
                    Configs::http
                    >(
                        ent,
                        [&](Configs::http& outbound)
                        {
                            return outbound.ParseFromJson(
                                out
                            );
                        }
                    );
                if (!ok) continue;
            }

            // ShadowSocks
            if (out["type"] == "shadowsocks") {
                ent = Configs::ProfilesRepo::NewProfile("shadowsocks");
                const bool ok =
                    mutateProfileOutbound<
                    Configs::shadowsocks
                    >(
                        ent,
                        [&](Configs::shadowsocks& outbound)
                        {
                            return outbound.ParseFromJson(
                                out
                            );
                        }
                    );
                if (!ok) continue;
            }

            // VMess
            if (out["type"] == "vmess") {
                ent = Configs::ProfilesRepo::NewProfile("vmess");
                const bool ok =
                    mutateProfileOutbound<
                    Configs::vmess
                    >(
                        ent,
                        [&](Configs::vmess& outbound)
                        {
                            return outbound.ParseFromJson(
                                out
                            );
                        }
                    );
                if (!ok) continue;
            }

            // VLESS
            if (out["type"] == "vless") {
                ent = Configs::ProfilesRepo::NewProfile("vless");
                const bool ok =
                    mutateProfileOutbound<
                    Configs::vless
                    >(
                        ent,
                        [&](Configs::vless& outbound)
                        {
                            return outbound.ParseFromJson(
                                out
                            );
                        }
                    );
                if (!ok) continue;
            }

            // Trojan
            if (out["type"] == "trojan") {
                ent = Configs::ProfilesRepo::NewProfile("trojan");
                const bool ok =
                    mutateProfileOutbound<
                    Configs::Trojan
                    >(
                        ent,
                        [&](Configs::Trojan& outbound)
                        {
                            return outbound.ParseFromJson(
                                out
                            );
                        }
                    );
                if (!ok) continue;
            }

            // AnyTLS
            if (out["type"] == "anytls") {
                ent = Configs::ProfilesRepo::NewProfile("anytls");
                const bool ok =
                    mutateProfileOutbound<
                    Configs::anyTLS
                    >(
                        ent,
                        [&](Configs::anyTLS& outbound)
                        {
                            return outbound.ParseFromJson(
                                out
                            );
                        }
                    );
                if (!ok) continue;
            }

            // Hysteria
            if (out["type"] == "hysteria" || out["type"] == "hysteria2") {
                ent = Configs::ProfilesRepo::NewProfile("hysteria");
                const bool ok =
                    mutateProfileOutbound<
                    Configs::hysteria
                    >(
                        ent,
                        [&](Configs::hysteria& outbound)
                        {
                            return outbound.ParseFromJson(
                                out
                            );
                        }
                    );
                if (!ok) continue;
            }

            // TUIC
            if (out["type"] == "tuic") {
                ent = Configs::ProfilesRepo::NewProfile("tuic");
                const bool ok =
                    mutateProfileOutbound<
                    Configs::tuic
                    >(
                        ent,
                        [&](Configs::tuic& outbound)
                        {
                            return outbound.ParseFromJson(
                                out
                            );
                        }
                    );
                if (!ok) continue;
            }

            // Juicity
            if (out["type"] == "juicity") {
                ent = Configs::ProfilesRepo::NewProfile("juicity");
                const bool ok =
                    mutateProfileOutbound<
                    Configs::juicity
                    >(
                        ent,
                        [&](Configs::juicity& outbound)
                        {
                            return outbound.ParseFromJson(
                                out
                            );
                        }
                    );
                if (!ok) continue;
            }

            // TrustTunnel
            if (out["type"] == "trusttunnel") {
                ent = Configs::ProfilesRepo::NewProfile("trusttunnel");
                const bool ok =
                    mutateProfileOutbound<
                    Configs::trusttunnel
                    >(
                        ent,
                        [&](Configs::trusttunnel& outbound)
                        {
                            return outbound.ParseFromJson(
                                out
                            );
                        }
                    );
                if (!ok) continue;
            }

            // ShadowTLS
            if (out["type"] == "shadowtls") {
                ent = Configs::ProfilesRepo::NewProfile("shadowtls");
                const bool ok =
                    mutateProfileOutbound<
                    Configs::shadowtls
                    >(
                        ent,
                        [&](Configs::shadowtls& outbound)
                        {
                            return outbound.ParseFromJson(
                                out
                            );
                        }
                    );
                if (!ok) continue;
            }

            // Wireguard
            if (out["type"] == "wireguard") {
                ent = Configs::ProfilesRepo::NewProfile("wireguard");
                const bool ok =
                    mutateProfileOutbound<
                    Configs::wireguard
                    >(
                        ent,
                        [&](Configs::wireguard& outbound)
                        {
                            return outbound.ParseFromJson(
                                out
                            );
                        }
                    );
                if (!ok) continue;
            }

            // SSH
            if (out["type"] == "ssh") {
                ent = Configs::ProfilesRepo::NewProfile("ssh");
                const bool ok =
                    mutateProfileOutbound<
                    Configs::ssh
                    >(
                        ent,
                        [&](Configs::ssh& outbound)
                        {
                            return outbound.ParseFromJson(
                                out
                            );
                        }
                    );
                if (!ok) continue;
            }

            // Naive
            if (out["type"] == "naive") {
                ent = Configs::ProfilesRepo::NewProfile("naive");
                const bool ok =
                    mutateProfileOutbound<
                    Configs::naive
                    >(
                        ent,
                        [&](Configs::naive& outbound)
                        {
                            return outbound.ParseFromJson(
                                out
                            );
                        }
                    );
                if (!ok) continue;
            }

            if (ent == nullptr) continue;

            updated_order += ent;
        }
    }

    void RawUpdater::updateXray(const QJsonDocument& doc, XraySubType type)
    {
        QJsonArray outbounds;
        if (type == XraySubType::outboundInJson) {
            outbounds = doc.object()["outbounds"].toArray();
        }
        else if (type == XraySubType::outboundJsonArray) {
            outbounds = doc.array();
        }
        else {
            return;
        }
        for (const auto& o : outbounds) {
            if (!o.isObject()) continue;
            if (auto e = makeProfileForXrayOutbound(o.toObject()); e != nullptr) {
                updated_order += e;
            }
        }
    }

    void RawUpdater::updateClash(const QString& str)
    {
        try {
            fkyaml::node node = fkyaml::node::deserialize(str.toStdString());
            clash::Clash clash_config = node.get_value<clash::Clash>();

            for (const auto& out : clash_config.proxies)
            {
                std::shared_ptr<Configs::Profile> ent;

                // SOCKS
                if (out.type == "socks5") {
                    ent = Configs::ProfilesRepo::NewProfile("socks");
                    const bool ok =
                        mutateProfileOutbound<
                        Configs::socks
                        >(
                            ent,
                            [&](Configs::socks& outbound)
                            {
                                return outbound.ParseFromClash(
                                    out
                                );
                            }
                        );
                    if (!ok) continue;
                }

                // HTTP
                if (out.type == "http") {
                    ent = Configs::ProfilesRepo::NewProfile("http");
                    const bool ok =
                        mutateProfileOutbound<
                        Configs::http
                        >(
                            ent,
                            [&](Configs::http& outbound)
                            {
                                return outbound.ParseFromClash(
                                    out
                                );
                            }
                        );
                    if (!ok) continue;
                }

                // ShadowSocks
                if (out.type == "ss") {
                    ent = Configs::ProfilesRepo::NewProfile("shadowsocks");
                    const bool ok =
                        mutateProfileOutbound<
                        Configs::shadowsocks
                        >(
                            ent,
                            [&](Configs::shadowsocks& outbound)
                            {
                                return outbound.ParseFromClash(
                                    out
                                );
                            }
                        );
                    if (!ok) continue;
                }

                // VMess
                if (out.type == "vmess") {
                    ent = Configs::ProfilesRepo::NewProfile("vmess");
                    const bool ok =
                        mutateProfileOutbound<
                        Configs::vmess
                        >(
                            ent,
                            [&](Configs::vmess& outbound)
                            {
                                return outbound.ParseFromClash(
                                    out
                                );
                            }
                        );
                    if (!ok) continue;
                }

                // VLESS
                if (out.type == "vless") {
                    if (out.network == "xhttp" || (!out.encryption.empty() && out.encryption != "none")) {
                        ent = Configs::ProfilesRepo::NewProfile("xrayvless");
                        const bool ok =
                            mutateProfileOutbound<
                            Configs::xrayVless
                            >(
                                ent,
                                [&](Configs::xrayVless& outbound)
                                {
                                    return outbound.ParseFromClash(
                                        out
                                    );
                                }
                            );
                        if (!ok) continue;
                    }
                    else {
                        ent = Configs::ProfilesRepo::NewProfile("vless");
                        const bool ok =
                            mutateProfileOutbound<
                            Configs::vless
                            >(
                                ent,
                                [&](Configs::vless& outbound)
                                {
                                    return outbound.ParseFromClash(
                                        out
                                    );
                                }
                            );
                        if (!ok) continue;
                    }
                }

                // Trojan
                if (out.type == "trojan") {
                    ent = Configs::ProfilesRepo::NewProfile("trojan");
                    const bool ok =
                        mutateProfileOutbound<
                        Configs::Trojan
                        >(
                            ent,
                            [&](Configs::Trojan& outbound)
                            {
                                return outbound.ParseFromClash(
                                    out
                                );
                            }
                        );
                    if (!ok) continue;
                }

                // AnyTLS
                if (out.type == "anytls") {
                    ent = Configs::ProfilesRepo::NewProfile("anytls");
                    const bool ok =
                        mutateProfileOutbound<
                        Configs::anyTLS
                        >(
                            ent,
                            [&](Configs::anyTLS& outbound)
                            {
                                return outbound.ParseFromClash(
                                    out
                                );
                            }
                        );
                    if (!ok) continue;
                }

                // Hysteria
                if (out.type == "hysteria" || out.type == "hysteria2") {
                    ent = Configs::ProfilesRepo::NewProfile("hysteria");
                    const bool ok =
                        mutateProfileOutbound<
                        Configs::hysteria
                        >(
                            ent,
                            [&](Configs::hysteria& outbound)
                            {
                                return outbound.ParseFromClash(
                                    out
                                );
                            }
                        );
                    if (!ok) continue;
                }

                // TUIC
                if (out.type == "tuic") {
                    ent = Configs::ProfilesRepo::NewProfile("tuic");
                    const bool ok =
                        mutateProfileOutbound<
                        Configs::tuic
                        >(
                            ent,
                            [&](Configs::tuic& outbound)
                            {
                                return outbound.ParseFromClash(
                                    out
                                );
                            }
                        );
                    if (!ok) continue;
                }

                // SSH
                if (out.type == "ssh") {
                    ent = Configs::ProfilesRepo::NewProfile("ssh");
                    const bool ok =
                        mutateProfileOutbound<
                        Configs::ssh
                        >(
                            ent,
                            [&](Configs::ssh& outbound)
                            {
                                return outbound.ParseFromClash(
                                    out
                                );
                            }
                        );
                    if (!ok) continue;
                }

                if (ent == nullptr) continue;

                updated_order += ent;
            }
        }
        catch (const fkyaml::exception& ex) {
            runOnUiThread([=] {
                MessageBoxWarning("YAML Exception", ex.what());
                });
        }
    }

    void RawUpdater::updateWireguardFileConfig(const QString& str)
    {
        auto ent = Configs::ProfilesRepo::NewProfile("wireguard");
        const bool ok =
            mutateProfileOutbound<
            Configs::wireguard
            >(
                ent,
                [&](Configs::wireguard& outbound)
                {
                    return outbound.ParseFromLink(
                        str
                    );
                }
            );
        if (!ok) return;
        updated_order += ent;
    }

    void RawUpdater::updateSIP008(const QString& str)
    {
        auto json = QString2QJsonObject(str);

        for (const auto& o : json["servers"].toArray())
        {
            auto out = o.toObject();
            if (out.isEmpty())
            {
                MW_show_log("invalid server object");
                continue;
            }

            auto ent = Configs::ProfilesRepo::NewProfile("shadowsocks");
            const bool ok =
                mutateProfileOutbound<
                Configs::shadowsocks
                >(
                    ent,
                    [&](Configs::shadowsocks& outbound)
                    {
                        return outbound.ParseFromSIP008(
                            out
                        );
                    }
                );
            if (!ok) continue;
            updated_order += ent;
        }
    }

    // 在新的 thread 运行
    void GroupUpdater::AsyncUpdate(const QString& str, int _sub_gid, const std::function<void()>& finish) {
        auto content = str.trimmed();
        bool asURL = false;
        bool createNewGroup = false;

        if (_sub_gid < 0 && (content.startsWith("http://") || content.startsWith("https://"))) {
            auto items = QStringList{
                QObject::tr("Add profiles to this group"),
                QObject::tr("Create new subscription group"),
                QObject::tr("Import HTTP proxy profile"),
            };
            bool ok;
            auto a = QInputDialog::getItem(nullptr,
                QObject::tr("url detected"),
                QObject::tr("%1\nHow to update?").arg(content),
                items, 0, false, &ok);
            if (!ok) return;
            switch (items.indexOf(a)) {
            case 1: createNewGroup = true;
            case 0: asURL = true; break;
            }
        }

        Async::run(
            [=, this]
            {
            auto gid =
                _sub_gid;

            if (createNewGroup) {
                auto group =
                    Configs::GroupsRepo::NewGroup();

                group->SetSubscriptionSource(
                    QUrl(str).host(),
                    str
                );

                Configs::dataManager
                    ->groupsRepo
                    ->AddGroup(group);

                gid =
                    group
                    ->Snapshot()
                    .id;

                MW_dialog_message(
                    MwMessage::SubscriptionNewGroup,
                    {}
                );
            }

            Update(
                str,
                gid,
                asURL
            );

            emit asyncUpdateCallback(
                gid
            );

            if (finish != nullptr) {
                finish();
            }
            });
    }

    void GroupUpdater::Update(
        const QString& _str,
        int _sub_gid,
        bool _not_sub_as_url)
    {
        auto* const settings =
            Configs::dataManager
            ->settingsRepo
            .get();

        auto* const profilesRepo =
            Configs::dataManager
            ->profilesRepo
            .get();

        auto* const groupsRepo =
            Configs::dataManager
            ->groupsRepo
            .get();


        if (!settings ||
            !profilesRepo ||
            !groupsRepo)
        {
            MW_show_log(
                "GroupUpdater: repositories "
                "are not initialized"
            );

            return;
        }


        settings->imported_count = 0;


        // =====================================================
        // Raw parser
        // =====================================================

        auto rawUpdater =
            std::make_unique<RawUpdater>();

        rawUpdater->gid_add_to =
            _sub_gid;


        QString content =
            _str.trimmed();

        QString subUserInfo;


        const bool asURL =
            _sub_gid >= 0
            ||
            _not_sub_as_url;


        // =====================================================
        // Resolve target group
        // =====================================================

        auto group =
            groupsRepo->GetGroup(
                _sub_gid
            );


        Configs::GroupSnapshot
            initialGroupSnapshot;


        if (_sub_gid >= 0 &&
            !group)
        {
            MW_show_log(
                QString(
                    "GroupUpdater: group %1 "
                    "does not exist"
                )
                .arg(_sub_gid)
            );

            return;
        }


        if (group)
        {
            initialGroupSnapshot =
                group->Snapshot();


            if (initialGroupSnapshot.archive)
            {
                return;
            }
        }


        // =====================================================
        // Download subscription
        //
        // IMPORTANT:
        // No Profile/Group data has been changed yet.
        // =====================================================

        if (asURL)
        {
            const QString groupName =
                group
                ? initialGroupSnapshot.name
                : content;


            MW_show_log(
                ">>>>>>>> "
                +
                QObject::tr(
                    "Requesting subscription: %1"
                )
                .arg(groupName)
            );


            const auto resp =
                NetworkRequestHelper::HttpGet(
                    content,
                    settings->sub_send_hwid
                );


            if (!resp.error.isEmpty())
            {
                MW_show_log(
                    "<<<<<<<< "
                    +
                    QObject::tr(
                        "Requesting subscription "
                        "%1 error: %2"
                    )
                    .arg(
                        groupName,
                        resp.error
                        + "\n"
                        + resp.data
                    )
                );

                // IMPORTANT:
                // Old group is completely untouched.
                return;
            }


            content =
                resp.data;


            subUserInfo =
                NetworkRequestHelper::GetHeader(
                    resp.header,
                    "Subscription-UserInfo"
                );


            MW_show_log(
                "<<<<<<<< "
                +
                QObject::tr(
                    "Subscription request finished: %1"
                )
                .arg(groupName)
            );
        }


        // =====================================================
        // Parse subscription FIRST
        //
        // RawUpdater creates detached/unpublished Profiles.
        // Nothing is written to ProfilesRepo yet.
        // =====================================================

        MW_show_log(
            ">>>>>>>> "
            +
            QObject::tr(
                "Processing subscription data..."
            )
        );


        rawUpdater->update(
            content
        );


        content.clear();


        auto& parsedProfiles =
            rawUpdater->updated_order;


        // =====================================================
        // Safety barrier
        // =====================================================
        //
        // RawUpdater currently returns void, so an empty list
        // cannot distinguish:
        //
        //   - invalid/broken subscription
        //   - unsupported format
        //   - intentionally empty subscription
        //
        // For an existing subscription group the safest policy
        // is NOT to destroy the previous profiles.
        // =====================================================

        if (group &&
            parsedProfiles.isEmpty())
        {
            MW_show_log(
                "<<<<<<<< "
                +
                QObject::tr(
                    "Subscription contains no valid profiles. "
                    "The existing profiles were preserved."
                )
            );


            runOnUiThread(
                []
                {
                    MessageBoxWarning(
                        QObject::tr(
                            "Subscription update failed"
                        ),

                        QObject::tr(
                            "No valid profiles were found "
                            "in the subscription. "
                            "The existing profiles were preserved."
                        )
                    );
                }
            );


            return;
        }


        // =====================================================
        // Simple import, not subscription replacement
        // =====================================================

        if (!group)
        {
            if (!parsedProfiles.isEmpty())
            {
                const bool added =
                    profilesRepo->AddProfileBatch(
                        parsedProfiles,
                        rawUpdater->gid_add_to
                    );


                if (!added)
                {
                    MW_show_log(
                        "GroupUpdater: failed to "
                        "add imported profiles"
                    );


                    runOnUiThread(
                        []
                        {
                            MessageBoxWarning(
                                "Internal Error",
                                "DB Error when adding profiles. "
                                "Please try again."
                            );
                        }
                    );


                    return;
                }
            }


            settings->imported_count =
                parsedProfiles.count();


            MW_dialog_message(
                MwMessage::SubscriptionFinished,
                {}
            );


            return;
        }


        // =====================================================
        // Existing subscription group
        //
        // Snapshot OLD state only AFTER network + parsing.
        // This keeps the race window much smaller.
        // =====================================================

        const QList<int> oldProfileIds =
            group->Profiles();


        const QList<
            std::shared_ptr<Configs::Profile>
        > oldProfiles =
            profilesRepo->GetProfileBatch(
                oldProfileIds
            );


        // =====================================================
        // Small safe display helper
        // =====================================================

        const auto profileDisplayName =
            [](
                const std::shared_ptr<
                Configs::Profile
                >& profile) -> QString
            {
                if (!profile)
                {
                    return {};
                }


                const auto config =
                    profile->ConfigSnapshot();


                if (config.displayName.isEmpty() &&
                    config.displayType.isEmpty())
                {
                    return profile->Name();
                }


                return QString("[%1] %2")
                    .arg(
                        config.displayType,
                        config.displayName
                    );
            };


        // =====================================================
        // State prepared before committing anything
        // =====================================================

        QList<
            std::shared_ptr<Configs::Profile>
        > profilesToAdd;


        QList<int> profilesToDelete;


        QList<int> finalProfileOrder;


        QString changeText;


        const bool clearEverything =
            settings->sub_clear;


        // =====================================================
        // MODE 1:
        // sub_clear == true
        //
        // Completely recreate subscription profiles.
        //
        // Old profiles are NOT deleted yet.
        // =====================================================

        if (clearEverything)
        {
            profilesToAdd =
                parsedProfiles;


            profilesToDelete =
                oldProfileIds;


            // -------------------------------------------------
            // Add NEW profiles first.
            //
            // This guarantees that an AddProfileBatch failure
            // cannot destroy the previous subscription.
            // -------------------------------------------------

            if (!profilesToAdd.isEmpty())
            {
                const bool added =
                    profilesRepo
                    ->AddProfileBatch(
                        profilesToAdd,
                        _sub_gid
                    );


                if (!added)
                {
                    MW_show_log(
                        "GroupUpdater: AddProfileBatch "
                        "failed while replacing subscription"
                    );


                    runOnUiThread(
                        []
                        {
                            MessageBoxWarning(
                                "Internal Error",

                                "DB Error when adding new "
                                "subscription profiles. "
                                "The previous profiles "
                                "were preserved."
                            );
                        }
                    );


                    return;
                }
            }


            // -------------------------------------------------
            // Validate AddProfileBatch result
            //
            // At this point every parsed Profile MUST have ID.
            // -------------------------------------------------

            bool allAssigned =
                true;


            for (const auto& profile :
                profilesToAdd)
            {
                if (!profile ||
                    profile->Id() < 0)
                {
                    allAssigned =
                        false;

                    break;
                }
            }


            if (!allAssigned)
            {
                // ---------------------------------------------
                // Roll back newly assigned profiles.
                // Old profiles are still untouched.
                // ---------------------------------------------

                QList<int> rollbackIds;


                for (const auto& profile :
                    profilesToAdd)
                {
                    if (!profile)
                    {
                        continue;
                    }


                    const int id =
                        profile->Id();


                    if (id >= 0)
                    {
                        rollbackIds.append(
                            id
                        );
                    }
                }


                if (!rollbackIds.isEmpty())
                {
                    profilesRepo
                        ->BatchDeleteProfiles(
                            rollbackIds,
                            false
                        );
                }


                group->ReplaceProfiles(
                    oldProfileIds
                );


                groupsRepo->Save(
                    group
                );


                MW_show_log(
                    "GroupUpdater: incomplete batch "
                    "insert; rolled back new profiles"
                );


                return;
            }


            // -------------------------------------------------
            // Exact order from remote subscription.
            // -------------------------------------------------

            finalProfileOrder.reserve(
                parsedProfiles.size()
            );


            for (const auto& profile :
                parsedProfiles)
            {
                if (!profile)
                {
                    continue;
                }


                const int id =
                    profile->Id();


                if (id >= 0)
                {
                    finalProfileOrder.append(
                        id
                    );
                }
            }


            // -------------------------------------------------
            // Change description
            // -------------------------------------------------

            if (parsedProfiles.size() >= 1000)
            {
                changeText +=
                    "[+] "
                    +
                    Int2String(
                        parsedProfiles.size()
                    )
                    +
                    " profiles\n";
            }
            else
            {
                for (const auto& profile :
                    parsedProfiles)
                {
                    changeText +=
                        "[+] "
                        +
                        profileDisplayName(
                            profile
                        )
                        +
                        "\n";
                }
            }
        }


        // =====================================================
        // MODE 2:
        // sub_clear == false
        //
        // Reuse unchanged OLD Profiles.
        //
        // This preserves:
        //   - Profile ID
        //   - traffic counters
        //   - latency/test data
        //   - runtime identity
        //
        // Only genuinely new profiles are inserted.
        // =====================================================

        else
        {
            QList<
                std::shared_ptr<Configs::Profile>
            > oldCommon;


            QList<
                std::shared_ptr<Configs::Profile>
            > newCommon;


            QList<
                std::shared_ptr<Configs::Profile>
            > onlyOld;


            QList<
                std::shared_ptr<Configs::Profile>
            > onlyNew;


            // -------------------------------------------------
            // OLD ∩ NEW
            //
            // ProfileFilter::Common preserves alignment:
            //
            // oldCommon[i] corresponds to newCommon[i].
            // -------------------------------------------------

            Configs::ProfileFilter::Common(
                oldProfiles,
                parsedProfiles,
                oldCommon,
                newCommon,
                false
            );


            // OLD - NEW
            Configs::ProfileFilter::OnlyInSrc(
                oldProfiles,
                parsedProfiles,
                onlyOld,
                false
            );


            // NEW - OLD
            Configs::ProfileFilter::OnlyInSrc(
                parsedProfiles,
                oldProfiles,
                onlyNew,
                false
            );


            // Only genuinely NEW profiles need DB IDs.
            profilesToAdd =
                onlyNew;


            // Only disappeared OLD profiles need deletion.
            profilesToDelete.reserve(
                onlyOld.size()
            );


            for (const auto& profile :
                onlyOld)
            {
                if (!profile)
                {
                    continue;
                }


                const int id =
                    profile->Id();


                if (id >= 0)
                {
                    profilesToDelete.append(
                        id
                    );
                }
            }


            // -------------------------------------------------
            // Map:
            //
            // parsed new Profile pointer
            //          ↓
            // existing old Profile ID
            //
            // This allows us to reproduce remote order while
            // keeping old Profile objects for unchanged nodes.
            // -------------------------------------------------

            QHash<
                const Configs::Profile*,
                int
            > reusedProfileIds;


            const int commonCount =
                std::min(
                    oldCommon.size(),
                    newCommon.size()
                );


            reusedProfileIds.reserve(
                commonCount
            );


            for (int i = 0;
                i < commonCount;
                ++i)
            {
                const auto& oldProfile =
                    oldCommon[i];

                const auto& newProfile =
                    newCommon[i];


                if (!oldProfile ||
                    !newProfile)
                {
                    continue;
                }


                const int oldId =
                    oldProfile->Id();


                if (oldId < 0)
                {
                    continue;
                }


                reusedProfileIds.insert(
                    newProfile.get(),
                    oldId
                );
            }


            // -------------------------------------------------
            // Persist only NEW profiles.
            // -------------------------------------------------

            if (!profilesToAdd.isEmpty())
            {
                const bool added =
                    profilesRepo
                    ->AddProfileBatch(
                        profilesToAdd,
                        _sub_gid
                    );


                if (!added)
                {
                    MW_show_log(
                        "GroupUpdater: failed to add "
                        "new profiles during merge"
                    );


                    runOnUiThread(
                        []
                        {
                            MessageBoxWarning(
                                "Internal Error",

                                "DB Error when adding "
                                "new subscription profiles. "
                                "The previous subscription "
                                "was preserved."
                            );
                        }
                    );


                    return;
                }
            }


            // -------------------------------------------------
            // Validate that every genuinely new profile
            // received an ID.
            // -------------------------------------------------

            bool allAssigned =
                true;


            for (const auto& profile :
                profilesToAdd)
            {
                if (!profile ||
                    profile->Id() < 0)
                {
                    allAssigned =
                        false;

                    break;
                }
            }


            if (!allAssigned)
            {
                QList<int> rollbackIds;


                for (const auto& profile :
                    profilesToAdd)
                {
                    if (!profile)
                    {
                        continue;
                    }


                    const int id =
                        profile->Id();


                    if (id >= 0)
                    {
                        rollbackIds.append(
                            id
                        );
                    }
                }


                if (!rollbackIds.isEmpty())
                {
                    profilesRepo
                        ->BatchDeleteProfiles(
                            rollbackIds,
                            false
                        );
                }


                group->ReplaceProfiles(
                    oldProfileIds
                );


                groupsRepo->Save(
                    group
                );


                MW_show_log(
                    "GroupUpdater: incomplete batch "
                    "insert during subscription merge"
                );


                return;
            }


            // -------------------------------------------------
            // Rebuild exact remote order.
            //
            // If remote profile is unchanged:
            //      use OLD ID.
            //
            // If remote profile is new:
            //      use newly assigned ID.
            // -------------------------------------------------

            finalProfileOrder.reserve(
                parsedProfiles.size()
            );


            for (const auto& parsedProfile :
                parsedProfiles)
            {
                if (!parsedProfile)
                {
                    continue;
                }


                const auto reusedIt =
                    reusedProfileIds.constFind(
                        parsedProfile.get()
                    );


                if (reusedIt !=
                    reusedProfileIds.constEnd())
                {
                    finalProfileOrder.append(
                        reusedIt.value()
                    );

                    continue;
                }


                const int newId =
                    parsedProfile->Id();


                if (newId < 0)
                {
                    MW_show_log(
                        "GroupUpdater: new profile "
                        "does not have an ID"
                    );


                    continue;
                }


                finalProfileOrder.append(
                    newId
                );
            }


            // -------------------------------------------------
            // Added/deleted profile description
            // -------------------------------------------------

            QString noticeAdded;
            QString noticeDeleted;


            if (onlyNew.size() < 1000)
            {
                for (const auto& profile :
                    onlyNew)
                {
                    noticeAdded +=
                        "[+] "
                        +
                        profileDisplayName(
                            profile
                        )
                        +
                        "\n";
                }
            }
            else
            {
                noticeAdded +=
                    "[+] added "
                    +
                    Int2String(
                        onlyNew.size()
                    )
                    +
                    "\n";
            }


            if (onlyOld.size() < 1000)
            {
                for (const auto& profile :
                    onlyOld)
                {
                    noticeDeleted +=
                        "[-] "
                        +
                        profileDisplayName(
                            profile
                        )
                        +
                        "\n";
                }
            }
            else
            {
                noticeDeleted +=
                    "[-] deleted "
                    +
                    Int2String(
                        onlyOld.size()
                    )
                    +
                    "\n";
            }


            changeText =
                "\n"
                +
                QObject::tr(
                    "Added %1 profiles:\n"
                    "%2\n"
                    "Deleted %3 Profiles:\n"
                    "%4"
                )
                .arg(
                    onlyNew.length()
                )
                .arg(
                    noticeAdded
                )
                .arg(
                    onlyOld.length()
                )
                .arg(
                    noticeDeleted
                );


            if (onlyNew.isEmpty() &&
                onlyOld.isEmpty())
            {
                changeText =
                    QObject::tr(
                        "Nothing"
                    );
            }
        }


        // =====================================================
        // Sanity check before publishing
        // =====================================================

        if (finalProfileOrder.isEmpty())
        {
            // parsedProfiles was non-empty, therefore reaching
            // an empty final order means an internal failure.
            //
            // Never replace existing subscription with empty
            // state in this situation.

            QList<int> rollbackIds;


            for (const auto& profile :
                profilesToAdd)
            {
                if (!profile)
                {
                    continue;
                }


                const int id =
                    profile->Id();


                if (id >= 0)
                {
                    rollbackIds.append(
                        id
                    );
                }
            }


            if (!rollbackIds.isEmpty())
            {
                profilesRepo
                    ->BatchDeleteProfiles(
                        rollbackIds,
                        false
                    );
            }


            group->ReplaceProfiles(
                oldProfileIds
            );


            groupsRepo->Save(
                group
            );


            MW_show_log(
                "GroupUpdater: refusing to replace "
                "subscription with empty final order"
            );


            return;
        }


        // =====================================================
        // Publish NEW group order
        //
        // Important:
        // New profiles already exist in DB.
        // Old profiles have NOT been deleted yet.
        //
        // Therefore there is no moment where the group loses
        // all valid profiles because parsing/insertion failed.
        // =====================================================

        group->ReplaceProfilesFromSubscription(
            finalProfileOrder
        );


        if (!groupsRepo->Save(
            group))
        {
            MW_show_log(
                "GroupUpdater: failed to save "
                "new subscription order"
            );


            // Try to restore previous group order.
            group->ReplaceProfiles(
                oldProfileIds
            );


            groupsRepo->Save(
                group
            );


            // Remove only profiles created during this attempt.
            QList<int> rollbackIds;


            for (const auto& profile :
                profilesToAdd)
            {
                if (!profile)
                {
                    continue;
                }


                const int id =
                    profile->Id();


                if (id >= 0)
                {
                    rollbackIds.append(
                        id
                    );
                }
            }


            if (!rollbackIds.isEmpty())
            {
                profilesRepo
                    ->BatchDeleteProfiles(
                        rollbackIds,
                        false
                    );
            }


            runOnUiThread(
                []
                {
                    MessageBoxWarning(
                        "Internal Error",

                        "Failed to save the updated "
                        "subscription order. "
                        "The previous order was restored."
                    );
                }
            );


            return;
        }


        // =====================================================
        // Delete obsolete OLD profiles LAST
        //
        // This is the key fix.
        // =====================================================

        if (!profilesToDelete.isEmpty())
        {
            QList<int> deleteIds =
                profilesToDelete;


            const bool deleted =
                profilesRepo
                ->BatchDeleteProfiles(
                    deleteIds,
                    settings
                    ->allow_stopping_active_profile
                );


            if (!deleted)
            {
                // The new subscription itself is already valid
                // and published.
                //
                // Failure here means stale DB rows may remain,
                // but we DO NOT destroy the valid new state.

                MW_show_log(
                    "GroupUpdater: failed to remove "
                    "some obsolete subscription profiles"
                );


                runOnUiThread(
                    []
                    {
                        MessageBoxWarning(
                            "Internal Error",

                            "The subscription was updated, "
                            "but some obsolete profiles "
                            "could not be removed."
                        );
                    }
                );
            }
        }


        // =====================================================
        // Persist SUCCESS state LAST
        //
        // At this point:
        //
        // 1. Subscription was downloaded successfully.
        // 2. Subscription was parsed successfully.
        // 3. New Profiles were inserted.
        // 4. Final profile order was constructed.
        // 5. Final profile order was persisted.
        // 6. Obsolete profiles were handled.
        //
        // Only now are we allowed to mark the subscription
        // as successfully updated.
        // =====================================================

        const qint64 completedAt =
            QDateTime::currentSecsSinceEpoch();


        if (!groupsRepo->CommitSubscriptionState(
            group,
            completedAt,
            subUserInfo))
        {
            MW_show_log(
                "GroupUpdater: subscription profiles "
                "were updated, but failed to persist "
                "subscription success state"
            );


            runOnUiThread(
                []
                {
                    MessageBoxWarning(
                        QObject::tr(
                            "Subscription update warning"
                        ),

                        QObject::tr(
                            "The subscription profiles were updated, "
                            "but Gryph could not save the subscription "
                            "update state.\n\n"
                            "The last update timestamp was not advanced."
                        )
                    );
                }
            );


            // IMPORTANT:
            //
            // Do NOT emit SubscriptionFinished here.
            //
            // The profile update itself is already valid,
            // but its completion metadata was not persisted.
            //
            // Keeping the old timestamp is preferable because
            // automatic update logic can retry later.

            return;
        }


        // =====================================================
        // Completion log
        // =====================================================

        const auto finalGroupSnapshot =
            group->Snapshot();


        MW_show_log(
            "<<<<<<<< "
            +
            QObject::tr(
                "Change of %1:"
            )
            .arg(
                finalGroupSnapshot.name
            )
            +
            "\n"
            +
            changeText
        );


        MW_dialog_message(
            MwMessage::SubscriptionFinished,
            {
                MwArg::Quiet
            }
        );
    }

} // namespace Subscription

std::atomic_bool
UI_update_all_groups_Updating{
    false
};

static bool shouldSkipGroup(
    const std::shared_ptr<Configs::Group>& group,
    bool onlyAllowed)
{
    if (!group) {
        return true;
    }

    const auto snapshot =
        group->Snapshot();

    return
        snapshot.url.isEmpty()
        ||
        snapshot.archive
        ||
        (
            onlyAllowed
            &&
            snapshot.skip_auto_update
            );
}

void serialUpdateSubscription(
    const QList<int>& groupsTabOrder,
    int order,
    bool onlyAllowed)
{
    if (order >=
        groupsTabOrder.size())
    {
        UI_update_all_groups_Updating.store(
            false,
            std::memory_order_release
        );

        return;
    }

    // -------------------------------------------------
    // Current group
    // -------------------------------------------------
    auto group =
        Configs::dataManager
        ->groupsRepo
        ->GetGroup(
            groupsTabOrder[order]
        );

    if (shouldSkipGroup(
        group,
        onlyAllowed))
    {
        serialUpdateSubscription(
            groupsTabOrder,
            order + 1,
            onlyAllowed
        );

        return;
    }

    // -------------------------------------------------
    // Find next eligible group
    // -------------------------------------------------
    int nextOrder =
        order + 1;

    while (nextOrder <
        groupsTabOrder.size())
    {
        const int nextGid =
            groupsTabOrder[
                nextOrder
            ];

        auto nextGroup =
            Configs::dataManager
            ->groupsRepo
            ->GetGroup(
                nextGid
            );

        if (!shouldSkipGroup(
            nextGroup,
            onlyAllowed))
        {
            break;
        }

        ++nextOrder;
    }

    // -------------------------------------------------
    // Snapshot before worker starts
    // -------------------------------------------------

    const auto groupSnapshot =
        group->Snapshot();

    // Update itself performs another archive check,
    // so a later state change is still handled.
    Subscription::groupUpdater
        ->AsyncUpdate(
            groupSnapshot.url,
            groupSnapshot.id,

            [=] {

                serialUpdateSubscription(
                    groupsTabOrder,
                    nextOrder,
                    onlyAllowed
                );
            }
        );
}

void UI_update_all_groups(
    bool onlyAllowed)
{
    // Atomically test AND set.
    //
    // Only one update chain may enter.
    if (UI_update_all_groups_Updating.exchange(
        true,
        std::memory_order_acq_rel))
    {
        MW_show_log(
            "The last subscription "
            "update has not exited."
        );

        return;
    }

    const auto groupsTabOrder =
        Configs::dataManager
        ->groupsRepo
        ->GetGroupsTabOrder();

    if (groupsTabOrder.isEmpty()) {

        UI_update_all_groups_Updating.store(
            false,
            std::memory_order_release
        );

        return;
    }

    serialUpdateSubscription(
        groupsTabOrder,
        0,
        onlyAllowed
    );
}