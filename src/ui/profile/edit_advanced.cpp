#include "include/ui/profile/edit_advanced.h"

#include <QInputDialog>
#include <QNetworkInterface>
#include <QAbstractSocket>
#include <QMessageBox>

#include "include/database/DatabaseManager.h"


EditAdvanced::EditAdvanced(
    QWidget* parent,
    const std::shared_ptr<Configs::Profile>& _ent)
    :
    QDialog(parent),
    ui(new Ui::EditAdvanced)
{
    ui->setupUi(this);

    ent = _ent;


    // =====================================================
    // Validate Profile
    // =====================================================

    if (!ent)
    {
        return;
    }


    // =====================================================
    // Take ONE detached outbound copy for UI initialization
    //
    // This dialog only reads from it in the constructor.
    // =====================================================

    const auto outbound =
        ent->OutboundClone();


    if (!outbound)
    {
        return;
    }


    // =====================================================
    // Dial fields
    // =====================================================

    const auto dialFieldsObj =
        outbound->dialFields;


    if (!dialFieldsObj)
    {
        return;
    }


    ui->reuse_addr->setChecked(
        dialFieldsObj->reuse_addr
    );

    ui->tcp_fast_open->setChecked(
        dialFieldsObj->tcp_fast_open
    );

    ui->udp_fragment->setChecked(
        dialFieldsObj->udp_fragment
    );

    ui->tcp_multipath->setChecked(
        dialFieldsObj->tcp_multi_path
    );

    ui->connect_timeout->setText(
        dialFieldsObj->connect_timeout
    );


    // =====================================================
    // Collect system network interfaces
    // =====================================================

    for (const auto& ifc :
        QNetworkInterface::allInterfaces())
    {
        m_systemInterfaces <<
            ifc.humanReadableName();
    }


    for (const auto& addr :
        QNetworkInterface::allAddresses())
    {
        if (addr.protocol() ==
            QAbstractSocket::IPv4Protocol)
        {
            m_systemIpv4Addresses <<
                addr.toString();
        }
        else if (
            addr.protocol() ==
            QAbstractSocket::IPv6Protocol)
        {
            m_systemIpv6Addresses <<
                addr.toString();
        }
    }


    // =====================================================
    // Bind combo helper
    // =====================================================

    const auto populateBindCombo =
        [](
            QComboBox* combo,
            const QStringList& systemItems,
            const QStringList& history,
            const QString& current)
        {
            if (!combo)
            {
                return;
            }


            combo->addItem(
                QString()
            );


            combo->addItems(
                systemItems
            );


            for (const auto& historyItem :
                history)
            {
                if (!systemItems.contains(
                    historyItem))
                {
                    combo->addItem(
                        historyItem
                    );
                }
            }


            combo->setCurrentText(
                current
            );
        };


    auto* repo =
        Configs::dataManager
        ->settingsRepo
        .get();


    if (repo)
    {
        populateBindCombo(
            ui->bind_interface,
            m_systemInterfaces,
            repo->dial_bind_interface_history,
            dialFieldsObj->bind_interface
        );


        populateBindCombo(
            ui->inet4_bind_address,
            m_systemIpv4Addresses,
            repo->dial_inet4_bind_address_history,
            dialFieldsObj->inet4_bind_address
        );


        populateBindCombo(
            ui->inet6_bind_address,
            m_systemIpv6Addresses,
            repo->dial_inet6_bind_address_history,
            dialFieldsObj->inet6_bind_address
        );
    }


    // =====================================================
    // TLS
    // =====================================================

    if (outbound->HasTLS())
    {
        const auto tlsObj =
            outbound->GetTLS();


        if (!tlsObj)
        {
            ui->tls_box->hide();
            adjustSize();

            return;
        }


        ui->disable_sni->setChecked(
            tlsObj->disable_sni
        );


        ui->min_version->setText(
            tlsObj->min_version
        );


        ui->max_version->setText(
            tlsObj->max_version
        );


        // -------------------------------------------------
        // ECH
        // -------------------------------------------------

        if (tlsObj->ech)
        {
            ui->enable_ech->setChecked(
                tlsObj->ech->enabled
            );


            ui->ech_server_name->setText(
                tlsObj->ech->serverName
            );


            if (!tlsObj->ech
                ->config
                .isEmpty())
            {
                ui->ech_config->setText(
                    tr("Already set")
                );


                CACHE.echConfig =
                    tlsObj->ech->config;
            }
        }


        // -------------------------------------------------
        // Certificate SHA256
        // -------------------------------------------------

        if (!tlsObj
            ->certificate_public_key_sha256
            .isEmpty())
        {
            ui->cert_sha256->setText(
                tr("Already set")
            );


            CACHE.certSha256 =
                tlsObj
                ->certificate_public_key_sha256;
        }


        // -------------------------------------------------
        // Client certificate
        // -------------------------------------------------

        if (!tlsObj
            ->client_certificate
            .isEmpty())
        {
            ui->client_cert->setText(
                tr("Already set")
            );


            CACHE.clientCert =
                tlsObj->client_certificate;
        }


        // -------------------------------------------------
        // Client key
        // -------------------------------------------------

        if (!tlsObj
            ->client_key
            .isEmpty())
        {
            ui->client_key->setText(
                tr("Already set")
            );


            CACHE.clientKey =
                tlsObj->client_key;
        }
    }
    else
    {
        ui->tls_box->hide();

        adjustSize();
    }
}


EditAdvanced::~EditAdvanced()
{
    delete ui;
}


void EditAdvanced::accept()
{
    if (!ent)
    {
        return;
    }


    // =====================================================
    // Freeze all UI values BEFORE configuration mutation
    // =====================================================

    const bool reuseAddr =
        ui->reuse_addr->isChecked();

    const bool tcpFastOpen =
        ui->tcp_fast_open->isChecked();

    const bool udpFragment =
        ui->udp_fragment->isChecked();

    const bool tcpMultipath =
        ui->tcp_multipath->isChecked();

    const QString connectTimeout =
        ui->connect_timeout->text();


    const QString bindInterface =
        ui->bind_interface
        ->currentText();

    const QString inet4BindAddress =
        ui->inet4_bind_address
        ->currentText();

    const QString inet6BindAddress =
        ui->inet6_bind_address
        ->currentText();


    // TLS values
    const bool disableSni =
        ui->disable_sni->isChecked();

    const QString minVersion =
        ui->min_version->text();

    const QString maxVersion =
        ui->max_version->text();

    const bool enableEch =
        ui->enable_ech->isChecked();

    const QString echServerName =
        ui->ech_server_name->text();


    // =====================================================
    // Atomically update Profile configuration
    //
    // MutateOutbound:
    //
    //   snapshot
    //      ↓
    //   detached outbound
    //      ↓
    //   modify
    //      ↓
    //   revision check
    //      ↓
    //   atomic pointer swap
    // =====================================================

    const bool updated =
        ent
        ->MutateOutbound<
        Configs::outbound
        >(
            [
                reuseAddr,
                tcpFastOpen,
                udpFragment,
                tcpMultipath,
                connectTimeout,
                bindInterface,
                inet4BindAddress,
                inet6BindAddress,
                disableSni,
                minVersion,
                maxVersion,
                enableEch,
                echServerName,
                echConfig = CACHE.echConfig,
                clientCert = CACHE.clientCert,
                clientKey = CACHE.clientKey,
                certSha256 = CACHE.certSha256
            ](
                Configs::outbound& outbound
                ) -> bool
    {
        // =========================================
        // Dial fields
        // =========================================

        const auto dialFieldsObj =
            outbound.dialFields;


        if (!dialFieldsObj)
        {
            return false;
        }


        dialFieldsObj->reuse_addr =
            reuseAddr;

        dialFieldsObj->tcp_fast_open =
            tcpFastOpen;

        dialFieldsObj->udp_fragment =
            udpFragment;

        dialFieldsObj->tcp_multi_path =
            tcpMultipath;

        dialFieldsObj->connect_timeout =
            connectTimeout;

        dialFieldsObj->bind_interface =
            bindInterface;

        dialFieldsObj->inet4_bind_address =
            inet4BindAddress;

        dialFieldsObj->inet6_bind_address =
            inet6BindAddress;


        // =========================================
        // TLS
        // =========================================

        if (outbound.HasTLS())
        {
            const auto tlsObj =
                outbound.GetTLS();


            if (!tlsObj)
            {
                return false;
            }


            tlsObj->disable_sni =
                disableSni;

            tlsObj->min_version =
                minVersion;

            tlsObj->max_version =
                maxVersion;


            // -------------------------------------
            // ECH
            // -------------------------------------

            if (tlsObj->ech)
            {
                tlsObj->ech->enabled =
                    enableEch;

                tlsObj->ech->serverName =
                    echServerName;

                tlsObj->ech->config =
                    echConfig;
            }


            // -------------------------------------
            // Certificates
            // -------------------------------------

            tlsObj->client_certificate =
                clientCert;

            tlsObj->client_key =
                clientKey;

            tlsObj
                ->certificate_public_key_sha256 =
                certSha256;
        }


        return true;
    }
        );


    if (!updated)
    {
        QMessageBox::warning(
            this,
            tr("Error"),
            tr(
                "Failed to update advanced "
                "profile settings."
            )
        );

        return;
    }


    // =====================================================
    // Update history
    //
    // Do this only AFTER successful Profile mutation.
    // =====================================================

    const auto updateHistory =
        [](
            QStringList& history,
            const QStringList& systemItems,
            const QString& value)
        {
            if (value.isEmpty() ||
                systemItems.contains(value))
            {
                return;
            }


            history.removeAll(
                value
            );


            history.prepend(
                value
            );


            if (history.size() > 5)
            {
                history =
                    history.mid(
                        0,
                        5
                    );
            }
        };


    auto* repo =
        Configs::dataManager
        ->settingsRepo
        .get();


    if (repo)
    {
        updateHistory(
            repo->dial_bind_interface_history,
            m_systemInterfaces,
            bindInterface
        );


        updateHistory(
            repo->dial_inet4_bind_address_history,
            m_systemIpv4Addresses,
            inet4BindAddress
        );


        updateHistory(
            repo->dial_inet6_bind_address_history,
            m_systemIpv6Addresses,
            inet6BindAddress
        );


        repo->Save();
    }


    QDialog::accept();
}


void EditAdvanced::on_ech_config_clicked()
{
    bool ok = false;


    const QString txt =
        QInputDialog::getMultiLineText(
            this,
            tr("ECH Config"),
            QString(),
            CACHE.echConfig.join("\n"),
            &ok
        );


    if (!ok)
    {
        return;
    }


    CACHE.echConfig =
        txt.split(
            "\n",
            Qt::SkipEmptyParts
        );


    ui->ech_config->setText(
        CACHE.echConfig.isEmpty()
        ? tr("Not Set")
        : tr("Already set")
    );
}


void EditAdvanced::on_client_cert_clicked()
{
    bool ok = false;


    const QString txt =
        QInputDialog::getMultiLineText(
            this,
            tr("Client Certificate"),
            QString(),
            CACHE.clientCert.join("\n"),
            &ok
        );


    if (!ok)
    {
        return;
    }


    CACHE.clientCert =
        txt.split(
            "\n",
            Qt::SkipEmptyParts
        );


    ui->client_cert->setText(
        CACHE.clientCert.isEmpty()
        ? tr("Not Set")
        : tr("Already set")
    );
}


void EditAdvanced::on_client_key_clicked()
{
    bool ok = false;


    const QString txt =
        QInputDialog::getMultiLineText(
            this,
            tr("Client Key"),
            QString(),
            CACHE.clientKey.join("\n"),
            &ok
        );


    if (!ok)
    {
        return;
    }


    CACHE.clientKey =
        txt.split(
            "\n",
            Qt::SkipEmptyParts
        );


    ui->client_key->setText(
        CACHE.clientKey.isEmpty()
        ? tr("Not Set")
        : tr("Already set")
    );
}


void EditAdvanced::on_cert_sha256_clicked()
{
    bool ok = false;


    const QString txt =
        QInputDialog::getMultiLineText(
            this,
            tr("Certificate sha256"),
            QString(),
            CACHE.certSha256.join("\n"),
            &ok
        );


    if (!ok)
    {
        return;
    }


    CACHE.certSha256 =
        txt.split(
            "\n",
            Qt::SkipEmptyParts
        );


    ui->cert_sha256->setText(
        CACHE.certSha256.isEmpty()
        ? tr("Not Set")
        : tr("Already set")
    );
}