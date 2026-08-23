// SPDX-License-Identifier: MIT

#include "SegmentedControlPage.h"

#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <vkui/core/VkIcon.h>
#include <vkui/widgets/VTextStyle.h>
#include <vkui/widgets/controls/VSegmentedControl.h>

SegmentedControlPage::SegmentedControlPage(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 14, 14);
    layout->setSpacing(14);

    auto* title = new QLabel(tr("Segmented Control"), this);
    vkui::setTextStyle(*title, vkui::VTextStyle::Title);
    layout->addWidget(title);
    auto* introduction = new QLabel(
        tr("A compact, single-selection control composed from standard checkable-button behavior. "
           "Use Left and Right arrow keys to move selection."),
        this);
    introduction->setWordWrap(true);
    layout->addWidget(introduction);

    auto* variants = new QGroupBox(tr("Content variants"), this);
    auto* variantLayout = new QVBoxLayout(variants);
    auto* textSegments = new vkui::VSegmentedControl(variants);
    textSegments->addSegment(tr("Day"));
    textSegments->addSegment(tr("Week"));
    textSegments->addSegment(tr("Month"));
    textSegments->setCurrentIndex(1);
    textSegments->setAccessibleName(tr("Time range"));
    variantLayout->addWidget(textSegments);

    auto* iconSegments = new vkui::VSegmentedControl(variants);
    iconSegments->addSegment(vkui::icon(vkui::VkSymbol::Document));
    iconSegments->addSegment(vkui::icon(vkui::VkSymbol::Folder));
    iconSegments->addSegment(vkui::icon(vkui::VkSymbol::Share));
    iconSegments->setSegmentAccessibleName(0, tr("Document"));
    iconSegments->setSegmentAccessibleName(1, tr("Folder"));
    iconSegments->setSegmentAccessibleName(2, tr("Shared item"));
    iconSegments->setCurrentIndex(0);
    iconSegments->setAccessibleName(tr("Content type: document, folder, or shared"));
    variantLayout->addWidget(iconSegments);

    auto* mixedSegments = new vkui::VSegmentedControl(variants);
    mixedSegments->addSegment(vkui::icon(vkui::VkSymbol::Information), tr("Summary"));
    mixedSegments->addSegment(vkui::icon(vkui::VkSymbol::Settings), tr("Options"));
    mixedSegments->addSegment(vkui::icon(vkui::VkSymbol::Share), tr("Sharing"));
    mixedSegments->setCurrentIndex(0);
    mixedSegments->setSegmentEnabled(1, false);
    mixedSegments->setAccessibleName(tr("Inspector section"));
    variantLayout->addWidget(mixedSegments);
    layout->addWidget(variants);

    auto* dynamicGroup = new QGroupBox(tr("Dynamic segments"), this);
    auto* dynamicLayout = new QVBoxLayout(dynamicGroup);
    auto* dynamic = new vkui::VSegmentedControl(dynamicGroup);
    dynamic->addSegment(tr("Alpha"));
    dynamic->addSegment(tr("Beta"));
    dynamic->addSegment(tr("Gamma"));
    dynamic->setCurrentIndex(0);
    dynamicLayout->addWidget(dynamic);
    auto* buttons = new QHBoxLayout;
    auto* insertButton =
        new QPushButton(vkui::icon(vkui::VkSymbol::Plus), tr("Insert segment"), dynamicGroup);
    auto* removeButton =
        new QPushButton(vkui::icon(vkui::VkSymbol::Minus), tr("Remove selected"), dynamicGroup);
    buttons->addWidget(insertButton);
    buttons->addWidget(removeButton);
    buttons->addStretch();
    dynamicLayout->addLayout(buttons);
    connect(insertButton, &QPushButton::clicked, this, [dynamic] {
        const int index = qMax(0, dynamic->currentIndex() + 1);
        dynamic->insertSegment(index, vkui::icon(vkui::VkSymbol::Plus),
                               SegmentedControlPage::tr("New"));
        dynamic->setCurrentIndex(index);
    });
    connect(removeButton, &QPushButton::clicked, this, [dynamic] {
        if (dynamic->count() > 1 && dynamic->currentIndex() >= 0) {
            dynamic->removeSegment(dynamic->currentIndex());
        }
    });
    layout->addWidget(dynamicGroup);

    auto* rtlGroup = new QGroupBox(tr("Right-to-left keyboard order"), this);
    auto* rtlLayout = new QVBoxLayout(rtlGroup);
    auto* rtl = new vkui::VSegmentedControl(rtlGroup);
    rtl->setLayoutDirection(Qt::RightToLeft);
    rtl->addSegment(QStringLiteral("الأول"));
    rtl->addSegment(QStringLiteral("الثاني"));
    rtl->addSegment(QStringLiteral("الثالث"));
    rtl->setCurrentIndex(0);
    rtl->setAccessibleName(tr("Right-to-left example"));
    rtlLayout->addWidget(rtl);
    layout->addWidget(rtlGroup);
    layout->addStretch();
}
