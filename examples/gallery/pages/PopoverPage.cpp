// SPDX-License-Identifier: MIT

#include "PopoverPage.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <vkui/core/VkIcon.h>
#include <vkui/widgets/overlays/VkPopover.h>

PopoverPage::PopoverPage(QWidget* parent) : QWidget(parent), popover_(new vkui::VkPopover(this)) {
    popover_->setClosePolicy(vkui::VkPopoverClosePolicyFlag::OutsideClick |
                             vkui::VkPopoverClosePolicyFlag::EscapeKey |
                             vkui::VkPopoverClosePolicyFlag::AnchorDestroyed |
                             vkui::VkPopoverClosePolicyFlag::WindowDeactivated);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 14, 14);
    layout->setSpacing(14);

    auto* title = new QLabel(tr("Popover"), this);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 6.0);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    layout->addWidget(title);
    auto* introduction = new QLabel(
        tr("VkPopover is an anchor-aware top-level overlay. Move or resize the gallery while it is "
           "open to see placement and arrow geometry update without recreation."),
        this);
    introduction->setWordWrap(true);
    layout->addWidget(introduction);

    auto* options = new QHBoxLayout;
    options->addWidget(new QLabel(tr("Preferred placement"), this));
    placementBox_ = new QComboBox(this);
    placementBox_->addItem(tr("Automatic"), static_cast<int>(vkui::VkPopoverPlacement::Automatic));
    placementBox_->addItem(tr("Below"), static_cast<int>(vkui::VkPopoverPlacement::Below));
    placementBox_->addItem(tr("Above"), static_cast<int>(vkui::VkPopoverPlacement::Above));
    placementBox_->addItem(tr("Right"), static_cast<int>(vkui::VkPopoverPlacement::Right));
    placementBox_->addItem(tr("Left"), static_cast<int>(vkui::VkPopoverPlacement::Left));
    options->addWidget(placementBox_);
    largeContent_ = new QCheckBox(tr("Large content"), this);
    options->addWidget(largeContent_);
    options->addStretch();
    layout->addLayout(options);

    auto* anchorGroup = new QGroupBox(tr("Anchor positions"), this);
    auto* anchorGrid = new QGridLayout(anchorGroup);
    const QList<QPair<QString, QPair<int, int>>> positions{
        {tr("Top left"), {0, 0}},    {tr("Top edge"), {0, 1}},      {tr("Top right"), {0, 2}},
        {tr("Left edge"), {1, 0}},   {tr("Window center"), {1, 1}}, {tr("Right edge"), {1, 2}},
        {tr("Bottom left"), {2, 0}}, {tr("Bottom edge"), {2, 1}},   {tr("Bottom right"), {2, 2}},
    };
    for (const auto& entry : positions) {
        auto* button = new QPushButton(entry.first, anchorGroup);
        anchorGrid->addWidget(button, entry.second.first, entry.second.second);
        connect(button, &QPushButton::clicked, this, [this, button] { openForAnchor(button); });
    }
    anchorGrid->setColumnStretch(0, 1);
    anchorGrid->setColumnStretch(1, 1);
    anchorGrid->setColumnStretch(2, 1);
    layout->addWidget(anchorGroup);

    auto* specialGroup = new QGroupBox(tr("Special anchors and live repositioning"), this);
    auto* specialLayout = new QGridLayout(specialGroup);

    auto* iconButton = new QPushButton(vkui::icon(vkui::VkSymbol::Settings),
                                       tr("Icon sub-rectangle"), specialGroup);
    specialLayout->addWidget(iconButton, 0, 0);
    connect(iconButton, &QPushButton::clicked, this, [this, iconButton] {
        const QRect contents =
            iconButton->style()->subElementRect(QStyle::SE_PushButtonContents, nullptr, iconButton);
        const int extent = iconButton->iconSize().width();
        const QRect iconRect(contents.left(), contents.center().y() - extent / 2, extent, extent);
        openForAnchor(iconButton, iconRect);
    });

    auto* toolbar = new QToolBar(specialGroup);
    toolbar->setWindowTitle(tr("Example toolbar"));
    QAction* toolbarAction = toolbar->addAction(vkui::icon(vkui::VkSymbol::More), tr("More"));
    specialLayout->addWidget(toolbar, 0, 1);
    if (auto* toolbarAnchor = toolbar->widgetForAction(toolbarAction)) {
        toolbarAnchor->setAccessibleName(tr("Toolbar Popover anchor"));
        connect(toolbarAction, &QAction::triggered, this,
                [this, toolbarAnchor] { openForAnchor(toolbarAnchor); });
    }

    auto* scrollArea = new QScrollArea(specialGroup);
    scrollArea->setFixedHeight(72);
    scrollArea->setWidgetResizable(false);
    auto* wideCanvas = new QWidget(scrollArea);
    wideCanvas->resize(760, 52);
    auto* scrollAnchor = new QPushButton(tr("Anchor inside a scrolling viewport"), wideCanvas);
    scrollAnchor->move(470, 8);
    wideCanvas->setMinimumSize(760, 52);
    scrollArea->setWidget(wideCanvas);
    scrollArea->horizontalScrollBar()->setValue(360);
    specialLayout->addWidget(scrollArea, 1, 0, 1, 2);
    connect(scrollAnchor, &QPushButton::clicked, this,
            [this, scrollAnchor] { openForAnchor(scrollAnchor); });

    auto* movingCanvas = new QWidget(specialGroup);
    movingCanvas->setMinimumHeight(52);
    auto* movingAnchor = new QPushButton(tr("Live anchor"), movingCanvas);
    movingAnchor->move(8, 8);
    auto* moveButton = new QPushButton(tr("Move anchor"), specialGroup);
    specialLayout->addWidget(movingCanvas, 2, 0);
    specialLayout->addWidget(moveButton, 2, 1);
    connect(movingAnchor, &QPushButton::clicked, this,
            [this, movingAnchor] { openForAnchor(movingAnchor); });
    connect(moveButton, &QPushButton::clicked, this, [movingCanvas, movingAnchor] {
        const int target = movingAnchor->x() < movingCanvas->width() / 2
                               ? qMax(8, movingCanvas->width() - movingAnchor->width() - 8)
                               : 8;
        movingAnchor->move(target, movingAnchor->y());
    });
    specialLayout->setColumnStretch(0, 1);
    specialLayout->setColumnStretch(1, 1);
    layout->addWidget(specialGroup);

    const qsizetype screenCount = QApplication::screens().size();
    auto* screenNote = new QLabel(
        screenCount > 1 ? tr("Multiple screens detected. Move the gallery between screens to "
                             "exercise per-screen "
                             "available geometry, including negative desktop coordinates.")
                        : tr("Connect a secondary screen to exercise per-screen placement. Edge "
                             "and corner cases "
                             "are covered by the unit tests."),
        this);
    screenNote->setWordWrap(true);
    layout->addWidget(screenNote);
    layout->addStretch();
}

QWidget* PopoverPage::makePopoverContent() {
    auto* content = new QWidget;
    content->setMinimumWidth(largeContent_->isChecked() ? 390 : 235);
    auto* layout = new QVBoxLayout(content);
    auto* title = new QLabel(tr("Anchored Popover"), content);
    QFont font = title->font();
    font.setWeight(QFont::DemiBold);
    title->setFont(font);
    layout->addWidget(title);
    auto* message =
        new QLabel(largeContent_->isChecked() ? tr("This larger surface demonstrates body "
                                                   "clamping. The body can move to stay visible "
                                                   "while the continuous arrow slides along its "
                                                   "edge and keeps pointing at the anchor.")
                                              : tr("Click outside or press Escape to close."),
                   content);
    message->setWordWrap(true);
    layout->addWidget(message);
    if (largeContent_->isChecked()) {
        layout->addWidget(
            new QLabel(tr("Popover content remains an ordinary QWidget hierarchy."), content));
        layout->addSpacing(30);
    }
    auto* closeButton = new QPushButton(tr("Done"), content);
    connect(closeButton, &QPushButton::clicked, popover_, &vkui::VkPopover::closeAnimated);
    layout->addWidget(closeButton, 0, Qt::AlignRight);
    return content;
}

void PopoverPage::openForAnchor(QWidget* anchor, const QRect& subRect) {
    popover_->setPreferredPlacement(
        static_cast<vkui::VkPopoverPlacement>(placementBox_->currentData().toInt()));
    popover_->setContentWidget(makePopoverContent());
    if (subRect.isEmpty()) {
        popover_->openFor(anchor);
    } else {
        popover_->openFor(anchor, subRect);
    }
}
