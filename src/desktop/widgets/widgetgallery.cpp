// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "desktop/widgets/widgetgallery.h"

#include <algorithm>
#include <cmath>

#include "base/i18n.h"
#include "ui/button.h"
#include "ui/keys.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/theme.h"

using Lingmo::i18n::tr;

namespace fm {

namespace {

void paintPreview(ui::Painter& painter, WidgetKind kind, const ui::RectF& rect)
{
    const ui::Theme& theme = ui::Theme::current();
    const ui::Color quiet = theme.text.withAlpha(0.18);
    const ui::Color accent = theme.highlight;
    switch (kind) {
    case WidgetKind::Clock: {
        painter.fillRoundedRect({rect.x, rect.y + rect.height * 0.16, rect.width * 0.78, 12},
                                theme.smallRadius, quiet);
        painter.fillRoundedRect({rect.x, rect.y + rect.height * 0.42, rect.width * 0.52, 8},
                                theme.smallRadius, quiet);
        return;
    }
    case WidgetKind::Calendar: {
        const double cell = rect.width / 8;
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 7; ++column) {
                const double size = std::max(2.0, cell * 0.4);
                painter.fillRoundedRect({rect.x + cell * 0.4 + column * cell,
                                         rect.y + cell * 0.7 + row * (cell * 0.8), size, size},
                                        size / 2, row == 0 && column == 2 ? accent : quiet);
            }
        }
        return;
    }
    case WidgetKind::System: {
        for (int i = 0; i < 2; ++i) {
            const double y = rect.y + rect.height * (0.28 + i * 0.32);
            painter.fillRoundedRect({rect.x, y, rect.width, 6}, 3, quiet);
            painter.fillRoundedRect({rect.x, y, rect.width * (i == 0 ? 0.62 : 0.38), 6}, 3, accent);
        }
        return;
    }
    }
}

} // namespace

class WidgetGallery::PreviewCard final : public ui::View
{
public:
    explicit PreviewCard(WidgetKind kind)
        : m_kind(kind)
    {
        setCursorShape(ui::CursorShape::Pointer);
        m_title = addChild<ui::Label>();
        m_title->setTextAlignment(ui::Label::TextAlignment::Center);
        m_title->setFontSize(12);
        m_title->setWeight(600);
        retranslate();
    }

    bool acceptsPointer() const override { return true; }

    void retranslate()
    {
        switch (m_kind) {
        case WidgetKind::Clock:
            m_title->setText(tr("Clock"));
            break;
        case WidgetKind::Calendar:
            m_title->setText(tr("Calendar"));
            break;
        case WidgetKind::System:
            m_title->setText(tr("System"));
            break;
        }
    }

    std::function<void()> onClicked;

    void paint(ui::Painter& painter) override
    {
        const ui::Theme& theme = ui::Theme::current();
        const ui::RectF preview = previewRect();
        const ui::Color fill = m_hovered ? theme.highlight.withAlpha(theme.darkMode ? 0.28 : 0.16)
                                         : theme.text.withAlpha(theme.darkMode ? 0.08 : 0.05);
        painter.fillRoundedRect(preview, theme.mediumRadius, fill);
        paintPreview(painter, m_kind, preview.shrunk({preview.width * 0.2, preview.height * 0.24,
                                                     preview.width * 0.2, preview.height * 0.24}));
    }

    void layout() override
    {
        m_title->setGeometry({0, geometry().height - 24, geometry().width, 20});
    }

protected:
    void pointerEnter() override
    {
        m_hovered = true;
        requestPaint();
    }
    void pointerLeave() override
    {
        m_hovered = false;
        requestPaint();
    }
    void pointerRelease(const ui::PointerEvent& event) override
    {
        if (localRect().contains(event.position) && onClicked)
            onClicked();
    }

private:
    ui::RectF previewRect() const
    {
        return {10, 10, geometry().width - 20, geometry().height - 44};
    }

    WidgetKind m_kind;
    ui::Label* m_title = nullptr;
    bool m_hovered = false;
};

// The panel: the only part of the gallery that takes presses. The dim around
// it is the gallery's paint, so the widgets being edited stay reachable
// beside it.
class WidgetGallery::PanelView final : public ui::View
{
public:
    PanelView() { setName("widget-gallery-panel"); }

    bool acceptsPointer() const override { return true; }

    void paint(ui::Painter& painter) override
    {
        const ui::Theme& theme = ui::Theme::current();
        painter.drawShadow(localRect(), theme.hugeRadius, 24, ui::Color {0, 0, 0, 90}, {0, 8});
        const ui::Color fill = theme.darkMode ? ui::Color {38, 38, 43, 248} : ui::Color {250, 250, 252, 248};
        painter.drawRoundedRect(localRect(), {theme.hugeRadius, 0, ui::Corners::Continuous}, fill, 1,
                                theme.text.withAlpha(0.09));
    }
};

WidgetGallery::WidgetGallery()
{
    setName("widget-gallery");
    setVisible(false);
    setClipsChildren(false);

    m_panelView = addChild<PanelView>();
    m_title = m_panelView->addChild<ui::Label>();
    m_title->setFontSize(20);
    m_title->setWeight(700);
    m_subtitle = m_panelView->addChild<ui::Label>();
    m_subtitle->setFontSize(12);
    m_subtitle->setColor(ui::ColorRecipe(ui::Theme::current().text.withAlpha(0.6)));

    for (WidgetKind kind : {WidgetKind::Clock, WidgetKind::Calendar, WidgetKind::System}) {
        PreviewCard* card = m_panelView->addChild<PreviewCard>(kind);
        card->onClicked = [this, kind] {
            if (onAdd)
                onAdd(kind);
        };
        m_cards.push_back(card);
    }

    m_done = m_panelView->addChild<ui::Button>(tr("Done"));
    m_done->setName("widget-gallery:done");
    m_done->onClicked = [this] {
        if (onDone)
            onDone();
    };
    retranslate();
}

WidgetGallery::~WidgetGallery() = default;

void WidgetGallery::retranslate()
{
    m_title->setText(tr("Widgets"));
    m_subtitle->setText(tr("Add widgets to your desktop"));
    m_done->setText(tr("Done"));
    for (PreviewCard* card : m_cards)
        card->retranslate();
}

void WidgetGallery::paint(ui::Painter& painter)
{
    painter.fillRect(localRect(), ui::Color {0, 0, 0, 96});
}

void WidgetGallery::layout()
{
    m_subtitle->setColor(ui::ColorRecipe(ui::Theme::current().text.withAlpha(0.6)));
    const ui::RectF area = localRect();
    const double width = std::min(560.0, std::max(320.0, area.width - 48));
    const double height = std::min(320.0, std::max(240.0, area.height - 48));
    m_panelView->setGeometry({(area.width - width) / 2, (area.height - height) / 2, width, height});

    m_title->setGeometry({24, 18, width - 48, 28});
    m_subtitle->setGeometry({24, 46, width - 48, 20});

    const double margin = 24;
    const double gap = 16;
    const double cardWidth = (width - margin * 2 - gap * 2) / 3;
    const double cardHeight = height - 118;
    for (size_t i = 0; i < m_cards.size(); ++i) {
        m_cards[i]->setGeometry({margin + i * (cardWidth + gap), 76, cardWidth, cardHeight});
    }
    const ui::SizeF doneSize = m_done->sizeHint();
    m_done->setGeometry({width - margin - std::max(76.0, doneSize.width + 24), height - 44,
                         std::max(76.0, doneSize.width + 24), 30});
}

bool WidgetGallery::keyPress(const ui::KeyEvent& event)
{
    if (event.key == ui::key::Escape) {
        if (onDone)
            onDone();
        return true;
    }
    return false;
}

} // namespace fm
