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
#include <QToolButton>
#include <QVBoxLayout>
#include <vkui/core/VkIcon.h>
#include <vkui/core/VkAppearance.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/window/VkWindowAgent.h>

GalleryWindow::GalleryWindow(QWidget* parent) : QMainWindow(parent) {
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    windowAgent_ = new vkui::VkWindowAgent(this);
    if (windowAgent_->setup(this)) {
        nativeSystemButtonsAvailable_ = windowAgent_->installSystemButtons();
        windowAgent_->setSystemButtonVisibility(
            vkui::VkWindowAgent::SystemButtonVisibility::AlwaysVisible);
    }
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

    if (windowAgent_ != nullptr) {
        windowAgent_->clearTitleBars();
    }

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("galleryRoot"));
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 20);
    rootLayout->setSpacing(0);

    auto* titleBarRow = new QHBoxLayout;
    titleBarRow->setContentsMargins(0, 0, 0, 0);
    titleBarRow->setSpacing(0);

    auto* navigationTitleBar = new QWidget(central);
    navigationTitleBar->setObjectName(QStringLiteral("galleryNavigationTitleBar"));
    navigationTitleBar->setAttribute(Qt::WA_StyledBackground, true);
    navigationTitleBar->setFixedHeight(56);
    navigationTitleBar->setMinimumWidth(215);
    navigationTitleBar->setMaximumWidth(245);
    auto* navigationTitleLayout = new QHBoxLayout(navigationTitleBar);
#ifdef Q_OS_MAC
    navigationTitleLayout->setContentsMargins(86, 0, 12, 0);
#else
    navigationTitleLayout->setContentsMargins(20, 0, 12, 0);
#endif
    auto* brand = new QLabel(tr("vkui Gallery"), navigationTitleBar);
    QFont brandFont = brand->font();
    brandFont.setPointSizeF(brandFont.pointSizeF() + 4.0);
    brandFont.setWeight(QFont::DemiBold);
    brand->setFont(brandFont);
    navigationTitleLayout->addWidget(brand);
    titleBarRow->addWidget(navigationTitleBar);

    auto* contentTitleBar = new QWidget(central);
    contentTitleBar->setObjectName(QStringLiteral("galleryContentTitleBar"));
    contentTitleBar->setAttribute(Qt::WA_StyledBackground, true);
    contentTitleBar->setFixedHeight(56);
    auto* contentTitleLayout = new QHBoxLayout(contentTitleBar);
    contentTitleLayout->setContentsMargins(18, 0, 14, 0);
    contentTitleLayout->addStretch();

    contentTitleLayout->addWidget(new QLabel(tr("Appearance"), contentTitleBar));
    appearanceBox_ = new QComboBox(contentTitleBar);
    appearanceBox_->addItem(tr("System"), static_cast<int>(vkui::VkAppearance::Auto));
    appearanceBox_->addItem(tr("Light"), static_cast<int>(vkui::VkAppearance::Light));
    appearanceBox_->addItem(tr("Dark"), static_cast<int>(vkui::VkAppearance::Dark));
    const int appearanceIndex =
        appearanceBox_->findData(static_cast<int>(vkui::VkThemeManager::instance()->appearance()));
    appearanceBox_->setCurrentIndex(qMax(0, appearanceIndex));
    contentTitleLayout->addWidget(appearanceBox_);

    contentTitleLayout->addSpacing(10);
    contentTitleLayout->addWidget(new QLabel(tr("Language"), contentTitleBar));
    languageBox_ = new QComboBox(contentTitleBar);
    languageBox_->addItem(tr("System"), static_cast<int>(Language::System));
    languageBox_->addItem(QStringLiteral("English"), static_cast<int>(Language::English));
    languageBox_->addItem(QStringLiteral("简体中文"),
                          static_cast<int>(Language::SimplifiedChinese));
    languageBox_->setCurrentIndex(qMax(0, languageBox_->findData(static_cast<int>(language_))));
    contentTitleLayout->addWidget(languageBox_);

    QList<QWidget*> interactiveWidgets{appearanceBox_, languageBox_};
    if (nativeSystemButtonsAvailable_) {
#ifndef Q_OS_MAC
        contentTitleLayout->addSpacing(132);
#endif
    } else {
        auto makeCaptionButton = [contentTitleBar](const QIcon& icon, const QString& tooltip) {
            auto* button = new QToolButton(contentTitleBar);
            button->setAutoRaise(true);
            button->setFocusPolicy(Qt::NoFocus);
            button->setIcon(icon);
            button->setIconSize(QSize(14, 14));
            button->setToolTip(tooltip);
            button->setFixedSize(36, 32);
            return button;
        };
        auto* minimizeButton =
            makeCaptionButton(vkui::icon(vkui::VkSymbol::Minus), tr("Minimize"));
        auto* maximizeButton =
            makeCaptionButton(vkui::icon(vkui::VkSymbol::Plus), tr("Maximize"));
        auto* closeButton =
            makeCaptionButton(vkui::icon(vkui::VkSymbol::Close), tr("Close"));
        connect(minimizeButton, &QToolButton::clicked, this, &QWidget::showMinimized);
        connect(maximizeButton, &QToolButton::clicked, this, [this] {
            isMaximized() ? showNormal() : showMaximized();
        });
        connect(closeButton, &QToolButton::clicked, this, &QWidget::close);
        contentTitleLayout->addWidget(minimizeButton);
        contentTitleLayout->addWidget(maximizeButton);
        contentTitleLayout->addWidget(closeButton);
        interactiveWidgets.append({minimizeButton, maximizeButton, closeButton});
    }
    titleBarRow->addWidget(contentTitleBar, 1);
    rootLayout->addLayout(titleBarRow);

    auto* separator = new QFrame(central);
    separator->setFrameShape(QFrame::HLine);
    rootLayout->addWidget(separator);

    auto* contentLayout = new QHBoxLayout;
    contentLayout->setContentsMargins(20, 14, 20, 0);
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

    registerWindowChrome(navigationTitleBar, contentTitleBar, interactiveWidgets);
}

void GalleryWindow::registerWindowChrome(
    QWidget* navigationTitleBar, QWidget* contentTitleBar,
    const QList<QWidget*>& interactiveWidgets) {
    if (windowAgent_ == nullptr) {
        return;
    }

    const bool navigationAdded = windowAgent_->addTitleBar(navigationTitleBar);
    const bool contentAdded = windowAgent_->addTitleBar(contentTitleBar);
    Q_ASSERT(navigationAdded);
    Q_ASSERT(contentAdded);
    for (QWidget* widget : interactiveWidgets) {
        windowAgent_->setHitTestVisible(widget, true);
    }
}

void GalleryWindow::updateWindowTitle() {
    setWindowTitle(tr("vkui Gallery"));
}
