#include "include/ui/profile/edit_trusttunnel.h"


EditTrustTunnel::EditTrustTunnel(
    QWidget* parent)
    :
    QWidget(parent),
    ui(new Ui::EditTrustTunnel)
{
    ui->setupUi(this);
}


EditTrustTunnel::~EditTrustTunnel()
{
    delete ui;
}


void EditTrustTunnel::onStart(
    std::shared_ptr<Configs::Profile> _ent)
{
    ent = _ent;

    if (!ent)
    {
        return;
    }


    // Read TrustTunnel configuration from a detached copy.
    const auto outbound =
        ent->OutboundCloneAs<
        Configs::trusttunnel
        >();

    if (!outbound)
    {
        return;
    }


    ui->username->setText(
        outbound->username
    );

    ui->password->setText(
        outbound->password
    );

    ui->health_check->setChecked(
        outbound->health_check
    );

    ui->quic->setChecked(
        outbound->quic
    );

    ui->congestion_control->setCurrentText(
        outbound->congestion_control.isEmpty()
        ? QStringLiteral("bbr")
        : outbound->congestion_control
    );
}


bool EditTrustTunnel::onEnd()
{
    if (!ent)
    {
        return false;
    }


    // Freeze UI values before modifying Profile.
    const QString username =
        ui->username->text();

    const QString password =
        ui->password->text();

    const bool healthCheck =
        ui->health_check->isChecked();

    const bool quic =
        ui->quic->isChecked();

    const QString congestionControl =
        ui->congestion_control
        ->currentText();


    // Atomically update TrustTunnel configuration.
    return ent
        ->MutateOutbound<
        Configs::trusttunnel
        >(
            [
                username,
                password,
                healthCheck,
                quic,
                congestionControl
            ](
                Configs::trusttunnel& outbound
                ) -> bool
            {
                outbound.username =
                    username;

                outbound.password =
                    password;

                outbound.health_check =
                    healthCheck;

                outbound.quic =
                    quic;

                outbound.congestion_control =
                    congestionControl;

                return true;
            }
        );
}