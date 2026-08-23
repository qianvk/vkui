// SPDX-License-Identifier: MIT

#include "GalleryApplicationController.h"

#include "GalleryPreferencesWindow.h"
#include "GalleryWindow.h"

GalleryApplicationController::GalleryApplicationController()
    : galleryWindow_(std::make_unique<GalleryWindow>()) {
    connect(galleryWindow_.get(), &GalleryWindow::preferencesRequested, this,
            &GalleryApplicationController::showPreferencesWindow);
}

GalleryApplicationController::~GalleryApplicationController() {
    // A live top-level Preferences window has no QWidget parent. Destroy it
    // while QApplication and the native platform integration still exist.
    delete preferencesWindow_.data();
}

void GalleryApplicationController::showMainWindow() {
    galleryWindow_->resize(1120, 760);
    galleryWindow_->show();
}

void GalleryApplicationController::showPreferencesWindow() {
    if (preferencesWindow_ == nullptr) {
        preferencesWindow_ = new GalleryPreferencesWindow;
    }
    preferencesWindow_->showForHost(galleryWindow_.get());
}
