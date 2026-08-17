#include "include/ui/profile/edit_tuic.h"


EditTuic::EditTuic(
    QWidget* parent)
    :
    QWidget(parent),
    ui(new Ui::EditTuic)
{
    ui->setupUi(this);
}


EditTuic::~EditTuic()
{
    delete ui;
}


void EditTuic::onStart(
    std::shared_ptr<Configs::Profile> _ent)
{
    ent = _ent;

    if (!ent)
    {
        return;
    }


    // Read TUIC configuration from a detached copy.
    const auto outbound =
        ent->OutboundCloneAs<
        Configs::tuic
        >();

    if (!outbound)
    {
        return;
    }


    ui->uuid->setText(
        outbound->uuid
    );

    ui->password->setText(
        outbound->password
    );

    ui->congestion_control->setCurrentText(
        outbound->congestion_control.isEmpty()
        ? QStringLiteral("bbr")
        : outbound->congestion_control
    );

    ui->udp_relay_mode->setCurrentText(
        outbound->udp_relay_mode.isEmpty()
        ? QStringLiteral("native")
        : outbound->udp_relay_mode
    );

    ui->udp_over_stream->setChecked(
        outbound->udp_over_stream
    );

    ui->zero_rtt->setChecked(
        outbound->zero_rtt_handshake
    );

    ui->heartbeat->setText(
        outbound->heartbeat
    );
}


bool EditTuic::onEnd()
{
    if (!ent)
    {
        return false;
    }


    // Freeze UI values before modifying Profile.
    const QString uuid =
        ui->uuid->text();

    const QString password =
        ui->password->text();

    const QString congestionControl =
        ui->congestion_control
        ->currentText();

    const QString udpRelayMode =
        ui->udp_relay_mode
        ->currentText();

    const bool udpOverStream =
        ui->udp_over_stream
        ->isChecked();

    const bool zeroRttHandshake =
        ui->zero_rtt
        ->isChecked();

    const QString heartbeat =
        ui->heartbeat->text();


    // Atomically update TUIC configuration.
    return ent
        ->MutateOutbound<
        Configs::tuic
        >(
            [
                uuid,
                password,
                congestionControl,
                udpRelayMode,
                udpOverStream,
                zeroRttHandshake,
                heartbeat
            ](
                Configs::tuic& outbound
                ) -> bool
            {
                outbound.uuid =
                    uuid;

                outbound.password =
                    password;

                outbound.congestion_control =
                    congestionControl;

                outbound.udp_relay_mode =
                    udpRelayMode;

                outbound.udp_over_stream =
                    udpOverStream;

                outbound.zero_rtt_handshake =
                    zeroRttHandshake;

                outbound.heartbeat =
                    heartbeat;

                return true;
            }
        );
}