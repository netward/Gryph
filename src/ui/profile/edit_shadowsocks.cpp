#include "include/ui/profile/edit_shadowsocks.h"


EditShadowSocks::EditShadowSocks(
    QWidget* parent)
    :
    QWidget(parent),
    ui(new Ui::EditShadowSocks)
{
    ui->setupUi(this);

    ui->method->addItems(
        Configs::shadowsocksMethods
    );
}


EditShadowSocks::~EditShadowSocks()
{
    delete ui;
}


void EditShadowSocks::onStart(
    std::shared_ptr<Configs::Profile> _ent)
{
    ent = _ent;

    if (!ent)
    {
        return;
    }


    // =====================================================
    // Read Shadowsocks configuration from a detached copy.
    // =====================================================

    const auto outbound =
        ent->OutboundCloneAs<
        Configs::shadowsocks
        >();

    if (!outbound)
    {
        return;
    }


    // =====================================================
    // Normalize legacy plugin representation locally.
    //
    // Do NOT mutate Profile here.
    // =====================================================

    QString plugin =
        outbound->plugin;

    QString pluginOpts =
        outbound->plugin_opts;


    if (plugin.contains(";"))
    {
        pluginOpts =
            SubStrAfter(
                plugin,
                ";"
            );

        plugin =
            SubStrBefore(
                plugin,
                ";"
            );
    }


    // =====================================================
    // Load UI
    // =====================================================

    ui->method->setCurrentText(
        outbound->method
    );

    ui->uot->setChecked(
        outbound->uot
    );

    ui->password->setText(
        outbound->password
    );

    ui->plugin->setCurrentText(
        plugin
    );

    ui->plugin_opts->setText(
        pluginOpts
    );
}


bool EditShadowSocks::onEnd()
{
    if (!ent)
    {
        return false;
    }


    // =====================================================
    // Freeze UI values before modifying Profile.
    // =====================================================

    const QString method =
        ui->method
        ->currentText();

    const QString password =
        ui->password
        ->text();

    const bool uot =
        ui->uot
        ->isChecked();

    const QString plugin =
        ui->plugin
        ->currentText();

    const QString pluginOpts =
        ui->plugin_opts
        ->text();


    // =====================================================
    // Atomically update Shadowsocks configuration.
    // =====================================================

    return ent
        ->MutateOutbound<
        Configs::shadowsocks
        >(
            [
                method,
                password,
                uot,
                plugin,
                pluginOpts
            ](
                Configs::shadowsocks& outbound
                ) -> bool
            {
                outbound.method =
                    method;

                outbound.password =
                    password;

                outbound.uot =
                    uot;

                outbound.plugin =
                    plugin;

                outbound.plugin_opts =
                    pluginOpts;


                return true;
            }
        );
}