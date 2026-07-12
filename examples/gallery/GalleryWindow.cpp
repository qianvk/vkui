// SPDX-License-Identifier: MIT

#include "GalleryWindow.h"

#include "pages/IconsPage.h"
#include "pages/LocalizationPage.h"
#include "pages/MotionPage.h"
#include "pages/PopoverPage.h"
#include "pages/SegmentedControlPage.h"
#include "pages/StandardWidgetsPage.h"
#include "pages/SwitchPage.h"
#include "pages/ThemePage.h"

#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QLocale>
#include <QStackedWidget>
#include <QStringListModel>
#include <QVBoxLayout>
#include <vkui/core/VkAppearance.h>
#include <vkui/core/VkThemeManager.h>

GalleryWindow::GalleryWindow(QWidget* parent) : QMainWindow(parent) {
    language_ = Language::System;
    applyLanguage(language_);
    setMinimumSize(880, 620);
}

void GalleryWindow::applyLanguage(Language language) {
    qApp->removeTranslator(&translator_);
    language_ = language;

    const bool useChinese =
        language == Language::SimplifiedChinese ||
        (language == Language::System && QLocale::system().language() == QLocale::Chinese);
    if (useChinese && translator_.load(QStringLiteral(":/vkui/translations/vkui_zh_CN.qm"))) {
        qApp->installTranslator(&translator_);
    }

    rebuildCentralWidget();
    updateWindowTitle();
}

void GalleryWindow::rebuildCentralWidget() {
    if (pages_ != nullptr) {
        currentPage_ = pages_->currentIndex();
    }

    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(20, 16, 20, 20);
    rootLayout->setSpacing(14);

    auto* header = new QHBoxLayout;
    auto* brand = new QLabel(tr("vkui Component Gallery"), central);
    QFont brandFont = brand->font();
    brandFont.setPointSizeF(brandFont.pointSizeF() + 4.0);
    brandFont.setWeight(QFont::DemiBold);
    brand->setFont(brandFont);
    header->addWidget(brand);
    header->addStretch();

    header->addWidget(new QLabel(tr("Appearance"), central));
    appearanceBox_ = new QComboBox(central);
    appearanceBox_->addItem(tr("System"), static_cast<int>(vkui::VkAppearance::Auto));
    appearanceBox_->addItem(tr("Light"), static_cast<int>(vkui::VkAppearance::Light));
    appearanceBox_->addItem(tr("Dark"), static_cast<int>(vkui::VkAppearance::Dark));
    const int appearanceIndex =
        appearanceBox_->findData(static_cast<int>(vkui::VkThemeManager::instance()->appearance()));
    appearanceBox_->setCurrentIndex(qMax(0, appearanceIndex));
    header->addWidget(appearanceBox_);

    header->addSpacing(10);
    header->addWidget(new QLabel(tr("Language"), central));
    languageBox_ = new QComboBox(central);
    languageBox_->addItem(tr("System"), static_cast<int>(Language::System));
    languageBox_->addItem(QStringLiteral("English"), static_cast<int>(Language::English));
    languageBox_->addItem(QStringLiteral("简体中文"),
                          static_cast<int>(Language::SimplifiedChinese));
    languageBox_->setCurrentIndex(qMax(0, languageBox_->findData(static_cast<int>(language_))));
    header->addWidget(languageBox_);
    rootLayout->addLayout(header);

    auto* separator = new QFrame(central);
    separator->setFrameShape(QFrame::HLine);
    rootLayout->addWidget(separator);

    auto* contentLayout = new QHBoxLayout;
    contentLayout->setSpacing(18);
    navigation_ = new QListView(central);
    navigation_->setObjectName(QStringLiteral("galleryNavigation"));
    navigation_->setAccessibleName(tr("Component pages"));
    navigation_->setMaximumWidth(205);
    navigation_->setMinimumWidth(175);
    navigation_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    navigationModel_ = new QStringListModel({tr("Standard Widgets"), tr("Switch"),
                                             tr("Segmented Control"), tr("Popover"), tr("Theme"),
                                             tr("Icons"), tr("Motion"), tr("Localization")},
                                            navigation_);
    navigation_->setModel(navigationModel_);
    contentLayout->addWidget(navigation_);

    pages_ = new QStackedWidget(central);
    pages_->addWidget(new StandardWidgetsPage(pages_));
    pages_->addWidget(new SwitchPage(pages_));
    pages_->addWidget(new SegmentedControlPage(pages_));
    pages_->addWidget(new PopoverPage(pages_));
    pages_->addWidget(new ThemePage(pages_));
    pages_->addWidget(new IconsPage(pages_));
    pages_->addWidget(new MotionPage(pages_));
    pages_->addWidget(new LocalizationPage(pages_));
    contentLayout->addWidget(pages_, 1);
    rootLayout->addLayout(contentLayout, 1);

    connect(navigation_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) {
                if (current.isValid()) {
                    currentPage_ = current.row();
                    pages_->setCurrentIndex(currentPage_);
                }
            });
    connect(appearanceBox_, &QComboBox::currentIndexChanged, this, [this](int index) {
        vkui::VkThemeManager::instance()->setAppearance(
            static_cast<vkui::VkAppearance>(appearanceBox_->itemData(index).toInt()));
    });
    connect(languageBox_, &QComboBox::activated, this, [this](int index) {
        applyLanguage(static_cast<Language>(languageBox_->itemData(index).toInt()));
    });

    currentPage_ = qBound(0, currentPage_, pages_->count() - 1);
    navigation_->setCurrentIndex(navigationModel_->index(currentPage_));
    pages_->setCurrentIndex(currentPage_);
    setCentralWidget(central);
}

void GalleryWindow::updateWindowTitle() {
    setWindowTitle(tr("vkui Gallery"));
}
