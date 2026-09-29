#pragma once

#include <QString>

class QWidget;

namespace Toast {

// Short message shown bottom-centre over `parent` (as a child, so it follows the window and
// never takes focus), removed after `durationMs`.
void show(QWidget* parent, const QString& message, int durationMs = 3000,
          const QString& background = "#1a7f37");

} // namespace Toast
