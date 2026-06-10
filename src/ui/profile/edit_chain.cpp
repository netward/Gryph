#include "include/ui/profile/edit_chain.h"


#include "include/database/ProfilesRepo.h"
#include "include/ui/mainwindow_interface.h"
#include "include/ui/profile/ProxyItem.h"

EditChain::EditChain(QWidget *parent) : QWidget(parent), ui(new Ui::EditChain) {
    ui->setupUi(this);
}

EditChain::~EditChain() {
    delete ui;
}

void EditChain::onStart(std::shared_ptr<Configs::Profile> _ent) {
    this->ent = _ent;
    auto outbound = this->ent->Chain();

    for (auto id: outbound->list) {
        AddProfileToListIfExist(id);
    }
}

bool EditChain::onEnd() {
    if (get_edit_text_name().isEmpty()) {
        MessageBoxWarning(software_name, tr("Name cannot be empty."));
        return false;
    }

    auto outbound = this->ent->Chain();

    QList<int> idList;
    int extracoreCount = 0;
    for (int i = 0; i < ui->listWidget->count(); i++) {
        int id = ui->listWidget->item(i)->data(114514).toInt();
        idList << id;
        auto e = Configs::dataManager->profilesRepo->GetProfile(id);
        if (e != nullptr && e->outbound != nullptr && e->outbound->IsExtraCore()) {
            extracoreCount++;
            // A profile using an extra core must be the outermost detour,
            // which corresponds to the top of the chain list (index 0). This
            // is the only position where its local socks server (127.0.0.1)
            // can be reached directly.
            if (i != 0) {
                MessageBoxWarning(software_name, tr("Profiles that use an extra core can only be the final hop in the chain. Move it to the top of the list."));
                return false;
            }
        }
    }
    if (extracoreCount > 1) {
        MessageBoxWarning(software_name, tr("Only one extra-core profile is allowed in a chain."));
        return false;
    }
    outbound->list = idList;

    return true;
}

void EditChain::on_select_profile_clicked() {
    get_edit_dialog()->hide();
    GetMainWindow()->start_select_mode(this, [=,this](int id) {
        get_edit_dialog()->show();
        AddProfileToListIfExist(id);
    });
}

void EditChain::AddProfileToListIfExist(int profileId) {
    auto _ent = Configs::dataManager->profilesRepo->GetProfile(profileId);
    if (_ent != nullptr && _ent->type != "chain") {
        auto wI = new QListWidgetItem();
        wI->setData(114514, profileId);
        auto w = new ProxyItem(this, _ent, wI);
        ui->listWidget->addItem(wI);
        ui->listWidget->setItemWidget(wI, w);
        // change button
        connect(w->get_change_button(), &QPushButton::clicked, w, [=,this] {
            get_edit_dialog()->hide();
            GetMainWindow()->start_select_mode(w, [=,this](int newId) {
                get_edit_dialog()->show();
                ReplaceProfile(w, newId);
            });
        });
    }
}

void EditChain::ReplaceProfile(ProxyItem *w, int profileId) {
    auto _ent = Configs::dataManager->profilesRepo->GetProfile(profileId);
    if (_ent != nullptr && _ent->type != "chain") {
        w->item->setData(114514, profileId);
        w->ent = _ent;
        w->refresh_data();
    }
}
