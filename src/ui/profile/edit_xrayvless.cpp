#include "include/ui/profile/edit_xrayvless.h"
#include "ui_edit_xrayvless.h"

EditXrayVless::EditXrayVless(
    QWidget* parent)
    :
    QWidget(parent),
    ui(new Ui::EditXrayVless)
{
    ui->setupUi(this);


    QStringList flows =
    {
        ""
    };

    flows <<
        Configs::xrayFlows;


    ui->xray_flow->addItems(
        flows
    );
}


EditXrayVless::~EditXrayVless()
{
    delete ui;
}


void EditXrayVless::onStart(
    std::shared_ptr<Configs::Profile> _ent)
{
    ent = _ent;


    if (!ent)
    {
        return;
    }


    // =====================================================
    // Read Xray VLESS configuration from detached copy.
    // =====================================================

    const auto outbound =
        ent->OutboundCloneAs<
        Configs::xrayVless
        >();


    if (!outbound)
    {
        return;
    }


    ui->xray_uuid->setText(
        outbound->uuid
    );


    ui->xray_enc->setText(
        outbound->encryption
    );


    ui->xray_flow->setCurrentText(
        outbound->flow
    );
}


bool EditXrayVless::onEnd()
{
    if (!ent)
    {
        return false;
    }


    // =====================================================
    // Freeze UI values before Profile mutation.
    // =====================================================

    const QString uuid =
        ui->xray_uuid
        ->text();


    const QString encryption =
        ui->xray_enc
        ->text();


    const QString flow =
        ui->xray_flow
        ->currentText();


    // =====================================================
    // Atomically update Xray VLESS configuration.
    // =====================================================

    return ent
        ->MutateOutbound<
        Configs::xrayVless
        >(
            [
                uuid,
                encryption,
                flow
            ](
                Configs::xrayVless& outbound
                ) -> bool
            {
                outbound.uuid =
                    uuid;


                outbound.encryption =
                    encryption;


                outbound.flow =
                    flow;


                return true;
            }
        );
}