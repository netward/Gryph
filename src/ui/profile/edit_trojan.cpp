#include "include/ui/profile/edit_trojan.h"


EditTrojan::EditTrojan(
    QWidget* parent)
    :
    QWidget(parent),
    ui(new Ui::EditTrojan)
{
    ui->setupUi(this);
}


EditTrojan::~EditTrojan()
{
    delete ui;
}


void EditTrojan::onStart(
    std::shared_ptr<Configs::Profile> _ent)
{
    ent = _ent;

    if (!ent)
    {
        return;
    }


    // Read Trojan configuration from a detached copy.
    const auto outbound =
        ent->OutboundCloneAs<
        Configs::Trojan
        >();

    if (!outbound)
    {
        return;
    }


    ui->password->setText(
        outbound->password
    );
}


bool EditTrojan::onEnd()
{
    if (!ent)
    {
        return false;
    }


    // Freeze UI value before modifying Profile.
    const QString password =
        ui->password->text();


    // Atomically update Trojan configuration.
    return ent
        ->MutateOutbound<
        Configs::Trojan
        >(
            [password](
                Configs::Trojan& outbound
                ) -> bool
            {
                outbound.password =
                    password;

                return true;
            }
        );
}