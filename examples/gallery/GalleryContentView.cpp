// SPDX-License-Identifier: MIT

#include "GalleryContentView.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPalette>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

GalleryContentView::GalleryContentView(QWidget* parent) : QWidget(parent) {
    auto* overlayLayout = new QGridLayout(this);
    overlayLayout->setContentsMargins(0, 0, 0, 0);
    overlayLayout->setSpacing(0);

    titleBar_ = new QWidget(this);
    titleBar_->setObjectName(QStringLiteral("galleryContentTitleBar"));
    titleBar_->setFixedHeight(TitleBarHeight);
    titleBar_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // This widget is only the logical native hit-test rectangle. It never
    // paints a surface of its own.
    titleBar_->setAutoFillBackground(false);
    titleBar_->setAttribute(Qt::WA_StyledBackground, false);
    overlayLayout->addWidget(titleBar_, 0, 0, Qt::AlignTop);

    pages_ = new QStackedWidget(this);
    pages_->setObjectName(QStringLiteral("galleryPages"));
    overlayLayout->addWidget(pages_, 0, 0);

    // The logical title-bar widget stays behind page painting. Controls are
    // direct children of this view and are positioned by a non-painting layout.
    // This produces real sibling visibility instead of propagated parent color.
    titleBar_->lower();
    titleBarLayout_ = new QHBoxLayout;
    titleBarLayout_->addStrut(TitleBarHeight);
    overlayLayout->addLayout(titleBarLayout_, 0, 0, Qt::AlignTop);
}

QWidget* GalleryContentView::titleBar() const noexcept {
    return titleBar_;
}

QHBoxLayout* GalleryContentView::titleBarLayout() const noexcept {
    return titleBarLayout_;
}

void GalleryContentView::addPage(QWidget* page) {
    if (page == nullptr) {
        return;
    }

    auto* scrollArea = new QScrollArea(pages_);
    scrollArea->setObjectName(QStringLiteral("galleryPageScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setBackgroundRole(QPalette::Base);
    scrollArea->viewport()->setBackgroundRole(QPalette::Base);
    scrollArea->viewport()->setAutoFillBackground(true);

    auto* canvas = new QWidget(scrollArea);
    canvas->setObjectName(QStringLiteral("galleryPageCanvas"));
    canvas->setBackgroundRole(QPalette::Base);
    canvas->setAutoFillBackground(true);
    auto* canvasLayout = new QVBoxLayout(canvas);
    // This inset belongs to the scrollable canvas, not the viewport. It is
    // present at rest and naturally scrolls out beneath the title-bar overlay.
    canvasLayout->setContentsMargins(0, TitleBarHeight, 0, 0);
    canvasLayout->setSpacing(0);
    canvasLayout->addWidget(page);

    scrollArea->setWidget(canvas);
    pages_->addWidget(scrollArea);
    // Initial focus assignment can ask QScrollArea to reveal a child before
    // the top-level window is shown. Restore the designed resting inset once.
    QTimer::singleShot(0, scrollArea, [scrollArea] {
        scrollArea->verticalScrollBar()->setValue(
            scrollArea->verticalScrollBar()->minimum());
    });
}

int GalleryContentView::count() const {
    return pages_->count();
}

int GalleryContentView::currentIndex() const {
    return pages_->currentIndex();
}

void GalleryContentView::setCurrentIndex(const int index) {
    pages_->setCurrentIndex(index);
}

QScrollArea* GalleryContentView::pageScrollArea(const int index) const {
    return qobject_cast<QScrollArea*>(pages_->widget(index));
}
