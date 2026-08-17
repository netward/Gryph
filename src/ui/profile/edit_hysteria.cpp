#include "include/ui/profile/edit_hysteria.h"


EditHysteria::EditHysteria(
    QWidget* parent)
    :
    QWidget(parent),
    ui(new Ui::EditHysteria)
{
    ui->setupUi(this);

    _protocol_version =
        ui->protocol_version;
}


EditHysteria::~EditHysteria()
{
    delete ui;
}


void EditHysteria::onStart(
    std::shared_ptr<Configs::Profile> _ent)
{
    ent = _ent;

    if (!ent)
    {
        return;
    }


    // =====================================================
    // Read Hysteria configuration from a detached copy.
    // =====================================================

    const auto outbound =
        ent->OutboundCloneAs<
        Configs::hysteria
        >();

    if (!outbound)
    {
        return;
    }


    // =====================================================
    // Load UI
    // =====================================================

    ui->protocol_version->setCurrentText(
        outbound->protocol_version
    );

    ui->server_ports->setText(
        outbound->server_ports.join(",")
    );

    ui->hop_interval->setText(
        outbound->hop_interval
    );

    ui->up_mbps->setText(
        Int2String(
            outbound->up_mbps
        )
    );

    ui->down_mbps->setText(
        Int2String(
            outbound->down_mbps
        )
    );

    ui->obfs->setText(
        outbound->obfs
    );

    ui->auth_type->setCurrentText(
        outbound->auth_type
    );

    ui->auth->setText(
        outbound->auth
    );

    ui->recv_window->setText(
        Int2String(
            outbound->recv_window
        )
    );

    ui->recv_window_conn->setText(
        Int2String(
            outbound->recv_window_conn
        )
    );

    ui->disable_mtu_discovery->setChecked(
        outbound->disable_mtu_discovery
    );

    ui->password->setText(
        outbound->password
    );


    editHysteriaLayout(
        outbound->protocol_version
    );
}


bool EditHysteria::onEnd()
{
    if (!ent)
    {
        return false;
    }


    // =====================================================
    // Freeze UI values before Profile mutation.
    // =====================================================

    const QString protocolVersion =
        ui->protocol_version
        ->currentText();

    const QStringList serverPorts =
        SplitAndTrim(
            ui->server_ports->text(),
            ",",
            false
        );

    const QString hopInterval =
        ui->hop_interval->text();

    const int upMbps =
        ui->up_mbps
        ->text()
        .toInt();

    const int downMbps =
        ui->down_mbps
        ->text()
        .toInt();

    const QString obfs =
        ui->obfs->text();

    const QString authType =
        ui->auth_type
        ->currentText();

    const QString auth =
        ui->auth->text();

    const int recvWindow =
        ui->recv_window
        ->text()
        .toInt();

    const int recvWindowConn =
        ui->recv_window_conn
        ->text()
        .toInt();

    const bool disableMtuDiscovery =
        ui->disable_mtu_discovery
        ->isChecked();

    const QString password =
        ui->password->text();


    // =====================================================
    // Atomically update Hysteria configuration.
    //
    // Do NOT use OutboundCloneAs() for writing:
    // it would modify only a detached copy.
    // =====================================================

    return ent
        ->MutateOutbound<
        Configs::hysteria
        >(
            [
                protocolVersion,
                serverPorts,
                hopInterval,
                upMbps,
                downMbps,
                obfs,
                authType,
                auth,
                recvWindow,
                recvWindowConn,
                disableMtuDiscovery,
                password
            ](
                Configs::hysteria& outbound
                ) -> bool
    {
        outbound.protocol_version =
            protocolVersion;

        outbound.server_ports =
            serverPorts;

        outbound.hop_interval =
            hopInterval;

        outbound.up_mbps =
            upMbps;

        outbound.down_mbps =
            downMbps;

        outbound.obfs =
            obfs;

        outbound.auth_type =
            authType;

        outbound.auth =
            auth;

        outbound.recv_window =
            recvWindow;

        outbound.recv_window_conn =
            recvWindowConn;

        outbound.disable_mtu_discovery =
            disableMtuDiscovery;

        outbound.password =
            password;


        return true;
    }
        );
}


void EditHysteria::editHysteriaLayout(
    const QString& version)
{
    if (version == "1")
    {
        ui->auth_type->setVisible(true);
        ui->auth_type_l->setVisible(true);

        ui->auth->setVisible(true);
        ui->auth_l->setVisible(true);

        ui->recv_window_conn->setVisible(true);
        ui->recv_window_conn_l->setVisible(true);

        ui->recv_window->setVisible(true);
        ui->recv_window_l->setVisible(true);

        ui->disable_mtu_discovery->setVisible(true);

        ui->password->setVisible(false);
        ui->password_l->setVisible(false);
    }
    else
    {
        ui->auth_type->setVisible(false);
        ui->auth_type_l->setVisible(false);

        ui->auth->setVisible(false);
        ui->auth_l->setVisible(false);

        ui->recv_window_conn->setVisible(false);
        ui->recv_window_conn_l->setVisible(false);

        ui->recv_window->setVisible(false);
        ui->recv_window_l->setVisible(false);

        ui->disable_mtu_discovery->setVisible(false);

        ui->password->setVisible(true);
        ui->password_l->setVisible(true);
    }
}