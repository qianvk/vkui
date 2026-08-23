// SPDX-License-Identifier: MIT

#include "IconChosenWindow.h"

#include "IconCatalogDelegate.h"
#include "IconCatalogModel.h"
#include "IconCatalogProxyModel.h"
#include "IconSelectionStore.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

IconChosenWindow::IconChosenWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle(tr("Nerd Font icon chooser"));
    resize(1120, 760);

    model_ = new IconCatalogModel(this);
    proxy_ = new IconCatalogProxyModel(this);
    proxy_->setSourceModel(model_);
    persistencePath_ = IconSelectionStore::defaultPath();
    if (const auto selection = IconSelectionStore::load(persistencePath_)) {
        (void)model_->restoreSelection(*selection);
    }

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    auto* title = new QLabel(tr("Choose symbols for VkUI"), this);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 6.0);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* explanation = new QLabel(
        tr("This temporary tool contains the SVG-converted Nerd Font symbol catalog. "
           "Selections are saved automatically and remain available after the app closes."),
        this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto* controls = new QHBoxLayout;
    auto* search = new QLineEdit(this);
    search->setClearButtonEnabled(true);
    search->setPlaceholderText(tr("Search by name or code point, for example: folder, cod, F07B"));
    auto* selectedOnly = new QCheckBox(tr("Selected only"), this);
    controls->addWidget(search, 1);
    controls->addWidget(selectedOnly);
    layout->addLayout(controls);

    view_ = new QListView(this);
    view_->setModel(proxy_);
    view_->setItemDelegate(new IconCatalogDelegate(view_));
    view_->setViewMode(QListView::IconMode);
    view_->setResizeMode(QListView::Adjust);
    view_->setMovement(QListView::Static);
    view_->setUniformItemSizes(true);
    view_->setSelectionMode(QAbstractItemView::NoSelection);
    view_->setMouseTracking(true);
    view_->setSpacing(4);
    layout->addWidget(view_, 1);

    auto* actions = new QHBoxLayout;
    auto* selectFiltered = new QPushButton(tr("Select filtered"), this);
    auto* clearFiltered = new QPushButton(tr("Clear filtered"), this);
    auto* clearAll = new QPushButton(tr("Clear all"), this);
    auto* loadButton = new QPushButton(tr("Load selection…"), this);
    auto* exportButton = new QPushButton(tr("Export selection…"), this);
    status_ = new QLabel(this);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status_->setToolTip(persistencePath_);
    actions->addWidget(selectFiltered);
    actions->addWidget(clearFiltered);
    actions->addWidget(clearAll);
    actions->addStretch();
    actions->addWidget(status_);
    actions->addWidget(loadButton);
    actions->addWidget(exportButton);
    layout->addLayout(actions);

    persistTimer_ = new QTimer(this);
    persistTimer_->setSingleShot(true);
    persistTimer_->setInterval(250);

    connect(search, &QLineEdit::textChanged, this, [this](const QString& text) {
        proxy_->setSearchText(text);
        updateStatus();
    });
    connect(selectedOnly, &QCheckBox::toggled, this, [this](const bool enabled) {
        proxy_->setSelectedOnly(enabled);
        updateStatus();
    });
    connect(view_, &QListView::clicked, this, [this](const QModelIndex& proxyIndex) {
        const QModelIndex sourceIndex = proxy_->mapToSource(proxyIndex);
        const bool selected = sourceIndex.data(IconCatalogModel::SelectedRole).toBool();
        model_->setData(sourceIndex, !selected, IconCatalogModel::SelectedRole);
    });
    connect(model_, &IconCatalogModel::selectionChanged, this, [this] {
        updateStatus();
        persistTimer_->start();
    });
    connect(persistTimer_, &QTimer::timeout, this, &IconChosenWindow::persistSelection);
    connect(selectFiltered, &QPushButton::clicked, this, [this] { setFilteredSelection(true); });
    connect(clearFiltered, &QPushButton::clicked, this, [this] { setFilteredSelection(false); });
    connect(clearAll, &QPushButton::clicked, this, [this] {
        QList<int> rows;
        rows.reserve(model_->rowCount());
        for (int row = 0; row < model_->rowCount(); ++row) {
            rows.append(row);
        }
        model_->setRowsSelected(std::move(rows), false);
    });
    connect(loadButton, &QPushButton::clicked, this, &IconChosenWindow::loadSelection);
    connect(exportButton, &QPushButton::clicked, this, &IconChosenWindow::exportSelection);
    updateStatus();
}

IconChosenWindow::~IconChosenWindow() {
    persistSelection();
}

void IconChosenWindow::closeEvent(QCloseEvent* event) {
    persistSelection();
    QWidget::closeEvent(event);
}

void IconChosenWindow::updateStatus() {
    status_->setText(tr("%1 shown · %2 selected")
                         .arg(QLocale().toString(proxy_->rowCount()),
                              QLocale().toString(model_->selectedCount())));
}

void IconChosenWindow::persistSelection() {
    (void)IconSelectionStore::save(persistencePath_, model_->selectedEntries());
}

void IconChosenWindow::setFilteredSelection(const bool selected) {
    QList<int> sourceRows;
    sourceRows.reserve(proxy_->rowCount());
    for (int row = 0; row < proxy_->rowCount(); ++row) {
        sourceRows.append(proxy_->mapToSource(proxy_->index(row, 0)).row());
    }
    model_->setRowsSelected(std::move(sourceRows), selected);
}

void IconChosenWindow::loadSelection() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Load selected icons"), {},
                                                      tr("JSON files (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    QString errorMessage;
    const auto selection = IconSelectionStore::load(path, &errorMessage);
    if (!selection) {
        QMessageBox::warning(this, tr("Load failed"),
                             tr("The selected icon manifest could not be loaded:\n%1")
                                 .arg(errorMessage));
        return;
    }

    const int restoredCount = model_->restoreSelection(*selection);
    persistTimer_->stop();
    persistSelection();
    const qsizetype unavailableCount = selection->size() - restoredCount;
    if (unavailableCount > 0) {
        QMessageBox::information(
            this, tr("Selection loaded"),
            tr("%1 icon name(s) are not available in this catalog and were ignored.")
                .arg(QLocale().toString(static_cast<qlonglong>(unavailableCount))));
    }
}

void IconChosenWindow::exportSelection() {
    QString path = QFileDialog::getSaveFileName(this, tr("Export selected icons"),
                                                QStringLiteral("vkui-selected-icons.json"),
                                                tr("JSON files (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".json");
    }
    if (!IconSelectionStore::save(path, model_->selectedEntries())) {
        QMessageBox::warning(this, tr("Export failed"),
                             tr("The selected icon manifest could not be written."));
    }
}
