// SPDX-License-Identifier: MIT

#include "MotionPage.h"

#include <QGroupBox>
#include <QLabel>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QVBoxLayout>
#include <vkui/core/VkThemeManager.h>

MotionPage::MotionPage(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 14, 14);
    layout->setSpacing(14);

    auto* title = new QLabel(tr("Motion Specifications"), this);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 6.0);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    layout->addWidget(title);
    auto* introduction = new QLabel(
        tr("vkui exposes restrained duration and easing policy—not a public animation framework. "
           "Widgets own and safely retarget their private animation drivers."),
        this);
    introduction->setWordWrap(true);
    layout->addWidget(introduction);

    auto* demo = new QGroupBox(tr("Interactive motion roles"), this);
    auto* demoLayout = new QVBoxLayout(demo);
    progress_ = new QProgressBar(demo);
    progress_->setRange(0, 100);
    progress_->setTextVisible(false);
    demoLayout->addWidget(progress_);
    specification_ = new QLabel(demo);
    demoLayout->addWidget(specification_);

    auto* buttonRow = new QHBoxLayout;
    const QList<QPair<QString, vkui::VkMotionRole>> roles{
        {tr("State"), vkui::VkMotionRole::StateTransition},
        {tr("Enter"), vkui::VkMotionRole::Enter},
        {tr("Exit"), vkui::VkMotionRole::Exit},
        {tr("Emphasized enter"), vkui::VkMotionRole::EmphasizedEnter},
        {tr("Emphasized exit"), vkui::VkMotionRole::EmphasizedExit},
    };
    for (const auto& entry : roles) {
        auto* button = new QPushButton(entry.first, demo);
        buttonRow->addWidget(button);
        connect(button, &QPushButton::clicked, this,
                [this, entry] { play(entry.second, entry.first); });
    }
    buttonRow->addStretch();
    demoLayout->addLayout(buttonRow);
    layout->addWidget(demo);

    auto* note = new QLabel(
        tr("Disable animations on the Theme page to verify that every role immediately reaches its "
           "final state."),
        this);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch();

    animation_ = new QPropertyAnimation(progress_, "value", this);
    play(vkui::VkMotionRole::StateTransition, tr("State transition"));
}

void MotionPage::play(vkui::VkMotionRole role, const QString& name) {
    const vkui::VkMotionSpec spec = vkui::motionSpec(role);
    animation_->stop();
    progress_->setValue(0);
    const int duration =
        vkui::VkThemeManager::instance()->animationsEnabled() ? spec.durationMs : 0;
    specification_->setText(tr("%1 · %2 ms").arg(name).arg(duration));
    if (duration == 0) {
        progress_->setValue(100);
        return;
    }
    animation_->setDuration(duration);
    animation_->setEasingCurve(spec.easing);
    animation_->setStartValue(0);
    animation_->setEndValue(100);
    animation_->start();
}
