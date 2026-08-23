// SPDX-License-Identifier: MIT

#include "ThemePage.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFormLayout>
#include <QFontInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QtMath>
#include <vkui/core/VkAccentColor.h>
#include <vkui/core/VkAppearance.h>
#include <vkui/core/VkIcon.h>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/VCombobox.h>
#include <vkui/widgets/VTextStyle.h>
#include <vkui/widgets/controls/VSlider.h>
#include <vkui/widgets/controls/VSwitch.h>

namespace {

QString accentColorName(vkui::VkAccentColor accentColor) {
    switch (accentColor) {
    case vkui::VkAccentColor::Blue:
        return ThemePage::tr("Blue");
    case vkui::VkAccentColor::Purple:
        return ThemePage::tr("Purple");
    case vkui::VkAccentColor::Pink:
        return ThemePage::tr("Pink");
    case vkui::VkAccentColor::Red:
        return ThemePage::tr("Red");
    case vkui::VkAccentColor::Orange:
        return ThemePage::tr("Orange");
    case vkui::VkAccentColor::Yellow:
        return ThemePage::tr("Yellow");
    case vkui::VkAccentColor::Green:
        return ThemePage::tr("Green");
    case vkui::VkAccentColor::Graphite:
        return ThemePage::tr("Graphite");
    }
    return {};
}

} // namespace

ThemePage::ThemePage(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 14, 14);
    layout->setSpacing(14);

    auto* title = new QLabel(tr("Theme and Appearance"), this);
    vkui::setTextStyle(*title, vkui::VTextStyle::Title);
    layout->addWidget(title);
    auto* introduction = new QLabel(
        tr("Auto follows the platform color scheme. A resolved immutable theme supplies semantic "
           "colors, metrics, typography, and motion to every subsystem."),
        this);
    introduction->setWordWrap(true);
    layout->addWidget(introduction);

    auto* appearanceGroup = new QGroupBox(tr("Appearance"), this);
    auto* appearanceLayout = new QHBoxLayout(appearanceGroup);
    auto* buttons = new QButtonGroup(appearanceGroup);
    const QList<QPair<QString, vkui::VkAppearance>> choices{
        {tr("System"), vkui::VkAppearance::Auto},
        {tr("Light"), vkui::VkAppearance::Light},
        {tr("Dark"), vkui::VkAppearance::Dark},
    };
    for (const auto& choice : choices) {
        auto* button = new QRadioButton(choice.first, appearanceGroup);
        const int id = static_cast<int>(choice.second);
        buttons->addButton(button, id);
        button->setChecked(vkui::VkThemeManager::instance()->appearance() == choice.second);
        appearanceLayout->addWidget(button);
    }
    appearanceLayout->addStretch();
    connect(buttons, &QButtonGroup::idClicked, this, [](int id) {
        vkui::VkThemeManager::instance()->setAppearance(static_cast<vkui::VkAppearance>(id));
    });
    connect(vkui::VkThemeManager::instance(), &vkui::VkThemeManager::appearanceChanged, this,
            [buttons](vkui::VkAppearance appearance) {
                if (auto* button = buttons->button(static_cast<int>(appearance))) {
                    button->setChecked(true);
                }
            });
    layout->addWidget(appearanceGroup);

    auto* accentGroup = new QGroupBox(tr("Accent color"), this);
    auto* accentLayout = new QGridLayout(accentGroup);
    auto* accentButtons = new QButtonGroup(accentGroup);
    const QList<vkui::VkAccentColor> accents{
        vkui::VkAccentColor::Blue,   vkui::VkAccentColor::Purple,
        vkui::VkAccentColor::Pink,   vkui::VkAccentColor::Red,
        vkui::VkAccentColor::Orange, vkui::VkAccentColor::Yellow,
        vkui::VkAccentColor::Green,  vkui::VkAccentColor::Graphite,
    };
    for (int index = 0; index < accents.size(); ++index) {
        const vkui::VkAccentColor accent = accents.at(index);
        auto* button = new QRadioButton(accentColorName(accent), accentGroup);
        const int id = static_cast<int>(accent);
        accentButtons->addButton(button, id);
        button->setChecked(vkui::VkThemeManager::instance()->accentColor() == accent);
        accentLayout->addWidget(button, index / 4, index % 4);
    }
    connect(accentButtons, &QButtonGroup::idClicked, this, [](int id) {
        vkui::VkThemeManager::instance()->setAccentColor(static_cast<vkui::VkAccentColor>(id));
    });
    connect(vkui::VkThemeManager::instance(), &vkui::VkThemeManager::accentColorChanged, this,
            [accentButtons](vkui::VkAccentColor accent) {
                if (auto* button = accentButtons->button(static_cast<int>(accent))) {
                    button->setChecked(true);
                }
            });
    layout->addWidget(accentGroup);

    auto* textSizeGroup = new QGroupBox(tr("Interface text size"), this);
    textSizeGroup->setObjectName(QStringLiteral("interfaceTextSizeGroup"));
    auto* textSizeLayout = new QVBoxLayout(textSizeGroup);
    auto* textSizeExplanation = new QLabel(
        tr("Text follows the platform system font. Controls, meaningful icons, spacing, and "
           "semantic text styles respond without uniformly zooming window chrome."),
        textSizeGroup);
    textSizeExplanation->setWordWrap(true);
    textSizeLayout->addWidget(textSizeExplanation);

    auto* sliderRow = new QHBoxLayout;
    auto* minimumLabel = new QLabel(tr("80%"), textSizeGroup);
    auto* textScaleSlider = new vkui::VSlider(Qt::Horizontal, textSizeGroup);
    textScaleSlider->setObjectName(QStringLiteral("interfaceTextScaleSlider"));
    textScaleSlider->setAccessibleName(tr("Interface text size"));
    textScaleSlider->setRange(qRound(vkui::VkMinimumTextScale * 100.0),
                              qRound(vkui::VkMaximumTextScale * 100.0));
    textScaleSlider->setSingleStep(qRound(vkui::VkTextScaleStep * 100.0));
    textScaleSlider->setPageStep(10);
    textScaleSlider->setTickInterval(10);
    textScaleSlider->setTickPosition(QSlider::TicksBelow);
    textScaleSlider->setTracking(true);
    textScaleSlider->setValue(
        qRound(vkui::VkThemeManager::instance()->textScale() * 100.0));
    auto* maximumLabel = new QLabel(tr("160%"), textSizeGroup);
    sliderRow->addWidget(minimumLabel);
    sliderRow->addWidget(textScaleSlider, 1);
    sliderRow->addWidget(maximumLabel);
    textSizeLayout->addLayout(sliderRow);

    auto* scaleFooter = new QHBoxLayout;
    textScaleValueLabel_ = new QLabel(textSizeGroup);
    vkui::setTextStyle(*textScaleValueLabel_, vkui::VTextStyle::BodyEmphasized);
    auto* resetTextScale = new QPushButton(tr("Reset to 100%"), textSizeGroup);
    scaleFooter->addWidget(textScaleValueLabel_);
    scaleFooter->addStretch();
    scaleFooter->addWidget(resetTextScale);
    textSizeLayout->addLayout(scaleFooter);

    auto* previewRow = new QHBoxLayout;
    auto* previewButton = new QPushButton(vkui::icon(vkui::VkSymbol::Settings),
                                          tr("Settings"), textSizeGroup);
    auto* previewCheck = new QCheckBox(tr("Option"), textSizeGroup);
    previewCheck->setChecked(true);
    auto* previewCombo = new vkui::VCombobox(textSizeGroup);
    previewCombo->addItems({tr("System"), tr("Automatic")});
    previewCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    auto* previewSwitch = new vkui::VSwitch(textSizeGroup);
    previewSwitch->setAccessibleName(tr("Preview switch"));
    previewSwitch->setChecked(true);
    previewRow->addWidget(previewButton);
    previewRow->addWidget(previewCheck);
    previewRow->addWidget(previewCombo);
    previewRow->addWidget(previewSwitch);
    previewRow->addStretch();
    textSizeLayout->addLayout(previewRow);

    connect(textScaleSlider, &QSlider::valueChanged, this,
            [textScaleSlider](const int percent) {
                const int minimum = textScaleSlider->minimum();
                const int step = textScaleSlider->singleStep();
                const int canonicalPercent =
                    minimum + qRound(static_cast<qreal>(percent - minimum) / step) * step;
                if (canonicalPercent != percent) {
                    const QSignalBlocker blocker(textScaleSlider);
                    textScaleSlider->setValue(canonicalPercent);
                }
                vkui::VkThemeManager::instance()->setTextScale(canonicalPercent / 100.0);
            });
    connect(resetTextScale, &QPushButton::clicked, vkui::VkThemeManager::instance(),
            &vkui::VkThemeManager::resetTextScale);
    connect(vkui::VkThemeManager::instance(), &vkui::VkThemeManager::textScaleChanged,
            textScaleSlider, [textScaleSlider](const qreal scale) {
                const QSignalBlocker blocker(textScaleSlider);
                textScaleSlider->setValue(qRound(scale * 100.0));
            });
    layout->addWidget(textSizeGroup);

    auto* motionGroup = new QGroupBox(tr("Motion policy"), this);
    auto* motionLayout = new QHBoxLayout(motionGroup);
    auto* motionLabel = new QLabel(tr("Enable interface animations"), motionGroup);
    auto* motionSwitch = new vkui::VSwitch(motionGroup);
    motionSwitch->setAccessibleName(tr("Enable interface animations"));
    motionSwitch->setChecked(vkui::VkThemeManager::instance()->animationsEnabled());
    motionLabel->setBuddy(motionSwitch);
    motionLayout->addWidget(motionLabel);
    motionLayout->addWidget(motionSwitch);
    motionLayout->addStretch();
    connect(motionSwitch, &vkui::VSwitch::toggled, vkui::VkThemeManager::instance(),
            &vkui::VkThemeManager::setAnimationsEnabled);
    connect(vkui::VkThemeManager::instance(), &vkui::VkThemeManager::animationsEnabledChanged,
            motionSwitch, &vkui::VSwitch::setChecked);
    layout->addWidget(motionGroup);

    auto* diagnostics = new QGroupBox(tr("Resolved theme"), this);
    auto* diagnosticsLayout = new QFormLayout(diagnostics);
    effectiveLabel_ = new QLabel(diagnostics);
    accentLabel_ = new QLabel(diagnostics);
    typographyLabel_ = new QLabel(diagnostics);
    generationLabel_ = new QLabel(diagnostics);
    diagnosticsLayout->addRow(tr("Effective appearance"), effectiveLabel_);
    diagnosticsLayout->addRow(tr("Accent color"), accentLabel_);
    diagnosticsLayout->addRow(tr("Responsive metrics"), typographyLabel_);
    diagnosticsLayout->addRow(tr("Theme generation"), generationLabel_);
    layout->addWidget(diagnostics);

    auto* note = new QLabel(
        tr("Theme changes update the application palette and invalidate generation-keyed icon and "
           "paint caches. VStyle itself is not recreated."),
        this);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch();

    connect(vkui::VkThemeManager::instance(), &vkui::VkThemeManager::themeChanged, this,
            [this] { updateSummary(); });
    updateSummary();
}

void ThemePage::updateSummary() {
    const auto* manager = vkui::VkThemeManager::instance();
    const auto appearance = manager->effectiveAppearance();
    effectiveLabel_->setText(appearance == vkui::VkAppearance::Dark ? tr("Dark") : tr("Light"));
    accentLabel_->setText(accentColorName(manager->accentColor()));

    const vkui::VkTheme& theme = manager->theme();
    const int percent = qRound(manager->textScale() * 100.0);
    const qreal bodyPoints = QFontInfo(theme.typography().body).pointSizeF();
    textScaleValueLabel_->setText(
        bodyPoints > 0.0
            ? tr("%1% · %2 pt body").arg(percent).arg(bodyPoints, 0, 'f', 1)
            : tr("%1% · %2 px body").arg(percent).arg(theme.typography().body.pixelSize()));
    const int controlHeight = qRound(theme.metrics().controlHeightRegular);
    const int iconExtent = qRound(theme.metrics().controlHeightSmall * 0.67);
    typographyLabel_->setText(
        tr("%1 px control · %2 px icon").arg(controlHeight).arg(iconExtent));
    generationLabel_->setText(QString::number(theme.generation()));
}
