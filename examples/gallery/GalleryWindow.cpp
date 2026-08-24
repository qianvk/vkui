// SPDX-License-Identifier: MIT

#include "GalleryWindow.h"

#include "GalleryContentView.h"
#include "pages/IconsPage.h"
#include "pages/LocalizationPage.h"
#include "pages/MotionPage.h"
#include "pages/PopoverPage.h"
#include "pages/SegmentedControlPage.h"
#include "pages/StandardWidgetsPage.h"
#include "pages/SwitchPage.h"
#include "pages/ThemePage.h"
#include "pages/WindowPage.h"

#include <QApplication>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLocale>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <vkui/core/VkAppearance.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/VCombobox.h>
#include <vkui/widgets/controls/VSplitter.h>
#include <vkui/widgets/effects/VLiquidGlass.h>
#include <vkui/widgets/views/VTreeView.h>

namespace {

constexpr int PageIndexRole = Qt::UserRole + 1;
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
constexpr QPoint GalleryTrafficLightOrigin{15, 15};
#endif

} // namespace

GalleryWindow::GalleryWindow(QWidget* parent) : QWidget(parent, Qt::Window), windowAgent_(*this) {
    auto* windowLayout = new QVBoxLayout(this);
    windowLayout->setContentsMargins(0, 0, 0, 0);
    windowLayout->setSpacing(0);
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    windowAgent_.setTrafficLightOrigin(GalleryTrafficLightOrigin);
#endif
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
    if (splitter_ != nullptr && !splitter_->sizes().isEmpty()) {
        navigationWidth_ = splitter_->sizes().constFirst();
    }

    windowAgent_.clearTitleBars();
    delete central_;
    central_ = nullptr;

    central_ = new QWidget(this);
    central_->setObjectName(QStringLiteral("galleryRoot"));
    auto* rootLayout = new QVBoxLayout(central_);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    splitter_ = new vkui::VSplitter(Qt::Horizontal, central_);
    splitter_->setObjectName(QStringLiteral("galleryPanelSplitter"));
    splitter_->setChildrenCollapsible(false);
    splitter_->setOpaqueResize(true);
    rootLayout->addWidget(splitter_);

    auto* navigationPanel = new QWidget(splitter_);
    navigationPanel->setObjectName(QStringLiteral("galleryNavigationPanel"));
    navigationPanel->setBackgroundRole(QPalette::Window);
    navigationPanel->setAutoFillBackground(true);
    navigationPanel->setMinimumWidth(176);
    auto* navigationPanelLayout = new QGridLayout(navigationPanel);
    navigationPanelLayout->setContentsMargins(0, 0, 0, 0);
    navigationPanelLayout->setSpacing(0);

    auto* navigationTitleBar = new QWidget(navigationPanel);
    navigationTitleBar->setObjectName(QStringLiteral("galleryNavigationTitleBar"));
    navigationTitleBar->setFixedHeight(GalleryContentView::TitleBarHeight);
    navigationTitleBar->setAutoFillBackground(false);
    navigationTitleBar->setAttribute(Qt::WA_StyledBackground, false);

    auto* navigationBody = new QWidget(navigationPanel);
    auto* navigationBodyLayout = new QVBoxLayout(navigationBody);
    navigationBodyLayout->setContentsMargins(12, GalleryContentView::TitleBarHeight, 8, 12);
    navigationBodyLayout->setSpacing(0);
    navigation_ = new vkui::VTreeView(navigationBody);
    navigation_->setObjectName(QStringLiteral("galleryNavigation"));
    navigation_->setBackgroundRole(QPalette::Window);
    navigation_->viewport()->setBackgroundRole(QPalette::Window);
    navigation_->setAccessibleName(tr("Component pages"));
    navigation_->setHeaderHidden(true);
    navigation_->setFrameShape(QFrame::NoFrame);
    navigation_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    navigation_->setSelectionBehavior(QAbstractItemView::SelectRows);
    navigation_->setSelectionMode(QAbstractItemView::SingleSelection);
    navigation_->setUniformRowHeights(true);
    navigation_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navigation_->setIndentation(18);

    navigationModel_ = new QStandardItemModel(navigation_);
    const auto addGroup = [this](const QString& text) {
        auto* group = new QStandardItem(text);
        group->setFlags(Qt::ItemIsEnabled);
        navigationModel_->appendRow(group);
        return group;
    };
    const auto addPage = [](QStandardItem* group, const QString& text, const int pageIndex) {
        auto* page = new QStandardItem(text);
        page->setData(pageIndex, PageIndexRole);
        page->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        group->appendRow(page);
    };

    QStandardItem* controls = addGroup(tr("Controls"));
    addPage(controls, tr("Standard Widgets"), 0);
    addPage(controls, tr("Switch"), 1);
    addPage(controls, tr("Segmented Control"), 2);
    addPage(controls, tr("Popover"), 3);
    QStandardItem* foundation = addGroup(tr("Foundation"));
    addPage(foundation, tr("Theme"), 4);
    addPage(foundation, tr("Icons"), 5);
    addPage(foundation, tr("Motion"), 6);
    addPage(foundation, tr("Localization"), 7);
    addPage(foundation, tr("Window"), 8);

    navigation_->setModel(navigationModel_);
    navigation_->expandAll();
    navigationBodyLayout->addWidget(navigation_);
    navigationPanelLayout->addWidget(navigationTitleBar, 0, 0, Qt::AlignTop);
    navigationPanelLayout->addWidget(navigationBody, 0, 0);
    navigationTitleBar->lower();
    splitter_->addWidget(navigationPanel);

    auto* contentPanel = new QWidget(splitter_);
    contentPanel->setObjectName(QStringLiteral("galleryContentPanel"));
    contentPanel->setBackgroundRole(QPalette::Base);
    contentPanel->setAutoFillBackground(true);
    contentPanel->setMinimumWidth(520);
    auto* contentPanelLayout = new QVBoxLayout(contentPanel);
    contentPanelLayout->setContentsMargins(0, 0, 0, 0);
    contentPanelLayout->setSpacing(0);

    pages_ = new GalleryContentView(contentPanel);
    contentPanelLayout->addWidget(pages_);
    auto* contentTitleBar = pages_->titleBar();
    auto* contentTitleLayout = pages_->titleBarLayout();
    contentTitleLayout->setContentsMargins(18, 0, 14, 0);
    contentTitleLayout->addStretch();

    const auto addGlassSetting = [this, contentTitleLayout](const QString& objectName,
                                                            const QString& labelText) {
        auto* surface = new vkui::VLiquidGlassSurface(pages_);
        surface->setObjectName(objectName);
        surface->setBackdrop(pages_->liquidGlassBackdrop());
        surface->setGlassStyle(vkui::VLiquidGlassStyle::regular());
        surface->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        auto* layout = new QHBoxLayout(surface);
        layout->setContentsMargins(10, 3, 5, 3);
        layout->setSpacing(4);
        layout->addWidget(new QLabel(labelText, surface));
        contentTitleLayout->addWidget(surface);
        return std::pair{surface, layout};
    };

    const auto [appearanceSurface, appearanceLayout] =
        addGlassSetting(QStringLiteral("galleryAppearanceGlass"), tr("Appearance"));
    appearanceBox_ = new vkui::VCombobox(appearanceSurface);
    appearanceBox_->addItem(tr("System"), static_cast<int>(vkui::VkAppearance::Auto));
    appearanceBox_->addItem(tr("Light"), static_cast<int>(vkui::VkAppearance::Light));
    appearanceBox_->addItem(tr("Dark"), static_cast<int>(vkui::VkAppearance::Dark));
    appearanceBox_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    const int appearanceIndex =
        appearanceBox_->findData(static_cast<int>(vkui::VkThemeManager::instance()->appearance()));
    appearanceBox_->setCurrentIndex(qMax(0, appearanceIndex));
    appearanceLayout->addWidget(appearanceBox_);

    contentTitleLayout->addSpacing(8);
    const auto [languageSurface, languageLayout] =
        addGlassSetting(QStringLiteral("galleryLanguageGlass"), tr("Language"));
    languageBox_ = new vkui::VCombobox(languageSurface);
    languageBox_->addItem(tr("System"), static_cast<int>(Language::System));
    languageBox_->addItem(QStringLiteral("English"), static_cast<int>(Language::English));
    languageBox_->addItem(QStringLiteral("简体中文"),
                          static_cast<int>(Language::SimplifiedChinese));
    languageBox_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    languageBox_->setCurrentIndex(qMax(0, languageBox_->findData(static_cast<int>(language_))));
    languageLayout->addWidget(languageBox_);

    QList<QWidget*> interactiveWidgets{appearanceBox_, languageBox_};
#ifndef Q_OS_MAC
    // DWM owns the right-aligned caption controls and their hit testing.
    constexpr int kCaptionButtonReserve = 144;
    contentTitleLayout->addSpacing(kCaptionButtonReserve);
#endif
    pages_->addPage(new StandardWidgetsPage(pages_));
    pages_->addPage(new SwitchPage(pages_));
    pages_->addPage(new SegmentedControlPage(pages_));
    pages_->addPage(new PopoverPage(pages_));
    pages_->addPage(new ThemePage(pages_));
    pages_->addPage(new IconsPage(pages_));
    pages_->addPage(new MotionPage(pages_));
    pages_->addPage(new LocalizationPage(pages_));
    auto* windowPage = new WindowPage(windowAgent_, pages_);
    connect(windowPage, &WindowPage::preferencesRequested, this,
            &GalleryWindow::preferencesRequested);
    pages_->addPage(windowPage);
    splitter_->addWidget(contentPanel);
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    splitter_->setSizes({navigationWidth_, qMax(520, width() - navigationWidth_)});

    connect(navigation_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) {
                bool validPage = false;
                const int pageIndex = current.data(PageIndexRole).toInt(&validPage);
                if (validPage && pageIndex >= 0 && pageIndex < pages_->count()) {
                    currentPage_ = pageIndex;
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
    QModelIndex currentNavigationIndex;
    for (int groupRow = 0; groupRow < navigationModel_->rowCount(); ++groupRow) {
        const QModelIndex groupIndex = navigationModel_->index(groupRow, 0);
        for (int pageRow = 0; pageRow < navigationModel_->rowCount(groupIndex); ++pageRow) {
            const QModelIndex pageIndex = navigationModel_->index(pageRow, 0, groupIndex);
            if (pageIndex.data(PageIndexRole).toInt() == currentPage_) {
                currentNavigationIndex = pageIndex;
                break;
            }
        }
        if (currentNavigationIndex.isValid()) {
            break;
        }
    }
    navigation_->setCurrentIndex(currentNavigationIndex);
    pages_->setCurrentIndex(currentPage_);
    layout()->addWidget(central_);

    interactiveWidgets.append(splitter_->handle(1));
    registerWindowChrome(navigationTitleBar, contentTitleBar, interactiveWidgets);
}

void GalleryWindow::registerWindowChrome(QWidget* navigationTitleBar, QWidget* contentTitleBar,
                                         const QList<QWidget*>& interactiveWidgets) {
    const bool navigationAdded = windowAgent_.addTitleBar(navigationTitleBar);
    const bool contentAdded = windowAgent_.addTitleBar(contentTitleBar);
    Q_ASSERT(navigationAdded);
    Q_ASSERT(contentAdded);
    for (QWidget* widget : interactiveWidgets) {
        if (widget == nullptr) {
            continue;
        }
        // Hit-test exclusions are window-scoped. This keeps full-height
        // splitter handles interactive across every registered title bar.
        const bool registered = windowAgent_.setHitTestVisible(widget, true);
        Q_ASSERT(registered);
    }
}

void GalleryWindow::updateWindowTitle() {
    setWindowTitle(tr("vkui Gallery"));
}
