// SPDX-License-Identifier: MIT

#include "GalleryPreferencesWindow.h"

#include <QCheckBox>
#include <QCursor>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QScreen>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <QtMath>
#include <vkui/core/VkAppearance.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/VCombobox.h>

namespace {

constexpr int kTitleBarHeight = 44;
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
constexpr int kMacCloseButtonReserve = 24;
constexpr QPoint kMacTrafficLightOrigin{18, 15};
#else
constexpr int kSystemButtonReserve = 56;
#endif
constexpr QSizeF kHostSizeFraction{0.55, 0.58};

} // namespace

GalleryPreferencesWindow::GalleryPreferencesWindow()
    : QWidget(nullptr, Qt::Window), windowAgent_(*this) {
    setObjectName(QStringLiteral("GalleryPreferencesWindow"));
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_QuitOnClose, false);
    setMinimumSize(480, 320);
    setBackgroundRole(QPalette::Window);
    setAutoFillBackground(true);

    buildUi();
    configureWindowChrome();
    retranslateUi();
}

void GalleryPreferencesWindow::showForHost(const QWidget* host) {
    if (!isVisible()) {
        positionForHost(host);
    }
    showNormal();
    windowAgent_.raiseWindow();
}

void GalleryPreferencesWindow::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event != nullptr && event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void GalleryPreferencesWindow::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    titleBar_ = new QWidget(this);
    titleBar_->setObjectName(QStringLiteral("GalleryPreferencesTitleBar"));
    titleBar_->setFixedHeight(kTitleBarHeight);
    titleBar_->setAutoFillBackground(false);
    titleBar_->setAttribute(Qt::WA_StyledBackground, false);
    auto* titleLayout = new QHBoxLayout(titleBar_);
    titleLayout->setContentsMargins(18, 0, 10, 0);
    titleLayout->setSpacing(8);

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    auto* nativeButtonReserve = new QWidget(titleBar_);
    nativeButtonReserve->setFixedWidth(kMacCloseButtonReserve);
    titleLayout->addWidget(nativeButtonReserve);
#endif

    titleLabel_ = new QLabel(titleBar_);
    titleLabel_->setObjectName(QStringLiteral("GalleryPreferencesTitleLabel"));
    QFont titleFont = titleLabel_->font();
    titleFont.setWeight(QFont::DemiBold);
    titleLabel_->setFont(titleFont);
    titleLayout->addWidget(titleLabel_, 1, Qt::AlignVCenter);

#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
    auto* nativeButtonReserve = new QWidget(titleBar_);
    nativeButtonReserve->setFixedWidth(kSystemButtonReserve);
    titleLayout->addWidget(nativeButtonReserve);
#endif
    root->addWidget(titleBar_);

    content_ = new QWidget(this);
    content_->setObjectName(QStringLiteral("GalleryPreferencesContent"));
    auto* contentLayout = new QVBoxLayout(content_);
    contentLayout->setContentsMargins(18, 0, 18, 18);
    contentLayout->setSpacing(14);

    generalGroup_ = new QGroupBox(content_);
    auto* form = new QFormLayout(generalGroup_);

    appearanceLabel_ = new QLabel(generalGroup_);
    appearanceBox_ = new vkui::VCombobox(generalGroup_);
    appearanceBox_->addItem({}, static_cast<int>(vkui::VkAppearance::Auto));
    appearanceBox_->addItem({}, static_cast<int>(vkui::VkAppearance::Light));
    appearanceBox_->addItem({}, static_cast<int>(vkui::VkAppearance::Dark));
    appearanceBox_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    auto* themeManager = vkui::VkThemeManager::instance();
    appearanceBox_->setCurrentIndex(
        qMax(0, appearanceBox_->findData(static_cast<int>(themeManager->appearance()))));
    form->addRow(appearanceLabel_, appearanceBox_);

    animationsBox_ = new QCheckBox(generalGroup_);
    animationsBox_->setChecked(themeManager->animationsEnabled());
    form->addRow(animationsBox_);

    scaleLabel_ = new QLabel(generalGroup_);
    auto* scaleControl = new QWidget(generalGroup_);
    auto* scaleLayout = new QHBoxLayout(scaleControl);
    scaleLayout->setContentsMargins(0, 0, 0, 0);
    scaleSlider_ = new QSlider(Qt::Horizontal, scaleControl);
    scaleSlider_->setRange(80, 140);
    scaleSlider_->setValue(100);
    scaleValue_ = new QLabel(scaleControl);
    scaleValue_->setMinimumWidth(46);
    scaleLayout->addWidget(scaleSlider_, 1);
    scaleLayout->addWidget(scaleValue_);
    form->addRow(scaleLabel_, scaleControl);
    contentLayout->addWidget(generalGroup_);

    noteLabel_ = new QLabel(content_);
    noteLabel_->setWordWrap(true);
    contentLayout->addWidget(noteLabel_);
    contentLayout->addStretch();
    root->addWidget(content_, 1);

    connect(appearanceBox_, &QComboBox::currentIndexChanged, this,
            [this, themeManager](const int index) {
                themeManager->setAppearance(
                    static_cast<vkui::VkAppearance>(appearanceBox_->itemData(index).toInt()));
            });
    connect(themeManager, &vkui::VkThemeManager::appearanceChanged, this,
            [this](const vkui::VkAppearance value) {
                const QSignalBlocker blocker(appearanceBox_);
                appearanceBox_->setCurrentIndex(
                    qMax(0, appearanceBox_->findData(static_cast<int>(value))));
            });
    connect(animationsBox_, &QCheckBox::toggled, themeManager,
            &vkui::VkThemeManager::setAnimationsEnabled);
    connect(themeManager, &vkui::VkThemeManager::animationsEnabledChanged, this,
            [this](const bool enabled) {
                const QSignalBlocker blocker(animationsBox_);
                animationsBox_->setChecked(enabled);
            });
    connect(scaleSlider_, &QSlider::valueChanged, this,
            [this](const int value) { scaleValue_->setText(tr("%1%").arg(value)); });
}

void GalleryPreferencesWindow::configureWindowChrome() {
    windowAgent_.setResizable(true);
    windowAgent_.setSystemButtons(vkui::VSystemButton::Close);
    const bool titleBarAdded = windowAgent_.addTitleBar(titleBar_);
    Q_ASSERT(titleBarAdded);
    Q_UNUSED(titleBarAdded)
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    windowAgent_.setTrafficLightOrigin(kMacTrafficLightOrigin);
#endif
}

void GalleryPreferencesWindow::positionForHost(const QWidget* host) {
    QRect available;
    QPoint placementCenter;
    if (host != nullptr) {
        available = host->geometry();
        placementCenter = host->frameGeometry().center();
    } else if (const QScreen* pointerScreen = QGuiApplication::screenAt(QCursor::pos())) {
        available = pointerScreen->availableGeometry();
        placementCenter = available.center();
    } else if (const QScreen* primaryScreen = QGuiApplication::primaryScreen()) {
        available = primaryScreen->availableGeometry();
        placementCenter = available.center();
    }
    if (!available.isValid()) {
        return;
    }

    const QSize target(qRound(available.width() * kHostSizeFraction.width()),
                       qRound(available.height() * kHostSizeFraction.height()));
    resize(target.expandedTo(minimumSize()).boundedTo(maximumSize()));
    const QPoint frameCenterOffset = frameGeometry().center() - frameGeometry().topLeft();
    move(placementCenter - frameCenterOffset);
}

void GalleryPreferencesWindow::retranslateUi() {
    setWindowTitle(tr("Preferences"));
    titleLabel_->setText(windowTitle());
    generalGroup_->setTitle(tr("General"));
    appearanceLabel_->setText(tr("Appearance"));
    appearanceBox_->setItemText(0, tr("System"));
    appearanceBox_->setItemText(1, tr("Light"));
    appearanceBox_->setItemText(2, tr("Dark"));
    animationsBox_->setText(tr("Enable interface animations"));
    scaleLabel_->setText(tr("Text preview scale"));
    scaleValue_->setText(tr("%1%").arg(scaleSlider_->value()));
    noteLabel_->setText(
        tr("The Gallery application owns this lazily created window. Closing it releases the "
           "instance; reopening Preferences creates a fresh window with the current settings."));
}
