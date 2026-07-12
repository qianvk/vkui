// SPDX-License-Identifier: MIT

#include "LocalizationPage.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

LocalizationPage::LocalizationPage(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 14, 14);
    layout->setSpacing(14);

    auto* title = new QLabel(tr("Localization"), this);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 6.0);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    layout->addWidget(title);
    auto* introduction = new QLabel(
        tr("The gallery can switch between the system language, English, and Simplified Chinese at "
           "runtime. Pages are rebuilt after the translator changes so every visible string "
           "updates."),
        this);
    introduction->setWordWrap(true);
    layout->addWidget(introduction);

    auto* sample = new QGroupBox(tr("Translation sample"), this);
    auto* form = new QFormLayout(sample);
    form->addRow(tr("Status"), new QLabel(tr("Ready"), sample));
    form->addRow(tr("Message"), new QLabel(tr("Your changes have been saved."), sample));
    form->addRow(tr("Action"), new QPushButton(tr("Continue"), sample));
    form->addRow(
        tr("Layout direction"),
        new QLabel(layoutDirection() == Qt::RightToLeft ? tr("Right to left") : tr("Left to right"),
                   sample));
    layout->addWidget(sample);

    auto* guidance = new QLabel(
        tr("Application authors can install their own QTranslator alongside vkui translations. "
           "Complete sentences are translated as units; UI text is never assembled from "
           "fragments."),
        this);
    guidance->setWordWrap(true);
    layout->addWidget(guidance);
    layout->addStretch();
}
