// SPDX-License-Identifier: MIT

#include "StandardWidgetsPage.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTabBar>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>
#include <vkui/core/VkFileIcon.h>
#include <vkui/core/VkIcon.h>
#include <vkui/widgets/VCombobox.h>
#include <vkui/widgets/VControlSize.h>
#include <vkui/widgets/VTextStyle.h>
#include <vkui/widgets/views/VFileTreeView.h>

namespace {

QLabel* makeIntroduction(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QStandardItemModel* makeListModel(QObject* parent) {
    auto* model = new QStandardItemModel(parent);
    model->appendRow(new QStandardItem(vkui::icon(vkui::VkSymbol::FileText),
                                       StandardWidgetsPage::tr("Quarterly report")));
    model->appendRow(
        new QStandardItem(vkui::icon(vkui::VkSymbol::FileFolderClosed, vkui::VkIconRole::Accent),
                          StandardWidgetsPage::tr("Design resources")));
    model->appendRow(new QStandardItem(vkui::icon(vkui::VkSymbol::Share),
                                       StandardWidgetsPage::tr("Shared with me")));
    return model;
}

} // namespace

StandardWidgetsPage::StandardWidgetsPage(QWidget* parent) : QWidget(parent) {
    auto* canvas = this;
    auto* layout = new QVBoxLayout(canvas);
    layout->setContentsMargins(4, 4, 14, 14);
    layout->setSpacing(14);

    auto* title = new QLabel(tr("Standard Qt Widgets"), canvas);
    vkui::setTextStyle(*title, vkui::VTextStyle::Title);
    layout->addWidget(title);
    layout->addWidget(makeIntroduction(
        tr("vkui keeps Qt's interaction, keyboard, and accessibility semantics while VStyle "
           "provides a unified visual language."),
        canvas));

    auto* buttons = new QGroupBox(tr("Buttons and selection"), canvas);
    auto* buttonLayout = new QVBoxLayout(buttons);
    auto* buttonRow = new QHBoxLayout;
    auto* primary = new QPushButton(tr("Continue"), buttons);
    primary->setDefault(true);
    buttonRow->addWidget(primary);
    buttonRow->addWidget(new QPushButton(tr("Cancel"), buttons));
    auto* tool = new QToolButton(buttons);
    tool->setIcon(vkui::icon(vkui::VkSymbol::Settings));
    tool->setToolTip(tr("Settings"));
    tool->setAccessibleName(tr("Settings"));
    buttonRow->addWidget(tool);
    auto* menuButton = new QToolButton(buttons);
    menuButton->setText(tr("Actions"));
    menuButton->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(menuButton);
    menu->addAction(vkui::icon(vkui::VkSymbol::Plus), tr("New item"));
    menu->addAction(vkui::icon(vkui::VkSymbol::Share), tr("Share"));
    menu->addSeparator();
    menu->addAction(vkui::icon(vkui::VkSymbol::Close, vkui::VkIconRole::Destructive), tr("Remove"));
    menuButton->setMenu(menu);
    buttonRow->addWidget(menuButton);
    buttonRow->addStretch();
    buttonLayout->addLayout(buttonRow);

    auto* choiceRow = new QHBoxLayout;
    auto* check = new QCheckBox(tr("Keep me signed in"), buttons);
    check->setChecked(true);
    vkui::setControlSize(*check, vkui::VControlSize::Small);
    choiceRow->addWidget(check);
    auto* radioOne = new QRadioButton(tr("Balanced"), buttons);
    radioOne->setChecked(true);
    vkui::setControlSize(*radioOne, vkui::VControlSize::Regular);
    choiceRow->addWidget(radioOne);
    auto* largeRadio = new QRadioButton(tr("High quality"), buttons);
    vkui::setControlSize(*largeRadio, vkui::VControlSize::Large);
    choiceRow->addWidget(largeRadio);
    choiceRow->addStretch();
    buttonLayout->addLayout(choiceRow);
    layout->addWidget(buttons);

    auto* inputs = new QGroupBox(tr("Inputs"), canvas);
    auto* form = new QFormLayout(inputs);
    auto* search = new QLineEdit(inputs);
    search->setPlaceholderText(tr("Search documents"));
    search->setClearButtonEnabled(true);
    search->addAction(vkui::icon(vkui::VkSymbol::Search, vkui::VkIconRole::Secondary),
                      QLineEdit::LeadingPosition);
    form->addRow(tr("Search"), search);
    auto* combo = new vkui::VCombobox(inputs);
    combo->addItems({tr("Personal"), tr("Team"), tr("Public"),
                     tr("A workspace name that demonstrates screen-aware popup expansion")});
    form->addRow(tr("Workspace"), combo);
    auto* spinRow = new QHBoxLayout;
    auto* spin = new QSpinBox(inputs);
    spin->setRange(1, 100);
    spin->setValue(24);
    auto* doubleSpin = new QDoubleSpinBox(inputs);
    doubleSpin->setRange(0.0, 10.0);
    doubleSpin->setValue(4.5);
    doubleSpin->setSingleStep(0.5);
    spinRow->addWidget(spin);
    spinRow->addWidget(doubleSpin);
    form->addRow(tr("Values"), spinRow);
    auto* slider = new QSlider(Qt::Horizontal, inputs);
    slider->setValue(62);
    form->addRow(tr("Level"), slider);
    auto* progress = new QProgressBar(inputs);
    progress->setValue(72);
    form->addRow(tr("Progress"), progress);
    layout->addWidget(inputs);

    auto* navigation = new QGroupBox(tr("Navigation and data"), canvas);
    auto* navigationLayout = new QVBoxLayout(navigation);
    auto* tabs = new QTabBar(navigation);
    tabs->addTab(tr("Overview"));
    tabs->addTab(tr("Activity"));
    tabs->addTab(tr("Details"));
    navigationLayout->addWidget(tabs);

    auto* views = new QHBoxLayout;
    auto* list = new QListView(navigation);
    list->setModel(makeListModel(list));
    list->setCurrentIndex(list->model()->index(0, 0));
    views->addWidget(list);

    auto* tree = new vkui::VFileTreeView(navigation);
    tree->setSelectionMode(QAbstractItemView::NoSelection);
    tree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* treeModel = new QStandardItemModel(tree);
    auto makeFolder = [](const QString& text) {
        auto* item = new QStandardItem(text);
        item->setData(true, vkui::VFileTreeDirectoryRole);
        return item;
    };
    auto makeFile = [](const QString& text, const QString& path) {
        auto* item = new QStandardItem(text);
        item->setData(false, vkui::VFileTreeDirectoryRole);
        item->setData(path, vkui::VFileTreePathRole);
        return item;
    };
    auto* rootItem = makeFolder(tr("vkui"));
    auto* sourceItem = makeFolder(tr("Source"));
    sourceItem->appendRow(makeFile(tr("VStyle.cpp"), QStringLiteral("src/VStyle.cpp")));
    sourceItem->appendRow(makeFile(tr("VSwitch.cpp"), QStringLiteral("src/VSwitch.cpp")));
    rootItem->appendRow(sourceItem);
    rootItem->appendRow(makeFolder(tr("Empty folder")));
    rootItem->appendRow(makeFile(tr("README.md"), QStringLiteral("README.md")));
    treeModel->appendRow(rootItem);
    tree->setModel(treeModel);
    tree->setMinimumHeight(150);
    tree->expand(rootItem->index());
    tree->expand(sourceItem->index());
    views->addWidget(tree);

    auto* table = new QTableView(navigation);
    auto* tableModel = new QStandardItemModel(3, 3, table);
    tableModel->setHorizontalHeaderLabels({tr("Name"), tr("Status"), tr("Updated")});
    const QStringList names{tr("Core"), tr("Widgets"), tr("Gallery")};
    for (int row = 0; row < names.size(); ++row) {
        tableModel->setItem(row, 0, new QStandardItem(names.at(row)));
        tableModel->setItem(row, 1, new QStandardItem(tr("Ready")));
        tableModel->setItem(row, 2, new QStandardItem(tr("Today")));
    }
    table->setModel(tableModel);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->hide();
    views->addWidget(table, 2);
    navigationLayout->addLayout(views);

    auto* scrollBar = new QScrollBar(Qt::Horizontal, navigation);
    scrollBar->setRange(0, 100);
    scrollBar->setValue(35);
    navigationLayout->addWidget(scrollBar);
    layout->addWidget(navigation);

    auto* frameGroup = new QGroupBox(tr("Surfaces"), canvas);
    auto* frameLayout = new QVBoxLayout(frameGroup);
    auto* frame = new QFrame(frameGroup);
    frame->setFrameShape(QFrame::StyledPanel);
    auto* frameInner = new QHBoxLayout(frame);
    frameInner->addWidget(new QLabel(tr("QFrame and QGroupBox remain standard widgets."), frame));
    frameInner->addStretch();
    frameLayout->addWidget(frame);
    layout->addWidget(frameGroup);
    layout->addStretch();
}
