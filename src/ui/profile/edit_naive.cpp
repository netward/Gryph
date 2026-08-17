#include "include/ui/profile/edit_naive.h"


EditNaive::EditNaive(
    QWidget* parent)
    :
    QWidget(parent),
    ui(new Ui::EditNaive)
{
    ui->setupUi(this);
}


EditNaive::~EditNaive()
{
    delete ui;
}


void EditNaive::onStart(
    std::shared_ptr<Configs::Profile> _ent)
{
    ent = _ent;

    if (!ent)
    {
        return;
    }


    // =====================================================
    // Read Naive configuration from a detached copy.
    // =====================================================

    const auto outbound =
        ent->OutboundCloneAs<
        Configs::naive
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

    ui->uot->setChecked(
        outbound->uot
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


bool EditNaive::onEnd()
{
    if (!ent)
    {
        return false;
    }


    // =====================================================
    // Freeze UI values before modifying Profile.
    // =====================================================

    const QString username =
        ui->username->text();

    const QString password =
        ui->password->text();

    const bool uot =
        ui->uot->isChecked();

    const bool quic =
        ui->quic->isChecked();

    const QString congestionControl =
        ui->congestion_control
        ->currentText();


    // =====================================================
    // Atomically update Naive configuration.
    // =====================================================

    return ent
        ->MutateOutbound<
        Configs::naive
        >(
            [
                username,
                password,
                uot,
                quic,
                congestionControl
            ](
                Configs::naive& outbound
                ) -> bool
            {
                outbound.username =
                    username;

                outbound.password =
                    password;

                outbound.uot =
                    uot;

                outbound.quic =
                    quic;

                outbound.congestion_control =
                    congestionControl;

                return true;
            }
        );
}