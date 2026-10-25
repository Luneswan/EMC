#include "LoginWizardPage.h"
#include "minecraft/auth/AccountList.h"
#include "ui/dialogs/ChooseOfflineNameDialog.h"
#include "ui/dialogs/MSALoginDialog.h"
#include "ui/dialogs/SkinServerLoginDialog.h"
#include "ui_LoginWizardPage.h"

#include "Application.h"

LoginWizardPage::LoginWizardPage(QWidget* parent) : BaseWizardPage(parent), ui(new Ui::LoginWizardPage)
{
    ui->setupUi(this);
    ui->verticalLayout->setSpacing(12);
    ui->logoLabel->setPixmap(APPLICATION->logo().pixmap(QSize(72, 72), devicePixelRatioF()));
    ui->verticalLayout->insertSpacing(ui->verticalLayout->indexOf(ui->pushButton), 10);
    connect(ui->offlineButton, &QPushButton::clicked, this, &LoginWizardPage::addOfflineAccount);

    // Ely.by: a free account with a custom skin, between the Microsoft and offline choices.
    m_elyButton = new QPushButton(tr("Sign in with Ely.by"), this);
    m_elyButton->setToolTip(tr("Free account with your own skin and cape"));
    m_elyButton->setAutoDefault(false);
    m_elyButton->setMinimumSize(ui->offlineButton->minimumSize());
    m_elyButton->setMaximumSize(ui->offlineButton->maximumSize());
    m_elyButton->setSizePolicy(ui->offlineButton->sizePolicy());
    ui->verticalLayout->insertWidget(ui->verticalLayout->indexOf(ui->offlineButton), m_elyButton, 0,
                                     ui->verticalLayout->itemAt(ui->verticalLayout->indexOf(ui->offlineButton))->alignment());
    connect(m_elyButton, &QPushButton::clicked, this, &LoginWizardPage::addElyAccount);
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
    m_elyButton->setText(tr("Sign in with Ely.by"));
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

void LoginWizardPage::addElyAccount()
{
    if (auto account = SkinServerLoginDialog::newAccount(this)) {
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
