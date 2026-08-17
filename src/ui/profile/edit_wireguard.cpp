#include "include/ui/profile/edit_wireguard.h"

#include "include/api/RPC.h"
#include "include/configs/sub/WARP.h"
#include "include/global/Utils.hpp"


EditWireguard::EditWireguard(
    QWidget* parent)
    :
    QWidget(parent),
    ui(new Ui::EditWireguard)
{
    ui->setupUi(this);


    connect(
        ui->warp_autogen,
        &QPushButton::clicked,
        this,
        [this]()
        {
            const QString originalText =
                ui->warp_autogen->text();


            // genWarpConfig blocks on a nested event loop,
            // so prevent a second request from double click.
            ui->warp_autogen->setEnabled(
                false
            );

            ui->warp_autogen->setText(
                tr("Getting keypair...")
            );


            bool ok = false;

            const auto keyPair =
                API::defaultClient
                ->GenWgKeyPair(
                    &ok
                );


            if (!ok)
            {
                runOnUiThread(
                    [this, keyPair]()
                    {
                        MessageBoxWarning(
                            tr("Failed to get key pair"),
                            keyPair.error
                            ? keyPair.error->c_str()
                            : ""
                        );
                    }
                );


                ui->warp_autogen->setText(
                    originalText
                );

                ui->warp_autogen->setEnabled(
                    true
                );

                return;
            }


            ui->warp_autogen->setText(
                tr("Generating config...")
            );


            QString error;


            const auto conf =
                Configs_network::genWarpConfig(
                    &error,
                    keyPair.private_key->c_str(),
                    keyPair.public_key->c_str()
                );


            if (!error.isEmpty() ||
                !conf)
            {
                runOnUiThread(
                    [this, error]()
                    {
                        MessageBoxWarning(
                            tr(
                                "Failed to generate "
                                "WARP config"
                            ),
                            error
                        );
                    }
                );


                ui->warp_autogen->setText(
                    originalText
                );

                ui->warp_autogen->setEnabled(
                    true
                );

                return;
            }


            ui->private_key->setText(
                conf->privateKey
            );

            ui->public_key->setText(
                conf->publicKey
            );

            ui->local_addr->setText(
                conf->ipv4Address
                +
                "/32,"
                +
                conf->ipv6Address
                +
                "/128"
            );

            ui->mtu->setText(
                "1280"
            );

            ui->persistent_keepalive->setText(
                "30"
            );


            // Endpoint is stored in outer profile editor.
            const int separator =
                conf->endpoint.lastIndexOf(
                    ':'
                );


            if (separator > 0)
            {
                if (set_edit_text_serverAddress)
                {
                    set_edit_text_serverAddress(
                        conf->endpoint.left(
                            separator
                        )
                    );
                }


                if (set_edit_text_serverPort)
                {
                    set_edit_text_serverPort(
                        conf->endpoint.mid(
                            separator + 1
                        )
                    );
                }
            }


            ui->warp_autogen->setText(
                tr("Success!")
            );


            setTimeout(
                [
                    this,
                    originalText
                ]()
                {
                    ui->warp_autogen->setText(
                        originalText
                    );

                    ui->warp_autogen->setEnabled(
                        true
                    );
                },
                this,
                2000
            );
        }
    );
}


EditWireguard::~EditWireguard()
{
    delete ui;
}


void EditWireguard::onStart(
    std::shared_ptr<Configs::Profile> _ent)
{
    ent =
        _ent;


    if (!ent)
    {
        return;
    }


    // =====================================================
    // Read WireGuard configuration from detached copy.
    // =====================================================

    const auto outbound =
        ent->OutboundCloneAs<
        Configs::wireguard
        >();


    if (!outbound)
    {
        return;
    }


#ifndef Q_OS_LINUX
    adjustSize();
#endif


    // =====================================================
    // Main WireGuard fields
    // =====================================================

    ui->private_key->setText(
        outbound->private_key
    );


    ui->mtu->setText(
        Int2String(
            outbound->mtu
        )
    );


    ui->sys_ifc->setChecked(
        outbound->system
    );


    ui->local_addr->setText(
        outbound->address.join(",")
    );


    ui->workers->setText(
        Int2String(
            outbound->worker_count
        )
    );


    // =====================================================
    // Peer
    // =====================================================

    if (outbound->peer)
    {
        ui->public_key->setText(
            outbound->peer->public_key
        );


        ui->preshared_key->setText(
            outbound->peer->pre_shared_key
        );


        ui->reserved->setText(
            QListInt2QListString(
                outbound->peer->reserved
            )
            .join(",")
        );


        ui->persistent_keepalive->setText(
            Int2String(
                outbound
                ->peer
                ->persistent_keepalive
            )
        );
    }
    else
    {
        ui->public_key->clear();
        ui->preshared_key->clear();
        ui->reserved->clear();
        ui->persistent_keepalive->clear();
    }


    // =====================================================
    // AmneziaWG fields
    // =====================================================

    ui->enable_amnezia->setChecked(
        outbound->enable_amnezia
    );


    ui->jc->setText(
        Int2String(
            outbound->jc
        )
    );

    ui->jmin->setText(
        Int2String(
            outbound->jmin
        )
    );

    ui->jmax->setText(
        Int2String(
            outbound->jmax
        )
    );


    ui->s1->setText(
        Int2String(
            outbound->s1
        )
    );

    ui->s2->setText(
        Int2String(
            outbound->s2
        )
    );

    ui->s3->setText(
        Int2String(
            outbound->s3
        )
    );

    ui->s4->setText(
        Int2String(
            outbound->s4
        )
    );


    ui->h1->setText(
        outbound->h1
    );

    ui->h2->setText(
        outbound->h2
    );

    ui->h3->setText(
        outbound->h3
    );

    ui->h4->setText(
        outbound->h4
    );


    ui->i1->setText(
        outbound->i1
    );

    ui->i2->setText(
        outbound->i2
    );

    ui->i3->setText(
        outbound->i3
    );

    ui->i4->setText(
        outbound->i4
    );

    ui->i5->setText(
        outbound->i5
    );
}


bool EditWireguard::onEnd()
{
    if (!ent)
    {
        return false;
    }


    // =====================================================
    // Freeze UI values before Profile mutation.
    // =====================================================

    const QString privateKey =
        ui->private_key
        ->text();


    const QString publicKey =
        ui->public_key
        ->text();


    const QString presharedKey =
        ui->preshared_key
        ->text();


    QList<int> reserved;


    const QStringList reservedItems =
        ui->reserved
        ->text()
        .split(
            ",",
            Qt::SkipEmptyParts
        );


    for (const QString& item :
        reservedItems)
    {
        const QString trimmed =
            item.trimmed();


        if (trimmed.isEmpty())
        {
            continue;
        }


        reserved.append(
            trimmed.toInt()
        );
    }


    const int persistentKeepalive =
        ui->persistent_keepalive
        ->text()
        .trimmed()
        .toInt();


    const int mtu =
        ui->mtu
        ->text()
        .toInt();


    const bool system =
        ui->sys_ifc
        ->isChecked();


    QStringList addresses;


    const QStringList rawAddresses =
        ui->local_addr
        ->text()
        .split(
            ",",
            Qt::SkipEmptyParts
        );


    for (const QString& address :
        rawAddresses)
    {
        const QString trimmed =
            address.trimmed();


        if (!trimmed.isEmpty())
        {
            addresses.append(
                trimmed
            );
        }
    }


    const int workerCount =
        ui->workers
        ->text()
        .toInt();


    // =====================================================
    // AmneziaWG
    // =====================================================

    const bool enableAmnezia =
        ui->enable_amnezia
        ->isChecked();


    const int jc =
        ui->jc
        ->text()
        .toInt();

    const int jmin =
        ui->jmin
        ->text()
        .toInt();

    const int jmax =
        ui->jmax
        ->text()
        .toInt();


    const int s1 =
        ui->s1
        ->text()
        .toInt();

    const int s2 =
        ui->s2
        ->text()
        .toInt();

    const int s3 =
        ui->s3
        ->text()
        .toInt();

    const int s4 =
        ui->s4
        ->text()
        .toInt();


    const QString h1 =
        ui->h1->text();

    const QString h2 =
        ui->h2->text();

    const QString h3 =
        ui->h3->text();

    const QString h4 =
        ui->h4->text();


    const QString i1 =
        ui->i1->text();

    const QString i2 =
        ui->i2->text();

    const QString i3 =
        ui->i3->text();

    const QString i4 =
        ui->i4->text();

    const QString i5 =
        ui->i5->text();


    // =====================================================
    // Atomically update WireGuard configuration.
    // =====================================================

    return ent
        ->MutateOutbound<
        Configs::wireguard
        >(
            [
                privateKey,
                publicKey,
                presharedKey,
                reserved,
                persistentKeepalive,
                mtu,
                system,
                addresses,
                workerCount,
                enableAmnezia,
                jc,
                jmin,
                jmax,
                s1,
                s2,
                s3,
                s4,
                h1,
                h2,
                h3,
                h4,
                i1,
                i2,
                i3,
                i4,
                i5
            ](
                Configs::wireguard& outbound
                ) -> bool
    {
        // =========================================
        // Main configuration
        // =========================================

        outbound.private_key =
            privateKey;


        outbound.mtu =
            mtu;


        outbound.system =
            system;


        outbound.address =
            addresses;


        outbound.worker_count =
            workerCount;


        // =========================================
        // Peer
        // =========================================

        if (!outbound.peer)
        {
            return false;
        }


        outbound.peer->public_key =
            publicKey;


        outbound.peer->pre_shared_key =
            presharedKey;


        outbound.peer->reserved =
            reserved;


        outbound
            .peer
            ->persistent_keepalive =
            persistentKeepalive;


        // =========================================
        // AmneziaWG
        // =========================================

        outbound.enable_amnezia =
            enableAmnezia;


        outbound.jc =
            jc;

        outbound.jmin =
            jmin;

        outbound.jmax =
            jmax;


        outbound.s1 =
            s1;

        outbound.s2 =
            s2;

        outbound.s3 =
            s3;

        outbound.s4 =
            s4;


        outbound.h1 =
            h1;

        outbound.h2 =
            h2;

        outbound.h3 =
            h3;

        outbound.h4 =
            h4;


        outbound.i1 =
            i1;

        outbound.i2 =
            i2;

        outbound.i3 =
            i3;

        outbound.i4 =
            i4;

        outbound.i5 =
            i5;


        return true;
    }
        );
}