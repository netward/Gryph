#include "include/ui/profile/edit_vmess.h"

#include <QUuid>


EditVMess::EditVMess(
    QWidget* parent)
    :
    QWidget(parent),
    ui(new Ui::EditVMess)
{
    ui->setupUi(this);


    connect(
        ui->uuidgen,
        &QPushButton::clicked,
        this,
        [this]()
        {
            ui->uuid->setText(
                QUuid::createUuid()
                .toString(
                    QUuid::WithoutBraces
                )
            );
        }
    );


    ui->packet_encoding->addItems(
        Configs::vPacketEncoding
    );
}


EditVMess::~EditVMess()
{
    delete ui;
}


void EditVMess::onStart(
    std::shared_ptr<Configs::Profile> _ent)
{
    ent = _ent;


    if (!ent)
    {
        return;
    }


    // =====================================================
    // Read VMess configuration from a detached copy.
    // =====================================================

    const auto outbound =
        ent->OutboundCloneAs<
        Configs::vmess
        >();


    if (!outbound)
    {
        return;
    }


    ui->uuid->setText(
        outbound->uuid
    );


    ui->aid->setText(
        Int2String(
            outbound->alter_id
        )
    );


    ui->packet_encoding->setCurrentText(
        outbound->packet_encoding
    );


    ui->security->setCurrentText(
        outbound->security
    );
}


bool EditVMess::onEnd()
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


    const int alterId =
        ui->aid
        ->text()
        .toInt();


    const QString packetEncoding =
        ui->packet_encoding
        ->currentText();


    const QString security =
        ui->security
        ->currentText();


    // =====================================================
    // Atomically update VMess configuration.
    // =====================================================

    return ent
        ->MutateOutbound<
        Configs::vmess
        >(
            [
                uuid,
                alterId,
                packetEncoding,
                security
            ](
                Configs::vmess& outbound
                ) -> bool
            {
                outbound.uuid =
                    uuid;


                outbound.alter_id =
                    alterId;


                outbound.packet_encoding =
                    packetEncoding;


                outbound.security =
                    security;


                return true;
            }
        );
}