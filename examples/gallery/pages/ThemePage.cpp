// SPDX-License-Identifier: MIT

#include "ThemePage.h"

#include <QButtonGroup>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>
#include <vkui/core/VkAccentColor.h>
#include <vkui/core/VkAppearance.h>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/controls/VkSwitch.h>

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
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 6.0);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
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

    auto* motionGroup = new QGroupBox(tr("Motion policy"), this);
    auto* motionLayout = new QHBoxLayout(motionGroup);
    auto* motionLabel = new QLabel(tr("Enable interface animations"), motionGroup);
    auto* motionSwitch = new vkui::VkSwitch(motionGroup);
    motionSwitch->setAccessibleName(tr("Enable interface animations"));
    motionSwitch->setChecked(vkui::VkThemeManager::instance()->animationsEnabled());
    motionLabel->setBuddy(motionSwitch);
    motionLayout->addWidget(motionLabel);
    motionLayout->addWidget(motionSwitch);
    motionLayout->addStretch();
    connect(motionSwitch, &vkui::VkSwitch::toggled, vkui::VkThemeManager::instance(),
            &vkui::VkThemeManager::setAnimationsEnabled);
    connect(vkui::VkThemeManager::instance(), &vkui::VkThemeManager::animationsEnabledChanged,
            motionSwitch, &vkui::VkSwitch::setChecked);
    layout->addWidget(motionGroup);

    auto* diagnostics = new QGroupBox(tr("Resolved theme"), this);
    auto* diagnosticsLayout = new QFormLayout(diagnostics);
    effectiveLabel_ = new QLabel(diagnostics);
    accentLabel_ = new QLabel(diagnostics);
    generationLabel_ = new QLabel(diagnostics);
    diagnosticsLayout->addRow(tr("Effective appearance"), effectiveLabel_);
    diagnosticsLayout->addRow(tr("Accent color"), accentLabel_);
    diagnosticsLayout->addRow(tr("Theme generation"), generationLabel_);
    layout->addWidget(diagnostics);

    auto* note = new QLabel(
        tr("Theme changes update the application palette and invalidate generation-keyed icon and "
           "paint caches. VkStyle itself is not recreated."),
        this);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch();

    connect(vkui::VkThemeManager::instance(), &vkui::VkThemeManager::themeChanged, this,
            [this] { updateSummary(); });
    updateSummary();
}

void ThemePage::updateSummary() {
    const auto appearance = vkui::VkThemeManager::instance()->effectiveAppearance();
    effectiveLabel_->setText(appearance == vkui::VkAppearance::Dark ? tr("Dark") : tr("Light"));
    accentLabel_->setText(accentColorName(vkui::VkThemeManager::instance()->accentColor()));
    generationLabel_->setText(
        QString::number(vkui::VkThemeManager::instance()->theme().generation()));
}
