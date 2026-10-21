#include "LoginWizardPage.h"
#include "minecraft/auth/AccountList.h"
#include "ui/dialogs/ChooseOfflineNameDialog.h"
#include "ui/dialogs/MSALoginDialog.h"
#include "ui_LoginWizardPage.h"

#include "Application.h"

LoginWizardPage::LoginWizardPage(QWidget* parent) : BaseWizardPage(parent), ui(new Ui::LoginWizardPage)
{
    ui->setupUi(this);
    ui->verticalLayout->setSpacing(12);
    ui->logoLabel->setPixmap(APPLICATION->logo().pixmap(QSize(72, 72), devicePixelRatioF()));
    ui->verticalLayout->insertSpacing(ui->verticalLayout->indexOf(ui->pushButton), 10);
    connect(ui->offlineButton, &QPushButton::clicked, this, &LoginWizardPage::addOfflineAccount);
}

LoginWizardPage::~LoginWizardPage()
{
    delete ui;
}

void LoginWizardPage::initializePage() {}

bool LoginWizardPage::validatePage()
{
    return true;
}

void LoginWizardPage::retranslate()
{
    ui->retranslateUi(this);
}

void LoginWizardPage::on_pushButton_clicked()
{
    wizard()->hide();
    auto account = MSALoginDialog::newAccount(nullptr);
    wizard()->show();
    if (account) {
        accountAdded(account);
    }
}

void LoginWizardPage::addOfflineAccount()
{
    ChooseOfflineNameDialog dialog(tr("Choose the name you will play under. Offline accounts work in singleplayer and on offline-mode servers."),
                                   this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (const MinecraftAccountPtr account = MinecraftAccount::createOffline(dialog.getUsername())) {
        account->login()->start();  // completes immediately for offline accounts
        accountAdded(account);
    }
}

void LoginWizardPage::accountAdded(const MinecraftAccountPtr& account)
{
    APPLICATION->accounts()->addAccount(account);
    APPLICATION->accounts()->setDefaultAccount(account);
    if (wizard()->currentId() == wizard()->pageIds().last()) {
        wizard()->accept();
    } else {
        wizard()->next();
    }
}
