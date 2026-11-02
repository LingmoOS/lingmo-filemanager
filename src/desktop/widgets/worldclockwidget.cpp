// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "desktop/widgets/worldclockwidget.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <sstream>

#include "base/i18n.h"
#include "base/paths.h"
#include "base/settings.h"
#include "core/config.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/theme.h"

using Lingmo::i18n::tr;

namespace fm {

namespace {

// Default zones shown when the user has not configured any.
constexpr const char* kDefaultZones[] = {
    "UTC",
    "America/New_York",
    "Europe/London",
    "Asia/Shanghai",
};

// Convert an Olson zone name to a short city label, by taking the last
// segment and replacing underscores with spaces.
std::string zoneLabel(const std::string& zone)
{
    const size_t slash = zone.rfind('/');
    std::string city = (slash == std::string::npos) ? zone : zone.substr(slash + 1);
    std::replace(city.begin(), city.end(), '_', ' ');
    return city;
}

// Format the wall-clock time for the given zone as HH:MM.
std::string zoneTime(const std::string& zone)
{
    const std::time_t now = std::time(nullptr);
    const char* prevTz = std::getenv("TZ");

    std::string tzCopy;
    if (prevTz)
        tzCopy = prevTz;

    setenv("TZ", zone.c_str(), 1);
    tzset();

    std::tm local;
    std::memset(&local, 0, sizeof(local));
    localtime_r(&now, &local);

    // Restore the previous TZ so we don't leak the zone into other code.
    if (prevTz)
        setenv("TZ", tzCopy.c_str(), 1);
    else
        unsetenv("TZ");
    tzset();

    std::ostringstream out;
    out << std::put_time(&local, "%H:%M");
    return out.str();
}

} // namespace

WorldClockWidget::WorldClockWidget(WidgetLayer& layer, WidgetSize size)
    : DesktopWidget(layer, WidgetKind::WorldClock, size)
{
    setName("world-clock-widget");
    for (int i = 0; i < 4; ++i) {
        m_timeLabels[i] = addChild<ui::Label>();
        m_timeLabels[i]->setElide(ui::Label::Elide::Right);
        m_cityLabels[i] = addChild<ui::Label>();
        m_cityLabels[i]->setElide(ui::Label::Elide::Right);
        m_cityLabels[i]->setColor(ui::ColorRecipe(ui::Theme::current().text.withAlpha(0.55)));
    }

    m_timer.setInterval(std::chrono::seconds(1));
    m_timer.setSingleShot(false);
    m_timer.onTimeout = [this] { refresh(); };
    m_timer.start();

    loadZones();
    refresh();
}

WorldClockWidget::~WorldClockWidget()
{
    m_timer.stop();
}

void WorldClockWidget::loadZones()
{
    m_zoneCount = 0;
    const base::Settings settings(settingsFile());
    bool any = false;
    for (int i = 0; i < 4; ++i) {
        std::string key = "WorldClock/Zone";
        key += std::to_string(i + 1);
        const std::string zone = settings.stringValue(key);
        if (!zone.empty()) {
            m_zones[i] = zone;
            m_cityLabels[i]->setText(zoneLabel(zone));
            ++m_zoneCount;
            any = true;
        } else {
            m_zones[i].clear();
            m_cityLabels[i]->setText(std::string());
        }
    }

    if (!any) {
        for (int i = 0; i < 4; ++i) {
            m_zones[i] = kDefaultZones[i];
            m_cityLabels[i]->setText(zoneLabel(kDefaultZones[i]));
        }
        m_zoneCount = 4;
    }
}

std::string WorldClockWidget::settingsFile()
{
    return base::paths::lingmoConfig("filemanager");
}

void WorldClockWidget::refresh()
{
    for (int i = 0; i < m_zoneCount; ++i) {
        if (m_zones[i].empty())
            continue;
        m_timeLabels[i]->setText(zoneTime(m_zones[i]));
    }
    requestLayout();
    requestPaint();
}

void WorldClockWidget::layoutContent()
{
    const ui::RectF content = contentRect();
    if (content.width <= 0 || content.height <= 0)
        return;

    const bool small = size() == WidgetSize::Small;
    const bool large = size() == WidgetSize::Large;

    double timeSize = 24;
    double citySize = 11;
    if (small) {
        timeSize = 36;
        citySize = 11;
    } else if (large) {
        timeSize = 28;
        citySize = 12;
    }

    for (int i = 0; i < 4; ++i) {
        m_timeLabels[i]->setFontSize(timeSize);
        m_timeLabels[i]->setWeight(600);
        m_cityLabels[i]->setFontSize(citySize);
        m_cityLabels[i]->setWeight(500);
        m_cityLabels[i]->setColor(ui::ColorRecipe(ui::Theme::current().text.withAlpha(0.55)));
    }

    if (small) {
        // Only the first zone.
        const double timeH = std::max(20.0, m_timeLabels[0]->sizeHint().height);
        const double cityH = std::max(14.0, m_cityLabels[0]->sizeHint().height);
        m_timeLabels[0]->setGeometry({content.x, content.y, content.width,
                                      std::max(0.0, content.height - cityH - 4)});
        m_cityLabels[0]->setGeometry({content.x, content.bottom() - cityH, content.width, cityH});
        for (int i = 1; i < 4; ++i) {
            m_timeLabels[i]->setGeometry({0, 0, 0, 0});
            m_cityLabels[i]->setGeometry({0, 0, 0, 0});
        }
    } else if (large) {
        // 2×2 grid.
        const double colW = (content.width - 16) / 2.0;
        const double rowH = content.height / 2.0;
        for (int i = 0; i < 4; ++i) {
            const int col = i % 2;
            const int row = i / 2;
            const double x = content.x + col * (colW + 16);
            const double y = content.y + row * rowH;
            const double timeH = std::max(20.0, m_timeLabels[i]->sizeHint().height);
            const double cityH = std::max(14.0, m_cityLabels[i]->sizeHint().height);
            m_timeLabels[i]->setGeometry({x, y, colW, std::max(0.0, rowH - cityH - 4)});
            m_cityLabels[i]->setGeometry({x, y + rowH - cityH, colW, cityH});
        }
    } else {
        // Medium: 2 side by side.
        const double colW = (content.width - 16) / 2.0;
        for (int i = 0; i < 2; ++i) {
            const double x = content.x + i * (colW + 16);
            const double timeH = std::max(20.0, m_timeLabels[i]->sizeHint().height);
            const double cityH = std::max(14.0, m_cityLabels[i]->sizeHint().height);
            m_timeLabels[i]->setGeometry({x, content.y, colW,
                                          std::max(0.0, content.height - cityH - 4)});
            m_cityLabels[i]->setGeometry({x, content.bottom() - cityH, colW, cityH});
        }
        for (int i = 2; i < 4; ++i) {
            m_timeLabels[i]->setGeometry({0, 0, 0, 0});
            m_cityLabels[i]->setGeometry({0, 0, 0, 0});
        }
    }
}

void WorldClockWidget::paintContent(ui::Painter& painter)
{
    const ui::RectF content = contentRect();
    if (content.width <= 0 || content.height <= 0)
        return;

    const ui::Theme& theme = ui::Theme::current();
    const ui::Color ink = theme.text.withAlpha(0.14);

    if (size() == WidgetSize::Large) {
        // Vertical and horizontal separators for the 2×2 grid.
        const double colW = (content.width - 16) / 2.0;
        const double midX = content.x + colW + 8;
        painter.drawLine({midX, content.y}, {midX, content.bottom()}, ink, 1);
        const double midY = content.y + content.height / 2.0;
        painter.drawLine({content.x, midY}, {content.right(), midY}, ink, 1);
    } else if (size() == WidgetSize::Medium) {
        // A single vertical divider between the two zones.
        const double colW = (content.width - 16) / 2.0;
        const double midX = content.x + colW + 8;
        painter.drawLine({midX, content.y}, {midX, content.bottom()}, ink, 1);
    }
}

void WorldClockWidget::retranslateContent()
{
    loadZones();
    refresh();
}

} // namespace fm
