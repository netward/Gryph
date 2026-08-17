#include "include/ui/profile/edit_shadowtls.h"


EditShadowTLS::EditShadowTLS(
    QWidget* parent)
    :
    QWidget(parent),
    ui(new Ui::EditShadowTLS)
{
    ui->setupUi(this);
}


EditShadowTLS::~EditShadowTLS()
{
    delete ui;
}


void EditShadowTLS::onStart(
    std::shared_ptr<Configs::Profile> _ent)
{
    ent = _ent;

    if (!ent)
    {
        return;
    }


    // Read ShadowTLS configuration from a detached copy.
    const auto outbound =
        ent->OutboundCloneAs<
        Configs::shadowtls
        >();

    if (!outbound)
    {
        return;
    }


    ui->version->setCurrentText(
        Int2String(
            outbound->version
        )
    );

    ui->password->setText(
        outbound->password
    );
}


bool EditShadowTLS::onEnd()
{
    if (!ent)
    {
        return false;
    }


    const int version =
        ui->version
        ->currentText()
        .toInt();

    const QString password =
        ui->password
        ->text();


    return ent
        ->MutateOutbound<
        Configs::shadowtls
        >(
            [
                version,
                password
            ](
                Configs::shadowtls& outbound
                ) -> bool
            {
                outbound.version =
                    version;

                outbound.password =
                    password;

                return true;
            }
        );
}