// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "desktop/widgets/widgetlayer.h"

#include <algorithm>
#include <cmath>

#include "base/event_loop.h"
#include "base/i18n.h"
#include "desktop/widgets/widget.h"
#include "ui/menu.h"
#include "ui/path.h"
#include "ui/window.h"

using Lingmo::i18n::tr;

namespace fm {

namespace {

constexpr double kSnap = 8;
constexpr double kMargin = 8;

bool intersects(const ui::RectF& a, const ui::RectF& b)
{
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

ui::RectF expanded(const ui::RectF& rect, double amount)
{
    return {rect.x - amount, rect.y - amount, rect.width + amount * 2, rect.height + amount * 2};
}

} // namespace

WidgetLayer::WidgetLayer()
{
    setName("desktop-widgets");
    setClipsChildren(false);
}

WidgetLayer::~WidgetLayer()
{
    *m_alive = false;
    m_menu.reset();
}

void WidgetLayer::setDesktopWindow(ui::Window* window)
{
    m_window = window;
    if (m_window && m_workArea.width <= 0 && m_window->workArea().width > 0)
        m_workArea = m_window->workArea();
    updateBlur();
}

void WidgetLayer::setWorkArea(const ui::RectF& area)
{
    if (area == m_workArea)
        return;
    m_workArea = area;
    layout();
    updateBlur();
}

void WidgetLayer::setEditing(bool editing)
{
    if (m_editing == editing)
        return;
    m_editing = editing;
    for (DesktopWidget* widget : m_widgets)
        widget->setEditing(editing);
}

ui::PointF WidgetLayer::clamped(ui::PointF position, ui::SizeF size) const
{
    if (m_workArea.width <= 0 || m_workArea.height <= 0)
        return position;
    const double maxX = std::max(m_workArea.x, m_workArea.right() - size.width);
    const double maxY = std::max(m_workArea.y, m_workArea.bottom() - size.height);
    position.x = std::clamp(position.x, m_workArea.x, maxX);
    position.y = std::clamp(position.y, m_workArea.y, maxY);
    return position;
}

ui::PointF WidgetLayer::snapped(ui::PointF position) const
{
    return {std::round(position.x / kSnap) * kSnap, std::round(position.y / kSnap) * kSnap};
}

bool WidgetLayer::overlaps(const ui::RectF& rect) const
{
    for (const DesktopWidget* widget : m_widgets) {
        if (intersects(expanded(widget->geometry(), kWidgetGap / 2), expanded(rect, 1)))
            return true;
    }
    return false;
}

ui::PointF WidgetLayer::freePosition(ui::SizeF size) const
{
    const ui::RectF area = m_workArea.width > 0 ? m_workArea : localRect();
    const double x = area.x + kMargin;
    const double y = area.y + kMargin;
    const double usableWidth = std::max(size.width, area.width - kMargin * 2);
    const int columns = std::max(1, int((usableWidth + kWidgetGap) / (size.width + kWidgetGap)));
    const int rows = std::max(1, int((std::max(size.height, area.height - kMargin * 2) + kWidgetGap)
                                     / (size.height + kWidgetGap)));
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const ui::RectF candidate {x + column * (size.width + kWidgetGap),
                                       y + row * (size.height + kWidgetGap), size.width, size.height};
            if (!overlaps(candidate))
                return candidate.topLeft();
        }
    }
    // Every slot is taken: let the cards overlap rather than not appear.
    return {x, y};
}

DesktopWidget* WidgetLayer::create(const WidgetPlacement& placement)
{
    std::unique_ptr<DesktopWidget> widget =
            createDesktopWidget(*this, placement.kind, placement.size);
    if (!widget)
        return nullptr;
    const ui::SizeF size = widgetSizeSpec(placement.size);
    const ui::PointF position = clamped({placement.x, placement.y}, size);
    widget->setGeometry({position.x, position.y, size.width, size.height});
    DesktopWidget* raw = widget.get();
    addChild(std::move(widget));
    m_widgets.push_back(raw);
    return raw;
}

void WidgetLayer::addWidget(WidgetKind kind, WidgetSize size)
{
    const ui::SizeF spec = widgetSizeSpec(size);
    const ui::PointF position = freePosition(spec);
    DesktopWidget* widget = create({kind, size, position.x, position.y});
    if (!widget)
        return;
    widget->setEditing(m_editing);
    updateBlur();
    if (onChanged)
        onChanged();
}

void WidgetLayer::removeWidget(DesktopWidget* widget)
{
    const auto found = std::find(m_widgets.begin(), m_widgets.end(), widget);
    if (found == m_widgets.end())
        return;
    m_widgets.erase(found);
    // Destroyed here, after the layer no longer lists it.
    takeChild(widget);
    updateBlur();
    if (onChanged)
        onChanged();
}

void WidgetLayer::removeWidgetDeferred(DesktopWidget* widget)
{
    base::EventLoop* loop = base::EventLoop::current();
    if (!loop) {
        removeWidget(widget);
        return;
    }
    std::shared_ptr<bool> alive = m_alive;
    loop->post([this, widget, alive] {
        if (*alive)
            removeWidget(widget);
    });
}

void WidgetLayer::setPlacements(const std::vector<WidgetPlacement>& placements)
{
    for (DesktopWidget* widget : m_widgets)
        takeChild(widget);
    m_widgets.clear();
    for (const WidgetPlacement& placement : placements)
        create(placement);
    for (DesktopWidget* widget : m_widgets)
        widget->setEditing(m_editing);
    updateBlur();
}

std::vector<WidgetPlacement> WidgetLayer::placements() const
{
    std::vector<WidgetPlacement> placements;
    placements.reserve(m_widgets.size());
    for (const DesktopWidget* widget : m_widgets)
        placements.push_back(widget->placement());
    return placements;
}

void WidgetLayer::layout()
{
    for (DesktopWidget* widget : m_widgets) {
        const ui::RectF card = widget->geometry();
        const ui::PointF position = clamped(card.topLeft(), card.size());
        if (position != card.topLeft())
            widget->setGeometry({position.x, position.y, card.width, card.height});
    }
}

void WidgetLayer::setWidgetSize(DesktopWidget* widget, WidgetSize size)
{
    const ui::SizeF spec = widgetSizeSpec(size);
    const ui::PointF position = clamped(widget->geometry().topLeft(), spec);
    widget->setSize(size);
    widget->setGeometry({position.x, position.y, spec.width, spec.height});
    updateBlur();
    if (onChanged)
        onChanged();
}

void WidgetLayer::moveWidget(DesktopWidget* widget, ui::PointF position, bool finished)
{
    const ui::RectF card = widget->geometry();
    ui::PointF where = clamped(position, card.size());
    if (finished)
        where = clamped(snapped(where), card.size());
    widget->setGeometry({where.x, where.y, card.width, card.height});
    updateBlur();
    if (finished && onChanged)
        onChanged();
}

void WidgetLayer::showWidgetMenu(DesktopWidget* widget, ui::PointF windowPosition)
{
    if (!m_window)
        return;
    m_menu = std::make_unique<ui::Menu>();
    ui::Menu* menu = m_menu.get();
    menu->addItem(tr("Remove Widget"), [this, widget] { removeWidget(widget); });
    ui::Menu* sizes = menu->addSubmenu(tr("Widget Size"));
    for (WidgetSize size : {WidgetSize::Small, WidgetSize::Medium, WidgetSize::Large}) {
        // tr() on literals, so the catalogues find all three names.
        const char *label = size == WidgetSize::Small ? tr("Small")
                : size == WidgetSize::Medium          ? tr("Medium")
                                                       : tr("Large");
        const size_t index = sizes->addItem(label, [this, widget, size] { setWidgetSize(widget, size); });
        sizes->setItemChecked(index, widget->size() == size);
    }
    menu->addSeparator();
    menu->addItem(tr("Edit Widgets…"), [this] {
        if (onEditWidgets)
            onEditWidgets();
    });
    m_menu->popup(m_window, windowPosition);
}

void WidgetLayer::updateBlur()
{
    if (!m_window)
        return;
    if (m_widgets.empty() || !m_window->backgroundGlassAvailable()) {
        m_window->setBackgroundBlur({}, 0);
        m_window->setBackgroundBlurShape(ui::Path());
        return;
    }
    ui::Path shape;
    ui::RectF bounds = m_widgets.front()->blurRect();
    for (const DesktopWidget* widget : m_widgets) {
        const ui::RectF card = widget->blurRect();
        shape.addRoundedRect(card, kWidgetRadius, kWidgetRadius);
        bounds.x = std::min(bounds.x, card.x);
        bounds.y = std::min(bounds.y, card.y);
        bounds.width = std::max(bounds.right(), card.right()) - bounds.x;
        bounds.height = std::max(bounds.bottom(), card.bottom()) - bounds.y;
    }
    // Blur only the cards' neighbourhood: a full-screen blur would capture and
    // soften the whole desktop every frame for nothing.
    bounds = bounds.adjusted(-48, -48, 48, 48);
    const ui::RectF area = localRect();
    const ui::PointF origin = mapToWindow(area.topLeft());
    const ui::RectF windowArea {origin.x, origin.y, area.width, area.height};
    bounds.x = std::clamp(bounds.x, windowArea.x, windowArea.right());
    bounds.y = std::clamp(bounds.y, windowArea.y, windowArea.bottom());
    bounds.width = std::min(bounds.right(), windowArea.right()) - bounds.x;
    bounds.height = std::min(bounds.bottom(), windowArea.bottom()) - bounds.y;
    if (bounds.width <= 0 || bounds.height <= 0) {
        m_window->setBackgroundBlur({}, 0);
        m_window->setBackgroundBlurShape(ui::Path());
        return;
    }
    m_window->setBackgroundBlur(bounds, 0);
    m_window->setBackgroundBlurShape(shape);
}

void WidgetLayer::retranslate()
{
    for (DesktopWidget* widget : m_widgets)
        widget->retranslate();
}

} // namespace fm
