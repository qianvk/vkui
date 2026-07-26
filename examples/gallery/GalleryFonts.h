// SPDX-License-Identifier: MIT

#pragma once

#include <QFont>

namespace gallery {

/** Returns Fira Code with a platform fixed-font fallback. */
QFont codeFont(qreal pointSize = -1.0);

} // namespace gallery
