// SPDX-License-Identifier: MIT

#include "WindowPage.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
#include <QSpinBox>
#endif
#include <QVBoxLayout>
#include <vkui/widgets/VCombobox.h>
#include <vkui/window/VMessageDialog.h>
#include <vkui/window/VWindowAgent.h>

WindowPage::WindowPage(vkui::VWindowAgent& windowAgent, QWidget* parent)
    : QWidget(parent), windowAgent_(windowAgent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 14, 14);
    layout->setSpacing(14);

    auto* title = new QLabel(tr("Windows and Dialogs"), this);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 6.0);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* introduction = new QLabel(
        tr("VkUI composes application content inside shared full-content chrome. System buttons "
           "remain platform-owned on macOS and Windows."),
        this);
    introduction->setWordWrap(true);
    layout->addWidget(introduction);

    auto* placement = new QGroupBox(tr("Gallery window"), this);
    auto* placementLayout = new QFormLayout(placement);
    auto* centerButton = new QPushButton(tr("Center Window"), placement);
    placementLayout->addRow(tr("Screen"), centerButton);
    auto* systemButtons = new vkui::VCombobox(placement);
    systemButtons->addItem(tr("Standard"), vkui::VStandardSystemButtons.toInt());
    systemButtons->addItem(tr("Close only"),
                           vkui::VSystemButtons(vkui::VSystemButton::Close).toInt());
    systemButtons->addItem(tr("None"), vkui::VSystemButtons{}.toInt());
    systemButtons->setCurrentIndex(
        qMax(0, systemButtons->findData(windowAgent_.systemButtons().toInt())));
    placementLayout->addRow(tr("Buttons"), systemButtons);
    auto* visibility = new QCheckBox(tr("Show native system buttons"), placement);
    visibility->setChecked(windowAgent_.systemButtonsVisible());
    placementLayout->addRow(visibility);
    layout->addWidget(placement);

    auto* preferences = new QGroupBox(tr("Preferences window"), this);
    auto* preferencesLayout = new QVBoxLayout(preferences);
    auto* preferencesDescription = new QLabel(
        tr("An application-owned QWidget with close-only native chrome. It is created on demand, "
           "has at most one live instance, and releases its memory when closed."),
        preferences);
    preferencesDescription->setWordWrap(true);
    preferencesLayout->addWidget(preferencesDescription);
    auto* preferencesButton = new QPushButton(tr("Show Preferences Window"), preferences);
    preferencesLayout->addWidget(preferencesButton, 0, Qt::AlignLeft);
    layout->addWidget(preferences);

    auto* confirm = new QGroupBox(tr("Confirm window"), this);
    auto* confirmLayout = new QVBoxLayout(confirm);
    auto* confirmDescription = new QLabel(
        tr("VMessageDialog adds a severity icon, platform-ordered actions, and safe keyboard "
           "defaults to its private full-content chrome. Confirm prompts intentionally have no "
           "system buttons."),
        confirm);
    confirmDescription->setWordWrap(true);
    confirmLayout->addWidget(confirmDescription);
    auto* confirmActions = new QHBoxLayout;
    auto* confirmButton = new QPushButton(tr("Show Confirm Window"), confirm);
    confirmActions->addWidget(confirmButton);
    confirmResult_ = new QLabel(tr("No result yet."), confirm);
    confirmActions->addWidget(confirmResult_);
    confirmActions->addStretch();
    confirmLayout->addLayout(confirmActions);
    layout->addWidget(confirm);

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    auto* trafficLights = new QGroupBox(tr("Traffic-light position"), this);
    auto* form = new QFormLayout(trafficLights);

    horizontalOrigin_ = new QSpinBox(trafficLights);
    horizontalOrigin_->setRange(0, 240);
    horizontalOrigin_->setSuffix(tr(" px"));
    verticalOrigin_ = new QSpinBox(trafficLights);
    verticalOrigin_->setRange(0, 42);
    verticalOrigin_->setSuffix(tr(" px"));

    const QPoint origin = windowAgent_.trafficLightOrigin().value_or(QPoint(15, 15));
    horizontalOrigin_->setValue(origin.x());
    verticalOrigin_->setValue(origin.y());
    form->addRow(tr("Left"), horizontalOrigin_);
    form->addRow(tr("Top"), verticalOrigin_);

    auto* note = new QLabel(
        tr("Coordinates use Qt logical pixels relative to the full-content window. "
           "The native backend clamps the complete group inside AppKit's button container."),
        trafficLights);
    note->setWordWrap(true);
    form->addRow(note);
    layout->addWidget(trafficLights);
#endif
    layout->addStretch();

    connect(centerButton, &QPushButton::clicked, this, [this] { windowAgent_.centralize(); });
    connect(systemButtons, &QComboBox::currentIndexChanged, this,
            [this, systemButtons](const int index) {
                windowAgent_.setSystemButtons(
                    vkui::VSystemButtons::fromInt(systemButtons->itemData(index).toUInt()));
            });
    connect(visibility, &QCheckBox::toggled, this,
            [this](const bool visible) { windowAgent_.setSystemButtonsVisible(visible); });
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    connect(horizontalOrigin_, &QSpinBox::valueChanged, this,
            [this] { applyTrafficLightOrigin(); });
    connect(verticalOrigin_, &QSpinBox::valueChanged, this, [this] { applyTrafficLightOrigin(); });
#endif
    connect(preferencesButton, &QPushButton::clicked, this,
            [this] { emit preferencesRequested(); });
    connect(confirmButton, &QPushButton::clicked, this, [this] { showConfirmWindow(); });
}

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
void WindowPage::applyTrafficLightOrigin() {
    windowAgent_.setTrafficLightOrigin(
        QPoint(horizontalOrigin_->value(), verticalOrigin_->value()));
}
#endif

void WindowPage::showConfirmWindow() {
    const bool confirmed = vkui::VMessageDialog::confirmDestructive(
        window(), tr("Delete the sample file?"),
        tr("This gallery action demonstrates a destructive confirmation. No data will be "
           "deleted."),
        tr("Delete Sample"));
    confirmResult_->setText(confirmed ? tr("Result: confirmed") : tr("Result: cancelled"));
}
