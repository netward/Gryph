#include "include/ui/profile/edit_tailscale.h"


EditTailScale::EditTailScale(
    QWidget* parent)
    :
    QWidget(parent),
    ui(new Ui::EditTailScale)
{
    ui->setupUi(this);
}


EditTailScale::~EditTailScale()
{
    delete ui;
}


void EditTailScale::onStart(
    std::shared_ptr<Configs::Profile> _ent)
{
    ent = _ent;

    if (!ent)
    {
        return;
    }


    // =====================================================
    // Read Tailscale configuration from a detached copy.
    // =====================================================

    const auto outbound =
        ent->OutboundCloneAs<
        Configs::tailscale
        >();

    if (!outbound)
    {
        return;
    }


    ui->state_dir->setText(
        outbound->state_directory
    );

    ui->auth_key->setText(
        outbound->auth_key
    );

    ui->control_plane->setText(
        outbound->control_url
    );

    ui->ephemeral->setChecked(
        outbound->ephemeral
    );

    ui->hostname->setText(
        outbound->hostname
    );

    ui->accept_route->setChecked(
        outbound->accept_routes
    );

    ui->exit_node->setText(
        outbound->exit_node
    );

    ui->exit_node_lan_access->setChecked(
        outbound->exit_node_allow_lan_access
    );

    ui->advertise_routes->setText(
        outbound->advertise_routes.join(",")
    );

    ui->advertise_exit_node->setChecked(
        outbound->advertise_exit_node
    );

    ui->global_dns->setChecked(
        outbound->globalDNS
    );
}


bool EditTailScale::onEnd()
{
    if (!ent)
    {
        return false;
    }


    // =====================================================
    // Freeze UI values before Profile mutation.
    // =====================================================

    const QString stateDirectory =
        ui->state_dir->text();

    const QString authKey =
        ui->auth_key->text();

    const QString controlUrl =
        ui->control_plane->text();

    const bool ephemeral =
        ui->ephemeral->isChecked();

    const QString hostname =
        ui->hostname->text();

    const bool acceptRoutes =
        ui->accept_route->isChecked();

    const QString exitNode =
        ui->exit_node->text();

    const bool exitNodeAllowLanAccess =
        ui->exit_node_lan_access
        ->isChecked();


    // =====================================================
    // Normalize advertised routes.
    // =====================================================

    QStringList advertiseRoutes;

    const QString advertiseRoutesText =
        ui->advertise_routes
        ->text()
        .trimmed();

    if (!advertiseRoutesText.isEmpty())
    {
        advertiseRoutes =
            advertiseRoutesText.split(
                ",",
                Qt::SkipEmptyParts
            );

        for (QString& route :
            advertiseRoutes)
        {
            route =
                route.trimmed();
        }
    }


    const bool advertiseExitNode =
        ui->advertise_exit_node
        ->isChecked();

    const bool globalDNS =
        ui->global_dns
        ->isChecked();


    // =====================================================
    // Atomically update Tailscale configuration.
    // =====================================================

    return ent
        ->MutateOutbound<
        Configs::tailscale
        >(
            [
                stateDirectory,
                authKey,
                controlUrl,
                ephemeral,
                hostname,
                acceptRoutes,
                exitNode,
                exitNodeAllowLanAccess,
                advertiseRoutes,
                advertiseExitNode,
                globalDNS
            ](
                Configs::tailscale& outbound
                ) -> bool
    {
        outbound.state_directory =
            stateDirectory;

        outbound.auth_key =
            authKey;

        outbound.control_url =
            controlUrl;

        outbound.ephemeral =
            ephemeral;

        outbound.hostname =
            hostname;

        outbound.accept_routes =
            acceptRoutes;

        outbound.exit_node =
            exitNode;

        outbound.exit_node_allow_lan_access =
            exitNodeAllowLanAccess;

        outbound.advertise_routes =
            advertiseRoutes;

        outbound.advertise_exit_node =
            advertiseExitNode;

        outbound.globalDNS =
            globalDNS;

        return true;
    }
        );
}