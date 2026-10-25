#pragma once

#include <QPushButton>
#include <QWidget>
#include "BaseWizardPage.h"
#include "minecraft/auth/MinecraftAccount.h"

namespace Ui {
class LoginWizardPage;
}

class LoginWizardPage : public BaseWizardPage {
    Q_OBJECT

   public:
    explicit LoginWizardPage(QWidget* parent = nullptr);
    ~LoginWizardPage();

    void initializePage() override;
    bool validatePage() override;
    void retranslate() override;
   private slots:
    void on_pushButton_clicked();

   private:
    void addOfflineAccount();
    void addElyAccount();
    void accountAdded(const MinecraftAccountPtr& account);

    Ui::LoginWizardPage* ui;
    QPushButton* m_elyButton = nullptr;
};
