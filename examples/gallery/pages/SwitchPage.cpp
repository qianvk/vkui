// SPDX-License-Identifier: MIT

#include "SwitchPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <vkui/widgets/controls/VkSwitch.h>

namespace {

void addSwitchRow(QFormLayout* form, const QString& labelText, vkui::VkSwitch* control,
                  const QString& accessibleName) {
    auto* label = new QLabel(labelText, form->parentWidget());
    label->setBuddy(control);
    control->setAccessibleName(accessibleName);
    form->addRow(label, control);
}

} // namespace

SwitchPage::SwitchPage(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto* canvas = new QWidget(scrollArea);
    auto* layout = new QVBoxLayout(canvas);
    layout->setContentsMargins(4, 4, 14, 14);
    layout->setSpacing(14);

    auto* title = new QLabel(tr("Switch"), canvas);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 6.0);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    layout->addWidget(title);
    auto* introduction = new QLabel(
        tr("VkSwitch adds the one common binary control Qt Widgets does not provide. It keeps "
           "QAbstractButton semantics, including Space-key activation and checked-state signals."),
        canvas);
    introduction->setWordWrap(true);
    layout->addWidget(introduction);

    auto* states = new QGroupBox(tr("States and sizes"), canvas);
    auto* form = new QFormLayout(states);

    auto* uncheckedSwitch = new vkui::VkSwitch(states);
    addSwitchRow(form, tr("Unchecked"), uncheckedSwitch, tr("Unchecked example"));

    auto* checkedSwitch = new vkui::VkSwitch(states);
    checkedSwitch->setChecked(true);
    addSwitchRow(form, tr("Checked"), checkedSwitch, tr("Checked example"));

    auto* disabledSwitch = new vkui::VkSwitch(states);
    disabledSwitch->setChecked(true);
    disabledSwitch->setEnabled(false);
    addSwitchRow(form, tr("Disabled"), disabledSwitch, tr("Disabled example"));

    auto* focusedSwitch = new vkui::VkSwitch(states);
    focusedSwitch->setChecked(true);
    addSwitchRow(form, tr("Keyboard focus"), focusedSwitch, tr("Focused example"));

    auto* smallSwitch = new vkui::VkSwitch(states);
    smallSwitch->setControlSize(vkui::VkControlSize::Small);
    smallSwitch->setChecked(true);
    addSwitchRow(form, tr("Small"), smallSwitch, tr("Small switch example"));

    auto* regularSwitch = new vkui::VkSwitch(states);
    regularSwitch->setControlSize(vkui::VkControlSize::Regular);
    addSwitchRow(form, tr("Regular"), regularSwitch, tr("Regular switch example"));

    auto* largeSwitch = new vkui::VkSwitch(states);
    largeSwitch->setControlSize(vkui::VkControlSize::Large);
    largeSwitch->setChecked(true);
    addSwitchRow(form, tr("Large"), largeSwitch, tr("Large switch example"));
    layout->addWidget(states);

    auto* sizing = new QGroupBox(tr("Preset and exact sizing"), canvas);
    auto* sizingForm = new QFormLayout(sizing);
    auto* preset = new QComboBox(sizing);
    preset->addItem(tr("Small"), static_cast<int>(vkui::VkControlSize::Small));
    preset->addItem(tr("Regular"), static_cast<int>(vkui::VkControlSize::Regular));
    preset->addItem(tr("Large"), static_cast<int>(vkui::VkControlSize::Large));
    preset->addItem(tr("Custom"), -1);
    preset->setCurrentIndex(1);
    auto* exactExtent = new QSpinBox(sizing);
    exactExtent->setRange(vkui::VkMinimumControlExtent, vkui::VkMaximumControlExtent);
    exactExtent->setSuffix(QStringLiteral(" px"));
    exactExtent->setAccelerated(true);
    exactExtent->setValue(vkui::controlExtent(vkui::VkControlSize::Regular));
    sizingForm->addRow(tr("Preset"), preset);
    sizingForm->addRow(tr("Exact visual extent"), exactExtent);

    auto* preview = new QWidget(sizing);
    auto* previewLayout = new QHBoxLayout(preview);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    auto* previewCheck = new QCheckBox(tr("Checkbox"), preview);
    previewCheck->setChecked(true);
    auto* previewRadio = new QRadioButton(tr("Radio button"), preview);
    previewRadio->setChecked(true);
    auto* previewSwitch = new vkui::VkSwitch(preview);
    previewSwitch->setChecked(true);
    previewLayout->addWidget(previewCheck);
    previewLayout->addWidget(previewRadio);
    previewLayout->addWidget(previewSwitch);
    previewLayout->addStretch();
    sizingForm->addRow(tr("Live preview"), preview);

    auto applyPreset = [previewCheck, previewRadio, previewSwitch](vkui::VkControlSize size) {
        vkui::setControlSize(*previewCheck, size);
        vkui::setControlSize(*previewRadio, size);
        previewSwitch->setControlSize(size);
    };
    auto applyExact = [previewCheck, previewRadio, previewSwitch](int extent) {
        vkui::setControlExtent(*previewCheck, extent);
        vkui::setControlExtent(*previewRadio, extent);
        previewSwitch->setControlExtent(extent);
    };
    connect(preset, &QComboBox::currentIndexChanged, sizing,
            [preset, exactExtent, applyPreset, applyExact](int index) {
                const int value = preset->itemData(index).toInt();
                if (value < 0) {
                    applyExact(exactExtent->value());
                    return;
                }
                const auto size = static_cast<vkui::VkControlSize>(value);
                applyPreset(size);
                const QSignalBlocker blocker(exactExtent);
                exactExtent->setValue(vkui::controlExtent(size));
            });
    connect(exactExtent, &QSpinBox::valueChanged, sizing, [preset, applyExact](int extent) {
        const QSignalBlocker blocker(preset);
        preset->setCurrentIndex(3);
        applyExact(extent);
    });
    {
        const QSignalBlocker presetBlocker(preset);
        const QSignalBlocker extentBlocker(exactExtent);
        preset->setCurrentIndex(1);
        exactExtent->setValue(vkui::controlExtent(vkui::VkControlSize::Regular));
    }
    applyPreset(vkui::VkControlSize::Regular);
    layout->addWidget(sizing);

    auto* interaction = new QGroupBox(tr("Animation interruption"), canvas);
    auto* interactionLayout = new QHBoxLayout(interaction);
    auto* rapidSwitch = new vkui::VkSwitch(interaction);
    rapidSwitch->setAccessibleName(tr("Rapid toggling target"));
    auto* toggleButton = new QPushButton(tr("Run rapid toggling"), interaction);
    auto* timer = new QTimer(interaction);
    timer->setInterval(55);
    interactionLayout->addWidget(rapidSwitch);
    interactionLayout->addWidget(toggleButton);
    interactionLayout->addStretch();
    connect(toggleButton, &QPushButton::clicked, this, [timer, rapidSwitch] {
        rapidSwitch->setProperty("remainingRapidToggles", 12);
        timer->start();
    });
    connect(timer, &QTimer::timeout, this, [timer, rapidSwitch] {
        int remaining = rapidSwitch->property("remainingRapidToggles").toInt();
        rapidSwitch->toggle();
        --remaining;
        rapidSwitch->setProperty("remainingRapidToggles", remaining);
        if (remaining <= 0) {
            timer->stop();
        }
    });
    layout->addWidget(interaction);

    auto* note = new QLabel(
        tr("Use the appearance selector above to compare all examples in light, dark, and system "
           "appearance."),
        canvas);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch();

    scrollArea->setWidget(canvas);
    outer->addWidget(scrollArea);

    QTimer::singleShot(0, focusedSwitch, [focusedSwitch] { focusedSwitch->setFocus(); });
}
