// SPDX-License-Identifier: GPL-3.0-only
#include "OfflineSkinDialog.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

#include "Application.h"

namespace {
// One side of a Minecraft skin, laid out on a 16x32 canvas: head, body, both arms and both legs, each with its outer
// layer. Skins from before 1.8 (64x32) have no left limbs, so the game mirrors the right ones; so does this.
QImage renderSide(const QImage& source, bool slim, bool back, const QImage& capeSource)
{
    const QImage skin = source.convertToFormat(QImage::Format_ARGB32);
    const bool modern = skin.height() >= 64;
    const int arm = slim ? 3 : 4;
    QImage out(16, 32, QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    QPainter p(&out);
    auto part = [&](int sx, int sy, int w, int h, int dx, int dy, bool mirror = false) {
        QImage piece = skin.copy(sx, sy, w, h);
        if (mirror)
            piece = piece.mirrored(true, false);
        p.drawImage(dx, dy, piece);
    };
    // Seen from behind, the player's right arm and leg are on the viewer's right.
    const int rightArmX = back ? 12 : 4 - arm;
    const int leftArmX = back ? 4 - arm : 12;
    const int rightLegX = back ? 8 : 4;
    const int leftLegX = back ? 4 : 8;
    const int rightArmU = back ? 48 + arm : 44;
    const int leftArmU = back ? 40 + arm : 36;
    const int rightLegU = back ? 12 : 4;
    const int leftLegU = back ? 28 : 20;

    part(back ? 24 : 8, 8, 8, 8, 4, 0);          // head
    part(back ? 32 : 20, 20, 8, 12, 4, 8);       // body
    part(rightArmU, 20, arm, 12, rightArmX, 8);  // right arm
    part(rightLegU, 20, 4, 12, rightLegX, 20);   // right leg
    if (modern) {
        part(leftArmU, 52, arm, 12, leftArmX, 8);
        part(leftLegU, 52, 4, 12, leftLegX, 20);
        part(back ? 32 : 20, 36, 8, 12, 4, 8);                 // jacket
        part(rightArmU, 36, arm, 12, rightArmX, 8);            // right sleeve
        part(leftArmU + 16, 52, arm, 12, leftArmX, 8);         // left sleeve
        part(rightLegU, 36, 4, 12, rightLegX, 20);             // right trouser leg
        part(leftLegU - 16, 52, 4, 12, leftLegX, 20);          // left trouser leg
    } else {
        part(rightArmU, 20, arm, 12, leftArmX, 8, true);
        part(rightLegU, 20, 4, 12, leftLegX, 20, true);
    }
    part(back ? 56 : 40, 8, 8, 8, 4, 0);  // hat
    if (back && !capeSource.isNull()) {
        // the outside of the cape; HD capes are larger multiples of 64x32
        const int unit = qMax(1, capeSource.width() / 64);
        p.drawImage(QRect(3, 8, 10, 16), capeSource.convertToFormat(QImage::Format_ARGB32).copy(unit, unit, 10 * unit, 16 * unit));
    }
    return out;
}
}  // namespace

// Shows one side of the skin at a crisp whole-number scale, or a quiet outline of the default shape while nothing is
// found. A click turns the figure around to show its back and cape.
class SkinFrontView : public QWidget {
   public:
    explicit SkinFrontView(QWidget* parent) : QWidget(parent)
    {
        setFixedSize(120, 216);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QObject::tr("Click to turn around"));
    }

    void setSkin(const QImage& skin, bool slim, const QImage& cape = {})
    {
        m_skin = skin;
        m_slim = slim;
        m_cape = cape;
        update();
    }
    void setCape(const QImage& cape)
    {
        m_cape = cape;
        update();
    }
    bool showingBack() const { return m_back; }

    std::function<void()> onTurned;

   protected:
    void mousePressEvent(QMouseEvent*) override
    {
        m_back = !m_back;
        update();
        if (onTurned)
            onTurned();
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath card;
        card.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
        p.fillPath(card, palette().color(QPalette::AlternateBase));
        p.setPen(QPen(palette().color(QPalette::Mid), 1));
        p.drawPath(card);

        const int scale = 6;
        const QRect target((width() - 16 * scale) / 2, (height() - 32 * scale) / 2, 16 * scale, 32 * scale);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        if (!m_skin.isNull()) {
            if (!isEnabled())
                p.setOpacity(0.35);
            p.drawImage(target, renderSide(m_skin, m_slim, m_back, m_cape));
            return;
        }
        // the default figure's silhouette
        QColor shape = palette().color(QPalette::Mid);
        shape.setAlphaF(0.45);
        for (const QRect& r : { QRect(4, 0, 8, 8), QRect(4, 8, 8, 12), QRect(0, 8, 4, 12), QRect(12, 8, 4, 12), QRect(4, 20, 4, 12),
                                QRect(8, 20, 4, 12) })
            p.fillRect(QRect(target.x() + r.x() * scale, target.y() + r.y() * scale, r.width() * scale, r.height() * scale), shape);
    }

   private:
    QImage m_skin;
    QImage m_cape;
    bool m_slim = false;
    bool m_back = false;
};

OfflineSkinDialog::OfflineSkinDialog(QWidget* parent, MinecraftAccountPtr account) : QDialog(parent), m_account(std::move(account))
{
    const auto* data = m_account->accountData();
    setWindowTitle(tr("Skin for %1").arg(data->profileName()));
    setModal(true);
    setMinimumWidth(520);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 20);
    root->setSpacing(10);

    auto* title = new QLabel(tr("Skin for %1").arg(data->profileName()), this);
    title->setProperty("role", "title");
    auto* intro = new QLabel(tr("Offline accounts can wear any skin from Ely.by, a free skin site. Type an Ely.by player's name and "
                                "EMC puts that skin and cape on you in game."),
                             this);
    intro->setProperty("role", "body");
    intro->setWordWrap(true);
    root->addWidget(title);
    root->addWidget(intro);
    root->addSpacing(8);

    auto* row = new QHBoxLayout;
    row->setSpacing(20);
    auto* previewColumn = new QVBoxLayout;
    previewColumn->setSpacing(6);
    m_preview = new SkinFrontView(this);
    m_side = new QLabel(tr("Front · click to turn"), this);
    m_side->setProperty("role", "note");
    m_side->setAlignment(Qt::AlignHCenter);
    m_preview->onTurned = [this] { m_side->setText(m_preview->showingBack() ? tr("Back · click to turn") : tr("Front · click to turn")); };
    previewColumn->addWidget(m_preview);
    previewColumn->addWidget(m_side);
    previewColumn->addStretch(1);
    row->addLayout(previewColumn);

    auto* side = new QVBoxLayout;
    side->setSpacing(10);
    m_enabled = new QCheckBox(tr("Wear an Ely.by skin"), this);
    m_enabled->setChecked(data->offlineSkin);
    side->addWidget(m_enabled);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setHorizontalSpacing(14);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_name = new QLineEdit(this);
    m_name->setText(data->offlineSkinLookupName());
    m_name->setPlaceholderText(data->profileName());
    m_name->setMaxLength(32);
    m_name->setClearButtonEnabled(true);
    m_name->setValidator(new QRegularExpressionValidator(QRegularExpression("^[A-Za-z0-9_.\\-]*$"), m_name));
    form->addRow(tr("Ely.by name"), m_name);
    side->addLayout(form);

    m_status = new QLabel(this);
    m_status->setProperty("role", "body");
    m_status->setWordWrap(true);
    m_status->setMinimumHeight(m_status->fontMetrics().lineSpacing() * 2);
    m_status->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    side->addWidget(m_status);
    side->addStretch(1);

    auto* note = new QLabel(tr("Your name, UUID and worlds stay the same. Other players see the skin on servers that load Ely.by "
                               "skins (with SkinsRestorer, for example)."),
                            this);
    note->setProperty("role", "note");
    note->setWordWrap(true);
    side->addWidget(note);

    auto* links = new QLabel(this);
    links->setProperty("role", "note");
    links->setTextFormat(Qt::RichText);
    links->setOpenExternalLinks(true);
    links->setText(QString("<a href=\"https://ely.by/skins\">%1</a> &nbsp;·&nbsp; <a href=\"https://account.ely.by/register\">%2</a>")
                       .arg(tr("Browse skins on Ely.by"), tr("Upload your own (free account)")));
    side->addWidget(links);
    row->addLayout(side, 1);
    root->addLayout(row);

    root->addSpacing(6);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    cancel->setAutoDefault(false);
    m_save = new QPushButton(tr("Save"), this);
    m_save->setProperty("accent", true);
    m_save->setDefault(true);
    m_save->setMinimumWidth(110);
    buttons->addWidget(cancel);
    buttons->addWidget(m_save);
    root->addLayout(buttons);

    m_lookup = new ElySkinLookup(this);
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(450);
    connect(m_debounce, &QTimer::timeout, this, &OfflineSkinDialog::lookUp);
    connect(m_lookup, &ElySkinLookup::finished, this, &OfflineSkinDialog::onLookup);
    connect(m_name, &QLineEdit::textChanged, this, [this] {
        m_debounce->start();
        updateEnabled();
    });
    connect(m_enabled, &QCheckBox::toggled, this, &OfflineSkinDialog::updateEnabled);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_save, &QPushButton::clicked, this, &OfflineSkinDialog::save);

    updateEnabled();
    lookUp();
}

OfflineSkinDialog::~OfflineSkinDialog()
{
    if (m_download)
        m_download->abort();
    if (m_capeDownload)
        m_capeDownload->abort();
}

void OfflineSkinDialog::lookUp()
{
    m_debounce->stop();
    if (m_download)
        m_download->abort();
    m_skinPng.clear();
    m_foundFor.clear();
    if (m_capeDownload)
        m_capeDownload->abort();
    m_preview->setSkin({}, false);
    const QString name = m_name->text().trimmed();
    if (name.isEmpty()) {
        setStatus(tr("Type a name to see its skin."));
        return;
    }
    setStatus(tr("Looking up %1 on Ely.by…").arg(name));
    m_lookup->start(name);
}

void OfflineSkinDialog::onLookup(const ElySkinLookup::Result& result)
{
    const QString name = m_name->text().trimmed();
    if (result.kind == ElySkinLookup::Result::NotFound) {
        setStatus(tr("No Ely.by player called %1 has a skin. You'll play with the default skin.").arg(name));
        return;
    }
    if (result.kind == ElySkinLookup::Result::Failed) {
        setStatus(tr("Couldn't reach Ely.by: %1").arg(result.error), true);
        return;
    }
    setStatus(tr("Loading %1's skin…").arg(result.elyName));
    QNetworkRequest request(result.skinUrl);
    request.setTransferTimeout(10000);
    m_download = APPLICATION->network()->get(request);
    auto* reply = m_download.data();
    connect(reply, &QNetworkReply::finished, this, [this, reply, result] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->error() != QNetworkReply::OperationCanceledError)
                setStatus(tr("Couldn't download the skin: %1").arg(reply->errorString()), true);
            return;
        }
        const QByteArray png = reply->readAll();
        const QImage image = QImage::fromData(png, "PNG");
        if (image.width() != 64 || (image.height() != 64 && image.height() != 32)) {
            setStatus(tr("%1's skin is not a standard 64×64 skin, but the game may still show it.").arg(result.elyName));
            return;
        }
        m_skinPng = png;
        m_foundFor = result.elyName;
        m_foundSlim = result.slim;
        m_preview->setSkin(image, result.slim);
        const QString arms = result.slim ? tr("slim arms") : tr("classic arms");
        setStatus(result.capeUrl.isEmpty() ? tr("You'll wear %1's skin (%2).").arg(result.elyName, arms)
                                           : tr("You'll wear %1's skin (%2) and cape.").arg(result.elyName, arms));
        if (!result.capeUrl.isEmpty()) {
            QNetworkRequest capeRequest(result.capeUrl);
            capeRequest.setTransferTimeout(10000);
            m_capeDownload = APPLICATION->network()->get(capeRequest);
            auto* capeReply = m_capeDownload.data();
            connect(capeReply, &QNetworkReply::finished, this, [this, capeReply] {
                capeReply->deleteLater();
                if (capeReply->error() == QNetworkReply::NoError)
                    m_preview->setCape(QImage::fromData(capeReply->readAll(), "PNG"));
            });
        }
    });
}

void OfflineSkinDialog::setStatus(const QString& text, bool error)
{
    m_status->setText(text);
    m_status->setStyleSheet(error ? "color: #FF6961;" : QString());
}

void OfflineSkinDialog::updateEnabled()
{
    const bool on = m_enabled->isChecked();
    m_name->setEnabled(on);
    m_preview->setEnabled(on);
    m_status->setEnabled(on);
    m_preview->update();
}

void OfflineSkinDialog::save()
{
    auto* data = m_account->accountData();
    const QString name = m_name->text().trimmed();
    data->offlineSkin = m_enabled->isChecked() && !name.isEmpty();
    data->offlineSkinName = name.compare(data->profileName(), Qt::CaseInsensitive) == 0 ? QString() : name;
    // the account list and toolbar show the face of the skin being worn
    auto& skin = data->minecraftProfile.skin;
    if (data->offlineSkin && !m_skinPng.isEmpty() && m_foundFor.compare(name, Qt::CaseInsensitive) == 0) {
        skin.data = m_skinPng;
        skin.id = m_foundFor;
        skin.variant = m_foundSlim ? "SLIM" : "CLASSIC";
    } else {
        skin = Skin();
    }
    emit m_account->changed();
    accept();
}
