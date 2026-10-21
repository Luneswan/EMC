// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "NewServerDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "Application.h"
#include "OptionCard.h"
#include "minecraft/auth/AccountList.h"
#include "server/ServerContent.h"
#include "server/ServerCreationTask.h"
#include "server/ServerInstance.h"
#include "server/ServerList.h"
#include "server/ServerOptimizer.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ProgressDialog.h"

namespace {
using ServerSoftware::Category;

QLabel* note(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "note");
    label->setWordWrap(true);
    return label;
}

QString memoryText(int mib)
{
    return mib % 1024 == 0 ? QObject::tr("%1 GB").arg(mib / 1024) : QObject::tr("%1 GB").arg(mib / 1024.0, 0, 'f', 1);
}

bool defaultAccountIsOffline()
{
    auto account = APPLICATION->accounts()->defaultAccount();
    return !account || account->accountType() == AccountType::Offline;
}
}  // namespace

NewServerDialog::NewServerDialog(ServerList* list, QWidget* parent) : QDialog(parent), m_list(list)
{
    setWindowTitle(tr("New Server"));
    setModal(true);
    resize(660, 620);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 22, 28, 20);
    root->setSpacing(14);

    m_stepLabel = new QLabel(this);
    m_stepLabel->setProperty("role", "note");
    root->addWidget(m_stepLabel);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildEditionStep());
    m_stack->addWidget(buildTypeStep());
    m_stack->addWidget(buildVersionStep());
    m_stack->addWidget(buildWorldStep());
    m_stack->addWidget(buildExtrasStep());
    m_stack->addWidget(buildReviewStep());
    root->addWidget(m_stack, 1);

    auto* buttons = new QHBoxLayout;
    auto* cancel = new QPushButton(tr("Cancel"), this);
    m_back = new QPushButton(tr("Back"), this);
    m_next = new QPushButton(tr("Continue"), this);
    m_next->setProperty("accent", true);
    m_next->setDefault(true);
    m_next->setMinimumWidth(120);
    buttons->addWidget(cancel);
    buttons->addStretch(1);
    buttons->addWidget(m_back);
    buttons->addWidget(m_next);
    root->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_back, &QPushButton::clicked, this, &NewServerDialog::back);
    connect(m_next, &QPushButton::clicked, this, &NewServerDialog::next);

    goTo(EditionStep);
}

QWidget* NewServerDialog::page(const QString& title, const QString& subtitle, QVBoxLayout** body)
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(scroll);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 6, 0);
    layout->setSpacing(10);
    auto* heading = new QLabel(title, content);
    heading->setProperty("role", "title");
    heading->setWordWrap(true);
    layout->addWidget(heading);
    if (!subtitle.isEmpty()) {
        auto* sub = new QLabel(subtitle, content);
        sub->setProperty("role", "body");
        sub->setWordWrap(true);
        layout->addWidget(sub);
    }
    layout->addSpacing(8);
    *body = layout;
    scroll->setWidget(content);
    return scroll;
}

QWidget* NewServerDialog::buildEditionStep()
{
    QVBoxLayout* body;
    auto* w = page(tr("Who will play on it?"), tr("Java and Bedrock are different games. Pick the one your friends play."), &body);
    m_editionGroup = new QButtonGroup(this);
    struct Choice {
        Edition edition;
        QString title;
        QString description;
        QString badge;
    };
    const QList<Choice> choices{
        { Edition::Java, tr("Java Edition"), tr("Minecraft on Windows, macOS and Linux. Supports plugins and mods."), tr("Most popular") },
        { Edition::Bedrock, tr("Bedrock Edition"), tr("Minecraft on phones, tablets, consoles and the Windows Store version."), {} },
        { Edition::Both, tr("Java and Bedrock together"),
          tr("A Java server that Bedrock players can join too, through Geyser. Plugins work; some mods do not."), {} },
    };
    for (const auto& choice : choices) {
        auto* card = new OptionCard(choice.title, choice.description, w);
        card->setBadge(choice.badge);
        m_editionGroup->addButton(card, int(choice.edition));
        body->addWidget(card);
    }
    m_editionGroup->button(int(Edition::Java))->setChecked(true);
    body->addStretch(1);
    connect(m_editionGroup, &QButtonGroup::idClicked, this, [this] {
        refreshTypeOptions();
        updateButtons();
    });
    return w;
}

QWidget* NewServerDialog::buildTypeStep()
{
    QVBoxLayout* body;
    auto* w = page(tr("What kind of server?"), tr("You can add plugins or mods later from the server's page."), &body);
    m_categoryGroup = new QButtonGroup(this);
    for (auto category : { Category::Vanilla, Category::Plugins, Category::Mods, Category::Hybrid }) {
        auto* card = new OptionCard(ServerSoftware::categoryName(category), ServerSoftware::categoryDescription(category), w);
        if (category == Category::Hybrid) {
            card->setBadge(tr("Advanced"));
        }
        m_categoryGroup->addButton(card, int(category));
        m_categoryCards << card;
        body->addWidget(card);
    }

    m_softwareBox = new QWidget(w);
    m_softwareLayout = new QVBoxLayout(m_softwareBox);
    m_softwareLayout->setContentsMargins(0, 8, 0, 0);
    m_softwareLayout->setSpacing(8);
    body->addWidget(m_softwareBox);
    m_softwareGroup = new QButtonGroup(this);
    body->addStretch(1);

    connect(m_categoryGroup, &QButtonGroup::idClicked, this, [this] {
        refreshSoftwareOptions();
        updateButtons();
    });
    return w;
}

QWidget* NewServerDialog::buildVersionStep()
{
    QVBoxLayout* body;
    auto* w = page(tr("Version and name"), tr("Players need the same Minecraft version to join."), &body);
    auto* form = new QFormLayout;
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(12);
    form->setLabelAlignment(Qt::AlignLeft);
    m_version = new QComboBox(w);
    m_version->setMinimumWidth(240);
    m_name = new QLineEdit(w);
    m_name->setPlaceholderText(tr("My Server"));
    m_name->setMaxLength(48);
    form->addRow(tr("Minecraft version"), m_version);
    form->addRow(tr("Server name"), m_name);
    body->addLayout(form);
    m_versionNote = note({}, w);
    body->addWidget(m_versionNote);
    body->addStretch(1);
    connect(m_name, &QLineEdit::textChanged, this, &NewServerDialog::updateButtons);
    return w;
}

QWidget* NewServerDialog::buildWorldStep()
{
    QVBoxLayout* body;
    auto* w = page(tr("World"), tr("All of this can be changed later."), &body);
    auto* form = new QFormLayout;
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(12);
    m_gamemode = new QComboBox(w);
    m_gamemode->addItem(tr("Survival"), "survival");
    m_gamemode->addItem(tr("Creative"), "creative");
    m_gamemode->addItem(tr("Adventure"), "adventure");
    m_difficulty = new QComboBox(w);
    m_difficulty->addItem(tr("Peaceful"), "peaceful");
    m_difficulty->addItem(tr("Easy"), "easy");
    m_difficulty->addItem(tr("Normal"), "normal");
    m_difficulty->addItem(tr("Hard"), "hard");
    m_difficulty->setCurrentIndex(2);
    m_seed = new QLineEdit(w);
    m_seed->setPlaceholderText(tr("Random"));
    m_motd = new QLineEdit(w);
    m_motd->setPlaceholderText(tr("Shown in the server list"));
    m_motd->setMaxLength(59);
    form->addRow(tr("Game mode"), m_gamemode);
    form->addRow(tr("Difficulty"), m_difficulty);
    form->addRow(tr("World seed"), m_seed);
    form->addRow(tr("Description"), m_motd);
    body->addLayout(form);
    body->addSpacing(6);

    m_hardcore = new QCheckBox(tr("Hardcore (one life)"), w);
    m_pvp = new QCheckBox(tr("Players can hurt each other"), w);
    m_pvp->setChecked(true);
    m_whitelist = new QCheckBox(tr("Only players I allow can join (whitelist)"), w);
    m_offlinePlayers = new QCheckBox(tr("Let players without a Microsoft account join"), w);
    m_offlinePlayers->setChecked(defaultAccountIsOffline());
    body->addWidget(m_hardcore);
    body->addWidget(m_pvp);
    body->addWidget(m_whitelist);
    body->addWidget(m_offlinePlayers);
    body->addWidget(note(tr("Needed for offline accounts. The server then trusts whatever name a player gives, so turn on the "
                            "whitelist before sharing it publicly."),
                         w));
    body->addStretch(1);
    return w;
}

QWidget* NewServerDialog::buildExtrasStep()
{
    QVBoxLayout* body;
    auto* w = page(tr("Performance"), {}, &body);
    m_autoOptimize = new QCheckBox(tr("Tune for this computer automatically"), w);
    m_autoOptimize->setChecked(true);
    body->addWidget(m_autoOptimize);
    m_optimizeSummary = note({}, w);
    body->addWidget(m_optimizeSummary);

    m_manualBox = new QWidget(w);
    auto* manual = new QFormLayout(m_manualBox);
    manual->setContentsMargins(0, 4, 0, 0);
    manual->setHorizontalSpacing(16);
    auto* memoryRow = new QHBoxLayout;
    m_memory = new QSlider(Qt::Horizontal, m_manualBox);
    const auto machine = ServerOptimizer::Machine::current();
    const int ceiling = std::max(1024, int(std::min<quint64>(machine.ramMiB - 1024, 32768)) / 512 * 512);
    m_memory->setRange(2, ceiling / 512);
    m_memory->setPageStep(2);
    m_memoryLabel = new QLabel(m_manualBox);
    m_memoryLabel->setMinimumWidth(56);
    memoryRow->addWidget(m_memory, 1);
    memoryRow->addWidget(m_memoryLabel);
    manual->addRow(tr("Memory"), memoryRow);
    m_maxPlayers = new QSpinBox(m_manualBox);
    m_maxPlayers->setRange(1, 500);
    manual->addRow(tr("Player limit"), m_maxPlayers);
    body->addWidget(m_manualBox);

    body->addSpacing(14);
    m_starterBox = new QWidget(w);
    m_starterLayout = new QVBoxLayout(m_starterBox);
    m_starterLayout->setContentsMargins(0, 0, 0, 0);
    m_starterLayout->setSpacing(6);
    body->addWidget(m_starterBox);
    body->addStretch(1);

    connect(m_autoOptimize, &QCheckBox::toggled, this, [this](bool on) { m_manualBox->setVisible(!on); });
    connect(m_memory, &QSlider::valueChanged, this, [this](int v) { m_memoryLabel->setText(memoryText(v * 512)); });
    return w;
}

QWidget* NewServerDialog::buildReviewStep()
{
    QVBoxLayout* body;
    auto* w = page(tr("Ready to create"), {}, &body);
    m_review = new QLabel(w);
    m_review->setProperty("role", "body");
    m_review->setWordWrap(true);
    m_review->setTextFormat(Qt::RichText);
    body->addWidget(m_review);
    body->addSpacing(10);
    m_eula = new QCheckBox(w);
    m_eula->setText(tr("I agree to the Minecraft End User License Agreement"));
    body->addWidget(m_eula);
    auto* link = new QLabel(tr("<a href=\"https://www.minecraft.net/eula\">Read the EULA</a> · Mojang requires every server owner "
                               "to agree to it."),
                            w);
    link->setProperty("role", "note");
    link->setOpenExternalLinks(true);
    link->setWordWrap(true);
    body->addWidget(link);
    body->addStretch(1);
    connect(m_eula, &QCheckBox::toggled, this, &NewServerDialog::updateButtons);
    return w;
}

NewServerDialog::Edition NewServerDialog::edition() const
{
    return Edition(m_editionGroup->checkedId());
}

QString NewServerDialog::software() const
{
    if (edition() == Edition::Bedrock) {
        return "bedrock";
    }
    auto* button = m_softwareGroup->checkedButton();
    return button ? button->property("software").toString() : QString();
}

bool NewServerDialog::stepSkipped(int step) const
{
    return step == TypeStep && edition() == Edition::Bedrock;
}

void NewServerDialog::refreshTypeOptions()
{
    const bool both = edition() == Edition::Both;
    for (auto* card : m_categoryCards) {
        const auto category = Category(m_categoryGroup->id(card));
        bool available = false;
        for (const auto& info : ServerSoftware::all()) {
            if (info.category == category && (!both || info.crossplay)) {
                available = true;
            }
        }
        card->setVisible(available);
        if (!available && card->isChecked()) {
            m_categoryGroup->setExclusive(false);
            card->setChecked(false);
            m_categoryGroup->setExclusive(true);
        }
    }
    if (!m_categoryGroup->checkedButton()) {
        // Plugins is the sensible default: Paper runs vanilla worlds faster and takes plugins later.
        m_categoryGroup->button(int(Category::Plugins))->setChecked(true);
    }
    refreshSoftwareOptions();
}

void NewServerDialog::refreshSoftwareOptions()
{
    const QString previous = software();
    for (auto* button : m_softwareGroup->buttons()) {
        m_softwareGroup->removeButton(button);
        button->deleteLater();
    }
    while (auto* item = m_softwareLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    const auto category = Category(m_categoryGroup->checkedId());
    const bool both = edition() == Edition::Both;
    QList<const ServerSoftware::Info*> options;
    for (const auto& info : ServerSoftware::all()) {
        if (info.category == category && (!both || info.crossplay)) {
            options << &info;
        }
    }

    if (options.size() > 1) {
        auto* label = new QLabel(tr("Server software"), m_softwareBox);
        label->setProperty("role", "note");
        m_softwareLayout->addWidget(label);
    }
    bool first = true;
    for (const auto* info : options) {
        auto* card = new OptionCard(info->name, info->description, m_softwareBox);
        card->setProperty("software", info->id);
        if (info->advanced && category != Category::Hybrid) {
            card->setBadge(tr("Advanced"));
        } else if (first && options.size() > 1) {
            card->setBadge(tr("Recommended"));
        }
        m_softwareGroup->addButton(card);
        m_softwareLayout->addWidget(card);
        if (info->id == previous || (first && !info->advanced)) {
            card->setChecked(true);
        }
        first = false;
    }
    // A category of only advanced options (Mods + Plugins) still starts with one chosen.
    if (!m_softwareGroup->checkedButton() && !m_softwareGroup->buttons().isEmpty()) {
        m_softwareGroup->buttons().first()->setChecked(true);
    }
    // With a single option, the category card already says everything.
    m_softwareBox->setVisible(options.size() > 1);
    connect(m_softwareGroup, &QButtonGroup::buttonClicked, this, &NewServerDialog::updateButtons, Qt::UniqueConnection);
}

void NewServerDialog::loadVersions()
{
    const QString id = software();
    if (id.isEmpty()) {
        return;
    }
    const QString previous = m_version->currentData().toString();
    auto fill = [this, previous](const QList<ServerVersionEntry>& versions) {
        m_version->clear();
        int stableCount = 0;
        for (const auto& entry : versions) {
            if (entry.stable) {
                m_version->addItem(entry.label, entry.mcVersion);
                ++stableCount;
            }
        }
        if (stableCount == 0) {
            for (const auto& entry : versions) {
                m_version->addItem(entry.label, entry.mcVersion);
            }
        }
        const int index = m_version->findData(previous);
        m_version->setCurrentIndex(index >= 0 ? index : 0);
        m_version->setEnabled(m_version->count() > 0);
        m_versionNote->setText(m_version->count() > 0 ? tr("The newest version is selected. Pick an older one only if your "
                                                           "friends or your mods need it.")
                                                        : tr("No versions found."));
        updateButtons();
    };

    if (m_versionCache.contains(id)) {
        fill(m_versionCache.value(id));
        return;
    }

    m_loadingFor = id;
    m_version->clear();
    m_version->setEnabled(false);
    m_versionNote->setText(tr("Looking up versions…"));
    updateButtons();

    auto* task = new ServerVersionListTask(id, this);
    connect(task, &Task::succeeded, this, [this, task, id, fill] {
        m_versionCache.insert(id, task->versions());
        if (m_loadingFor == id) {
            m_loadingFor.clear();
            fill(task->versions());
        }
        task->deleteLater();
    });
    connect(task, &Task::failed, this, [this, task, id](QString reason) {
        if (m_loadingFor == id) {
            m_loadingFor.clear();
            m_versionNote->setText(tr("Could not load versions: %1. Check your internet connection and go back and forward to "
                                      "try again.")
                                       .arg(reason));
            updateButtons();
        }
        task->deleteLater();
    });
    task->start();
}

void NewServerDialog::refreshExtras()
{
    const QString id = software();
    const auto* info = ServerSoftware::find(id);
    const bool bedrock = id == "bedrock";
    const bool mods = info && info->mods;
    const auto plan = ServerOptimizer::plan(ServerOptimizer::Machine::current(), mods, bedrock);
    m_optimizeSummary->setText(plan.summary);
    if (!m_memory->isSliderDown()) {
        m_memory->setValue(plan.maxMemMiB / 512);
        m_memoryLabel->setText(memoryText(m_memory->value() * 512));
    }
    m_maxPlayers->setValue(plan.maxPlayers);
    m_memory->setEnabled(!bedrock);
    m_manualBox->setVisible(!m_autoOptimize->isChecked());

    // Starter content for this server type; recommended items are ticked.
    m_starterChecks.clear();
    while (auto* item = m_starterLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto starters = ServerContent::starters(id);
    if (!starters.isEmpty()) {
        QString title = tr("Recommended plugins");
        if (info && info->mods) {
            title = info->plugins ? tr("Recommended plugins and mods") : tr("Recommended mods");
        }
        auto* heading = new QLabel(title, m_starterBox);
        heading->setProperty("role", "cardTitle");
        m_starterLayout->addWidget(heading);
        m_starterLayout->addWidget(note(tr("Installed with the server. Thousands more are on the server's page."), m_starterBox));
        m_starterLayout->addSpacing(4);
        // Name in one column, what it does in the next: easy to scan, nothing joined with dashes.
        auto* grid = new QWidget(m_starterBox);
        auto* gridLayout = new QGridLayout(grid);
        gridLayout->setContentsMargins(0, 0, 0, 0);
        gridLayout->setHorizontalSpacing(18);
        gridLayout->setVerticalSpacing(8);
        gridLayout->setColumnStretch(1, 1);
        int row = 0;
        for (const auto& starter : starters) {
            auto* check = new QCheckBox(starter.name, grid);
            check->setProperty("project", starter.project);
            check->setChecked(starter.recommended);
            check->setMinimumWidth(150);
            auto* what = new QLabel(starter.description, grid);
            what->setProperty("role", "body");
            what->setWordWrap(true);
            gridLayout->addWidget(check, row, 0, Qt::AlignTop);
            gridLayout->addWidget(what, row, 1, Qt::AlignVCenter);
            m_starterChecks << check;
            ++row;
        }
        m_starterLayout->addWidget(grid);
    }
    m_starterBox->setVisible(!starters.isEmpty());
}

void NewServerDialog::refreshReview()
{
    const auto* info = ServerSoftware::find(software());
    QStringList lines;
    const QString name = m_name->text().trimmed().isEmpty() ? m_name->placeholderText() : m_name->text().trimmed();
    lines << tr("<b>%1</b>").arg(name.toHtmlEscaped());
    lines << tr("%1 %2").arg(info ? info->name : software(), m_version->currentText().toHtmlEscaped());
    if (edition() == Edition::Both) {
        lines << tr("Bedrock players can join through Geyser");
    }
    lines << tr("%1, %2").arg(m_gamemode->currentText(), m_difficulty->currentText().toLower());
    if (software() != "bedrock") {
        const int memory = m_autoOptimize->isChecked()
                               ? ServerOptimizer::plan(ServerOptimizer::Machine::current(), info && info->mods, false).maxMemMiB
                               : m_memory->value() * 512;
        lines << tr("%1 of memory").arg(memoryText(memory));
    }
    int picked = 0;
    for (auto* check : m_starterChecks) {
        picked += check->isChecked() ? 1 : 0;
    }
    if (picked > 0) {
        lines << (picked == 1 ? tr("1 add-on") : tr("%1 add-ons").arg(picked));
    }
    lines << tr("EMC downloads the server and the Java it needs. This takes a minute or two.");
    m_review->setText(lines.join("<br>"));

    const bool bedrock = software() == "bedrock";
    m_eula->setText(bedrock ? tr("I agree to the Minecraft End User License Agreement and Privacy Policy")
                            : tr("I agree to the Minecraft End User License Agreement"));
}

void NewServerDialog::goTo(int step)
{
    m_step = step;
    const bool bedrock = edition() == Edition::Bedrock;
    m_hardcore->setVisible(!bedrock);
    m_offlinePlayers->setVisible(!bedrock);
    if (step == TypeStep) {
        refreshTypeOptions();
    } else if (step == VersionStep) {
        loadVersions();
        m_name->setFocus();
    } else if (step == ExtrasStep) {
        refreshExtras();
    } else if (step == ReviewStep) {
        refreshReview();
    }
    m_stack->setCurrentIndex(step);

    int total = 0;
    int current = 0;
    for (int i = 0; i < StepCount; ++i) {
        if (!stepSkipped(i)) {
            ++total;
            if (i <= step) {
                ++current;
            }
        }
    }
    m_stepLabel->setText(tr("Step %1 of %2").arg(current).arg(total));
    updateButtons();
}

void NewServerDialog::next()
{
    if (m_step == ReviewStep) {
        create();
        return;
    }
    int step = m_step + 1;
    while (step < ReviewStep && stepSkipped(step)) {
        ++step;
    }
    goTo(step);
}

void NewServerDialog::back()
{
    int step = m_step - 1;
    while (step > EditionStep && stepSkipped(step)) {
        --step;
    }
    goTo(std::max(0, step));
}

void NewServerDialog::updateButtons()
{
    m_back->setVisible(m_step != EditionStep);
    m_next->setText(m_step == ReviewStep ? tr("Create Server") : tr("Continue"));
    bool ok = true;
    switch (m_step) {
        case EditionStep:
            ok = m_editionGroup->checkedButton() != nullptr;
            break;
        case TypeStep:
            ok = !software().isEmpty();
            break;
        case VersionStep:
            ok = m_loadingFor.isEmpty() && m_version->count() > 0;
            break;
        case ReviewStep:
            ok = m_eula->isChecked();
            break;
        default:
            break;
    }
    m_next->setEnabled(ok);
}

void NewServerDialog::create()
{
    ServerCreateOptions options;
    options.name = m_name->text().trimmed().isEmpty() ? m_name->placeholderText() : m_name->text().trimmed();
    options.software = software();
    options.mcVersion = m_version->currentData().toString();
    options.crossplay = edition() == Edition::Both;
    options.acceptEula = m_eula->isChecked();
    options.motd = m_motd->text().trimmed();
    options.gamemode = m_gamemode->currentData().toString();
    options.difficulty = m_difficulty->currentData().toString();
    options.seed = m_seed->text().trimmed();
    // isHidden, not isVisible: this runs on the review step, where the World page is not on screen.
    options.hardcore = !m_hardcore->isHidden() && m_hardcore->isChecked();
    options.pvp = m_pvp->isChecked();
    options.onlineMode = options.software == "bedrock" || !m_offlinePlayers->isChecked();
    options.whitelist = m_whitelist->isChecked();
    options.autoOptimize = m_autoOptimize->isChecked();
    if (!options.autoOptimize) {
        options.maxMemMiB = m_memory->value() * 512;
        options.maxPlayers = m_maxPlayers->value();
    }
    for (auto* check : m_starterChecks) {
        if (check->isChecked()) {
            options.content << check->property("project").toString();
        }
    }

    auto task = std::make_unique<ServerCreationTask>(m_list, options);
    ProgressDialog progress(this);
    progress.execWithTask(task.get());
    if (task->wasSuccessful() && task->server()) {
        m_created = task->server();
        accept();
        return;
    }
    if (task->getState() != Task::State::AbortedByUser && !task->failReason().isEmpty()) {
        CustomMessageBox::selectable(this, tr("Could not create the server"), task->failReason(), QMessageBox::Warning)->exec();
    }
}
