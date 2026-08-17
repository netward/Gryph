#include "include/ui/profile/edit_chain.h"

#include "include/database/ProfilesRepo.h"
#include "include/ui/mainwindowapi.h"
#include "include/ui/profile/ProxyItem.h"


EditChain::EditChain(
    QWidget* parent)
    :
    QWidget(parent),
    ui(new Ui::EditChain)
{
    ui->setupUi(this);
}


EditChain::~EditChain()
{
    delete ui;
}


void EditChain::onStart(
    std::shared_ptr<Configs::Profile> _ent)
{
    ent =
        std::move(_ent);


    if (!ent)
    {
        return;
    }


    // =====================================================
    // Read chain configuration from a detached copy.
    // =====================================================

    const auto outbound =
        ent->OutboundCloneAs<
        Configs::chain
        >();


    if (!outbound)
    {
        return;
    }


    // =====================================================
    // Restore chain members into the editor
    // =====================================================

    for (const int id :
    outbound->list)
    {
        AddProfileToListIfExist(
            id
        );
    }
}


bool EditChain::onEnd()
{
    // =====================================================
    // Validation
    // =====================================================

    if (!ent)
    {
        return false;
    }


    if (get_edit_text_name().isEmpty())
    {
        MessageBoxWarning(
            software_name,
            tr("Name cannot be empty.")
        );

        return false;
    }


    // =====================================================
    // Build new chain Profile ID list
    //
    // Do NOT modify Profile until all validation succeeds.
    // =====================================================

    QList<int> idList;

    idList.reserve(
        ui->listWidget->count()
    );


    int extracoreCount = 0;


    for (int i = 0;
        i < ui->listWidget->count();
        ++i)
    {
        auto* const listItem =
            ui->listWidget->item(i);


        if (!listItem)
        {
            continue;
        }


        const int id =
            listItem
            ->data(114514)
            .toInt();


        idList.append(
            id
        );


        // =================================================
        // Validate extra-core position
        // =================================================

        const auto profile =
            Configs::dataManager
            ->profilesRepo
            ->GetProfile(
                id
            );


        if (!profile)
        {
            continue;
        }


        // Read-only detached outbound.
        const auto profileOutbound =
            profile->OutboundClone();


        if (!profileOutbound)
        {
            continue;
        }


        if (!profileOutbound->IsExtraCore())
        {
            continue;
        }


        ++extracoreCount;


        // A profile using an extra core must be the
        // outermost detour.
        //
        // In this editor that corresponds to index 0.
        if (i != 0)
        {
            MessageBoxWarning(
                software_name,
                tr(
                    "Profiles that use an extra core "
                    "can only be the final hop in the "
                    "chain. Move it to the top of the "
                    "list."
                )
            );

            return false;
        }
    }


    if (extracoreCount > 1)
    {
        MessageBoxWarning(
            software_name,
            tr(
                "Only one extra-core profile is "
                "allowed in a chain."
            )
        );

        return false;
    }


    // =====================================================
    // Atomically publish new chain configuration
    //
    // OutboundCloneAs() must NOT be used for writing:
    // changes to a clone would not reach Profile.
    // =====================================================

    const bool updated =
        ent
        ->MutateOutbound<
        Configs::chain
        >(
            [idList](
                Configs::chain& outbound
                ) -> bool
            {
                outbound.list =
                    idList;

                return true;
            }
        );


    if (!updated)
    {
        MessageBoxWarning(
            software_name,
            tr(
                "Failed to update chain "
                "configuration."
            )
        );

        return false;
    }


    return true;
}


void EditChain::on_select_profile_clicked()
{
    get_edit_dialog()->hide();


    MainWindowApi::StartSelectMode(
        this,

        [this](int id)
        {
            get_edit_dialog()->show();


            AddProfileToListIfExist(
                id
            );
        }
    );
}


void EditChain::AddProfileToListIfExist(
    int profileId)
{
    const auto profile =
        Configs::dataManager
        ->profilesRepo
        ->GetProfile(
            profileId
        );


    if (!profile)
    {
        return;
    }


    // Do not allow chains inside chains.
    if (profile->Type() == "chain")
    {
        return;
    }


    auto* const listItem =
        new QListWidgetItem();


    listItem->setData(
        114514,
        profileId
    );


    auto* const widget =
        new ProxyItem(
            this,
            profile,
            listItem
        );


    ui->listWidget->addItem(
        listItem
    );


    ui->listWidget->setItemWidget(
        listItem,
        widget
    );


    // =====================================================
    // Replace Profile button
    // =====================================================

    connect(
        widget->get_change_button(),
        &QPushButton::clicked,
        widget,

        [this, widget]()
        {
            get_edit_dialog()->hide();


            MainWindowApi::StartSelectMode(
                widget,

                [this, widget](
                    int newId)
                {
                    get_edit_dialog()->show();


                    ReplaceProfile(
                        widget,
                        newId
                    );
                }
            );
        }
    );
}


void EditChain::ReplaceProfile(
    ProxyItem* widget,
    int profileId)
{
    if (!widget ||
        !widget->item)
    {
        return;
    }


    const auto profile =
        Configs::dataManager
        ->profilesRepo
        ->GetProfile(
            profileId
        );


    if (!profile)
    {
        return;
    }


    // Do not allow nested chains.
    if (profile->Type() == "chain")
    {
        return;
    }


    widget->item->setData(
        114514,
        profileId
    );


    widget->ent =
        profile;


    widget->refresh_data();
}