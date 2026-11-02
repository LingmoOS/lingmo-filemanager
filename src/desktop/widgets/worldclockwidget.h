// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <ctime>
#include <string>
#include <vector>

#include "base/timer.h"
#include "desktop/widgets/widget.h"

namespace Lingmo::ui {
class Label;
}

namespace fm {

// World clock: shows up to four time zones side by side, one minute at a time.
// Small shows the first zone only; Medium shows two; Large shows a 2×2 grid.
class WorldClockWidget : public DesktopWidget
{
public:
    WorldClockWidget(WidgetLayer& layer, WidgetSize size);
    ~WorldClockWidget() override;

protected:
    void layoutContent() override;
    void paintContent(ui::Painter& painter) override;
    void retranslateContent() override;

private:
    void refresh();
    void loadZones();
    static std::string settingsFile();

    // Up to 4 zone slots, each has a time label and a city label.
    ui::Label* m_timeLabels[4] = {};
    ui::Label* m_cityLabels[4] = {};
    base::Timer m_timer;
    std::string m_zones[4];
    int m_zoneCount = 0;
};

} // namespace fm
