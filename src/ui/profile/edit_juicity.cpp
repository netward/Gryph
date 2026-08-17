#include "include/ui/profile/edit_juicity.h"


EditJuicity::EditJuicity(
    QWidget* parent)
    :
    QWidget(parent),
    ui(new Ui::EditJuicity)
{
    ui->setupUi(this);
}


EditJuicity::~EditJuicity()
{
    delete ui;
}


void EditJuicity::onStart(
    std::shared_ptr<Configs::Profile> _ent)
{
    ent = _ent;

    if (!ent)
    {
        return;
    }


    // =====================================================
    // Read Juicity configuration from a detached copy.
    // =====================================================

    const auto outbound =
        ent->OutboundCloneAs<
        Configs::juicity
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
}


bool EditJuicity::onEnd()
{
    if (!ent)
    {
        return false;
    }


    // =====================================================
    // Freeze UI values before modifying Profile.
    // =====================================================

    const QString uuid =
        ui->uuid->text();

    const QString password =
        ui->password->text();


    // =====================================================
    // Atomically update Juicity configuration.
    // =====================================================

    return ent
        ->MutateOutbound<
        Configs::juicity
        >(
            [
                uuid,
                password
            ](
                Configs::juicity& outbound
                ) -> bool
            {
                outbound.uuid =
                    uuid;

                outbound.password =
                    password;

                return true;
            }
        );
}