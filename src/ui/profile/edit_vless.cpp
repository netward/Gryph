#include "include/ui/profile/edit_vless.h"


EditVless::EditVless(
    QWidget* parent)
    :
    QWidget(parent),
    ui(new Ui::EditVless)
{
    ui->setupUi(this);

    _flow =
        ui->flow;


    QStringList flows =
    {
        ""
    };

    flows <<
        Configs::vlessFlows;


    ui->flow->addItems(
        flows
    );


    ui->packet_encoding->addItems(
        Configs::vPacketEncoding
    );
}


EditVless::~EditVless()
{
    delete ui;
}


void EditVless::onStart(
    std::shared_ptr<Configs::Profile> _ent)
{
    ent =
        _ent;


    if (!ent)
    {
        return;
    }


    // =====================================================
    // Read VLESS configuration from a detached copy.
    // =====================================================

    const auto outbound =
        ent->OutboundCloneAs<
        Configs::vless
        >();


    if (!outbound)
    {
        return;
    }


    ui->uuid->setText(
        outbound->uuid
    );


    ui->flow->setCurrentText(
        outbound->flow
    );


    ui->packet_encoding->setCurrentText(
        outbound->packet_encoding
    );
}


bool EditVless::onEnd()
{
    if (!ent)
    {
        return false;
    }


    // =====================================================
    // Freeze UI values before modifying Profile.
    // =====================================================

    const QString uuid =
        ui->uuid
        ->text();


    const QString flow =
        ui->flow
        ->currentText();


    const QString packetEncoding =
        ui->packet_encoding
        ->currentText();


    // =====================================================
    // Atomically update VLESS configuration.
    // =====================================================

    return ent
        ->MutateOutbound<
        Configs::vless
        >(
            [
                uuid,
                flow,
                packetEncoding
            ](
                Configs::vless& outbound
                ) -> bool
            {
                outbound.uuid =
                    uuid;


                outbound.flow =
                    flow;


                outbound.packet_encoding =
                    packetEncoding;


                return true;
            }
        );
}