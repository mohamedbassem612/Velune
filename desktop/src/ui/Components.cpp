#include "Components.h"
#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOption>
#include <QStyledItemDelegate>
#include <QtMath>
namespace velune {
namespace {
class TrackListDelegate : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        if (index.data(Qt::UserRole + 8).toBool()) {
            auto font = option.font;
            font.setPixelSize(18);
            font.setWeight(QFont::DemiBold);
            p->setFont(font);
            p->setPen(QColor("#D0D0D0"));
            p->drawText(option.rect.adjusted(18, 0, -18, 0), Qt::AlignVCenter,
                        index.data(Qt::DisplayRole).toString().trimmed());
            p->restore();
            return;
        }
        const bool hovered = option.state & QStyle::State_MouseOver;
        const bool focused = option.state & QStyle::State_HasFocus;
        const bool selected = option.state & QStyle::State_Selected;
        const auto row = option.rect.adjusted(2, 3, -2, -3);
        p->setPen(focused ? QPen(QColor("#777777"), 1) : Qt::NoPen);
        p->setBrush(QColor(hovered ? "#202020" : selected ? "#141414" : "#000000"));
        p->drawRoundedRect(row, 12, 12);
        const auto cover = TrackListWidget::coverRect(option.rect);
        const auto icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        icon.paint(p, cover);
        auto font = option.font;
        font.setPixelSize(18);
        font.setWeight(QFont::DemiBold);
        p->setFont(font);
        p->setPen(Qt::white);
        const auto lines = index.data(Qt::DisplayRole).toString().split('\n');
        const bool grid = option.rect.height() >= 150;
        const int left = grid ? option.rect.left() + 12 : cover.right() + 18;
        const int available = grid ? option.rect.width() - 24 : qMax(0, option.rect.right() - left - 16);
        const int titleY = grid ? cover.bottom() + 8 : cover.top() + 2;
        if (grid) {
            font.setPixelSize(16);
            p->setFont(font);
        }
        p->drawText(QRect(left, titleY, available, 26), Qt::AlignVCenter,
                    QFontMetrics(font).elidedText(lines.value(0), Qt::ElideRight, available));
        font.setPixelSize(grid ? 13 : 15);
        font.setWeight(QFont::Normal);
        p->setFont(font);
        p->setPen(QColor("#A3A3A3"));
        p->drawText(QRect(left, titleY + 27, available, 23), Qt::AlignVCenter,
                    QFontMetrics(font).elidedText(lines.mid(1).join(" "), Qt::ElideRight, available));
        const auto canPlay = index.data(Qt::UserRole + 10);
        if ((hovered || focused) && (!canPlay.isValid() || canPlay.toBool())) {
            QPainterPath clip;
            clip.addRoundedRect(cover, 8, 8);
            p->fillPath(clip, QColor(0, 0, 0, 145));
            auto circle = QRectF(TrackListWidget::playRect(option.rect));
            p->setPen(QPen(Qt::white, 1.3));
            p->setBrush(QColor(0, 0, 0, 160));
            p->drawEllipse(circle);
            drawIcon(*p, Icon::Play, circle.adjusted(7, 7, -7, -7));
        }
        p->restore();
    }
};
} // namespace
TrackListWidget::TrackListWidget(QWidget *parent) : QListWidget(parent) {
    setItemDelegate(new TrackListDelegate(this));
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    setAccessibleDescription(
        "Click a cover to play. Click the title or press Enter to open details. Ctrl+Enter plays.");
    connect(this, &QListWidget::itemClicked, this, &TrackListWidget::openRequested);
}
QRect TrackListWidget::coverRect(const QRect &row) {
    if (row.height() >= 150) {
        const int side = qMin(180, qMin(row.width() - 24, row.height() - 70));
        return QRect(row.center().x() - side / 2, row.top() + 10, side, side);
    }
    return QRect(row.left() + 18, row.center().y() - 28, 56, 56);
}
QRect TrackListWidget::playRect(const QRect &row) {
    const auto cover = coverRect(row);
    if (cover.width() > 80)
        return QRect(cover.center() - QPoint(22, 22), QSize(44, 44));
    return cover.adjusted(10, 10, -10, -10);
}
void TrackListWidget::mousePressEvent(QMouseEvent *event) {
    const auto index = indexAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && index.isValid() && !index.data(Qt::UserRole + 8).toBool() &&
        (!index.data(Qt::UserRole + 10).isValid() || index.data(Qt::UserRole + 10).toBool()) &&
        playRect(visualRect(index)).contains(event->position().toPoint())) {
        pressedCover = index;
        coverPressed = true;
        event->accept();
        return;
    }
    QListWidget::mousePressEvent(event);
}
void TrackListWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && coverPressed) {
        const auto index = indexAt(event->position().toPoint());
        const bool play = pressedCover.isValid() && index == pressedCover &&
                          playRect(visualRect(index)).contains(event->position().toPoint());
        pressedCover = QPersistentModelIndex();
        coverPressed = false;
        if (play)
            emit playRequested(item(index.row()));
        event->accept();
        return;
    }
    QListWidget::mouseReleaseEvent(event);
}
void TrackListWidget::mouseMoveEvent(QMouseEvent *event) {
    viewport()->setCursor(indexAt(event->position().toPoint()).isValid() ? Qt::PointingHandCursor
                                                                         : Qt::ArrowCursor);
    QListWidget::mouseMoveEvent(event);
}
void TrackListWidget::keyPressEvent(QKeyEvent *event) {
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && currentItem()) {
        if (event->modifiers() & Qt::ControlModifier) {
            if (!currentItem()->data(Qt::UserRole + 10).isValid() ||
                currentItem()->data(Qt::UserRole + 10).toBool())
                emit playRequested(currentItem());
        } else
            emit openRequested(currentItem());
        event->accept();
        return;
    }
    QListWidget::keyPressEvent(event);
}
int SeekSlider::positionAt(int x) const {
    QStyleOptionSlider option;
    initStyleOption(&option);
    const auto handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
    const int span = qMax(1, width() - handle.width());
    return QStyle::sliderValueFromPosition(minimum(), maximum(), qBound(0, x - handle.width() / 2, span),
                                           span, option.upsideDown);
}
void SeekSlider::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QSlider::mousePressEvent(event);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    setSliderDown(true);
    setSliderPosition(positionAt(event->position().toPoint().x()));
    event->accept();
}
void SeekSlider::mouseMoveEvent(QMouseEvent *event) {
    if (!isSliderDown()) {
        QSlider::mouseMoveEvent(event);
        return;
    }
    setSliderPosition(positionAt(event->position().toPoint().x()));
    event->accept();
}
void SeekSlider::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || !isSliderDown()) {
        QSlider::mouseReleaseEvent(event);
        return;
    }
    const int requested = positionAt(event->position().toPoint().x());
    setSliderPosition(requested);
    setSliderDown(false);
    setValue(requested);
    event->accept();
}
namespace {
class HoverMotion : public QObject {
  public:
    explicit HoverMotion(QPushButton *button) : QObject(button), button(button) {
        button->setProperty("veluneHover", 0.0);
        button->installEventFilter(this);
        transition.setDuration(180);
        transition.setEasingCurve(QEasingCurve::OutCubic);
        connect(&transition, &QVariantAnimation::valueChanged, button, [button](const QVariant &value) {
            button->setProperty("veluneHover", value);
            button->update();
        });
    }
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
            transition.stop();
            transition.setStartValue(button->property("veluneHover"));
            transition.setEndValue(event->type() == QEvent::Enter ? 1.0 : 0.0);
            transition.start();
        }
        return false;
    }

  private:
    QPushButton *button;
    QVariantAnimation transition;
};
double hoverOf(const QWidget *widget) {
    return widget->property("veluneHover").toDouble();
}
} // namespace
void drawIcon(QPainter &p, Icon icon, QRectF rect, QColor color, bool filled) {
    p.save();
    p.translate(rect.topLeft());
    p.scale(rect.width() / 24., rect.height() / 24.);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    auto line = [&](double a, double b, double c, double d) { p.drawLine(QPointF(a, b), QPointF(c, d)); };
    QPainterPath path;
    switch (icon) {
    case Icon::Logo:
        path.moveTo(4, 8);
        path.lineTo(10, 19);
        path.lineTo(20, 7);
        path.lineTo(14, 3);
        path.lineTo(11, 7);
        p.setPen(QPen(color, 3.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        break;
    case Icon::Home:
        path.moveTo(3, 10);
        path.lineTo(12, 2.5);
        path.lineTo(21, 10);
        path.moveTo(5, 9);
        path.lineTo(5, 21);
        path.lineTo(10, 21);
        path.lineTo(10, 14);
        path.lineTo(14, 14);
        path.lineTo(14, 21);
        path.lineTo(19, 21);
        path.lineTo(19, 9);
        p.drawPath(path);
        break;
    case Icon::Search:
        p.drawEllipse(QRectF(3, 2, 14, 14));
        line(16, 15, 22, 21);
        break;
    case Icon::Explore:
        p.drawEllipse(QRectF(2, 2, 20, 20));
        path.moveTo(16, 7);
        path.lineTo(13, 14);
        path.lineTo(7, 17);
        path.lineTo(10, 10);
        path.closeSubpath();
        p.drawPath(path);
        break;
    case Icon::Library:
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c)
                p.drawRoundedRect(QRectF(3 + c * 11, 3 + r * 11, 7, 7), .5, .5);
        break;
    case Icon::Devices:
        p.drawRoundedRect(QRectF(2, 3, 20, 14), 1.5, 1.5);
        line(12, 17, 12, 21);
        line(7, 21, 17, 21);
        break;
    case Icon::Settings:
        for (int i = 0; i < 8; ++i) {
            p.save();
            p.translate(12, 12);
            p.rotate(i * 45);
            p.drawRoundedRect(QRectF(-2, -11, 4, 5), .7, .7);
            p.restore();
        }
        p.drawEllipse(QRectF(5, 5, 14, 14));
        p.drawEllipse(QRectF(9, 9, 6, 6));
        break;
    case Icon::Play:
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        path.moveTo(8, 5);
        path.lineTo(20, 12);
        path.lineTo(8, 19);
        path.closeSubpath();
        p.drawPath(path);
        break;
    case Icon::Pause:
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(6, 5, 4, 14), .7, .7);
        p.drawRoundedRect(QRectF(14, 5, 4, 14), .7, .7);
        break;
    case Icon::Next:
    case Icon::Previous:
        if (icon == Icon::Previous) {
            p.translate(24, 0);
            p.scale(-1, 1);
        }
        p.setBrush(color);
        path.moveTo(5, 5);
        path.lineTo(16, 12);
        path.lineTo(5, 19);
        path.closeSubpath();
        p.drawPath(path);
        line(18, 5, 18, 19);
        break;
    case Icon::Shuffle:
        path.moveTo(3, 6);
        path.cubicTo(9, 6, 15, 18, 21, 18);
        path.moveTo(3, 18);
        path.cubicTo(9, 18, 15, 6, 21, 6);
        p.drawPath(path);
        line(18, 3, 21, 6);
        line(18, 9, 21, 6);
        line(18, 15, 21, 18);
        line(18, 21, 21, 18);
        break;
    case Icon::Repeat:
        path.moveTo(3, 10);
        path.lineTo(3, 6);
        path.lineTo(20, 6);
        path.moveTo(21, 14);
        path.lineTo(21, 18);
        path.lineTo(4, 18);
        p.drawPath(path);
        line(17, 3, 20, 6);
        line(17, 9, 20, 6);
        line(7, 15, 4, 18);
        line(7, 21, 4, 18);
        break;
    case Icon::Queue:
        line(3, 5, 21, 5);
        line(3, 10, 21, 10);
        line(3, 15, 12, 15);
        line(17, 13, 17, 21);
        line(13, 17, 21, 17);
        break;
    case Icon::Fullscreen:
        for (int i = 0; i < 4; ++i) {
            p.save();
            p.translate(12, 12);
            p.rotate(90 * i);
            path = QPainterPath();
            path.moveTo(-9, -3);
            path.lineTo(-9, -9);
            path.lineTo(-3, -9);
            p.drawPath(path);
            p.restore();
        }
        break;
    case Icon::Volume:
    case Icon::Muted:
        path.moveTo(3, 9);
        path.lineTo(7, 9);
        path.lineTo(12, 5);
        path.lineTo(12, 19);
        path.lineTo(7, 15);
        path.lineTo(3, 15);
        path.closeSubpath();
        p.drawPath(path);
        if (icon == Icon::Volume) {
            p.drawArc(QRectF(9, 6, 10, 12), -65 * 16, 130 * 16);
            p.drawArc(QRectF(6, 2, 18, 20), -60 * 16, 120 * 16);
        } else {
            line(16, 8, 22, 16);
            line(16, 16, 22, 8);
        }
        break;
    case Icon::Heart:
        path.moveTo(12, 21);
        path.cubicTo(-3, 12, 2, 1, 9, 5);
        path.lineTo(12, 8);
        path.lineTo(15, 5);
        path.cubicTo(22, 1, 27, 12, 12, 21);
        if (filled)
            p.setBrush(color);
        p.drawPath(path);
        break;
    case Icon::Radio:
        p.setBrush(color);
        p.drawEllipse(QRectF(10, 10, 4, 4));
        p.setBrush(Qt::NoBrush);
        for (int i = 0; i < 2; ++i) {
            p.drawArc(QRectF(6 - i * 4, 6 - i * 4, 12 + i * 8, 12 + i * 8), -55 * 16, 110 * 16);
            p.drawArc(QRectF(6 - i * 4, 6 - i * 4, 12 + i * 8, 12 + i * 8), 125 * 16, 110 * 16);
        }
        break;
    case Icon::Minimize:
        line(6, 12, 18, 12);
        break;
    case Icon::Maximize:
        p.drawRect(QRectF(6, 6, 12, 12));
        break;
    case Icon::Close:
        line(6, 6, 18, 18);
        line(6, 18, 18, 6);
        break;
    case Icon::Back:
        line(20, 12, 4, 12);
        line(4, 12, 11, 5);
        line(4, 12, 11, 19);
        break;
    case Icon::More:
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        for (int y : {5, 12, 19})
            p.drawEllipse(QPointF(12, y), 1.7, 1.7);
        break;
    case Icon::Google:
        p.setPen(QPen(color, 2.6));
        p.drawArc(QRectF(3, 3, 18, 18), 40 * 16, 285 * 16);
        line(12, 12, 21, 12);
        line(21, 12, 21, 17);
        break;
    default:
        break;
    }
    p.restore();
}
void drawWaves(QPainter &p, QRectF r, int variant) {
    p.save();
    p.setClipRect(r);
    p.translate(r.topLeft());
    p.scale(r.width() / 400., r.height() / 300.);
    p.setBrush(Qt::NoBrush);
    for (int layer = 0; layer < 3; ++layer) {
        for (int band = 55; band >= 0; --band) {
            const double shift = band * .9;
            QPainterPath path;
            path.moveTo(-25, 105 + layer * 32 + variant * 6 + shift);
            path.cubicTo(60, 205 + layer * 5 + shift, 125, 221 - layer * 18 + shift, 230,
                         136 + layer * 3 + shift);
            path.cubicTo(310, 50 + layer * 18 + variant * 15 + shift, 360, 68 + layer * 10 + shift, 430,
                         136 + shift);
            double strength = qExp(-band / 9.0) * (layer == 1 ? 1. : .42);
            QLinearGradient ink(45, 170, 355, 90);
            ink.setColorAt(0, QColor(92, 92, 92, qRound(90 * strength)));
            ink.setColorAt(.35, QColor(180, 180, 180, qRound(70 * strength)));
            ink.setColorAt(.78, QColor(240, 240, 240, qRound(230 * strength)));
            ink.setColorAt(1, QColor(110, 110, 110, qRound(75 * strength)));
            p.setPen(QPen(QBrush(ink), 1.2));
            p.drawPath(path);
        }
    }
    QLinearGradient shade(0, 180, 0, 300);
    shade.setColorAt(0, QColor(0, 0, 0, 0));
    shade.setColorAt(1, QColor(0, 0, 0, 235));
    p.fillRect(QRectF(0, 0, 400, 300), shade);
    p.restore();
}
IconButton::IconButton(Icon icon, QString tooltip, QWidget *parent) : QPushButton(parent), glyph(icon) {
    new HoverMotion(this);
    setFixedSize(40, 40);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tooltip);
    setAccessibleName(tooltip);
    setFocusPolicy(Qt::StrongFocus);
    likeAnimation.setDuration(180);
    likeAnimation.setEasingCurve(QEasingCurve::OutCubic);
    connect(&likeAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        heartFill = value.toDouble();
        update();
    });
    connect(this, &QPushButton::toggled, this, [this](bool liked) {
        if (glyph != Icon::Heart)
            return;
        likeAnimation.stop();
        if (!isVisible()) {
            heartFill = liked ? 1. : 0.;
            update();
            return;
        }
        likeAnimation.setStartValue(heartFill);
        likeAnimation.setEndValue(liked ? 1. : 0.);
        likeAnimation.start();
    });
}
void IconButton::setCheckedAppearance(bool checked, bool animate) {
    if (animate && isChecked() == checked)
        return;
    {
        QSignalBlocker blocker(this);
        QPushButton::setChecked(checked);
    }
    likeAnimation.stop();
    if (!animate || !isVisible()) {
        heartFill = checked ? 1. : 0.;
        update();
        return;
    }
    likeAnimation.setStartValue(heartFill);
    likeAnimation.setEndValue(checked ? 1. : 0.);
    likeAnimation.start();
}
LikeButton::LikeButton(Library *source, const Track &track, QWidget *parent)
    : IconButton(Icon::Heart, "Like song", parent), library(source) {
    setCheckable(true);
    connect(library, &Library::changed, this, [this] { sync(); });
    connect(this, &QPushButton::clicked, this, [this] {
        if (song.id.isEmpty())
            return;
        library->setLiked(song, !library->liked(song.id));
        sync(); // Also restore the actual state if saving failed.
    });
    setTrack(track);
}
void LikeButton::paintEvent(QPaintEvent *event) {
    if (isChecked() != (!song.id.isEmpty() && library->liked(song.id)))
        sync(false);
    IconButton::paintEvent(event);
}
void LikeButton::setTrack(const Track &track) {
    const bool changed = track.id != song.id;
    song = track;
    setProperty("trackId", track.id);
    setEnabled(!track.id.isEmpty());
    sync(!changed);
}
void LikeButton::sync(bool animate) {
    const bool liked = !song.id.isEmpty() && library->liked(song.id);
    setCheckedAppearance(liked, animate);
    const auto label = (liked ? "Remove from Liked Songs: " : "Add to Liked Songs: ") + song.title;
    setToolTip(label);
    setAccessibleName(label);
}
void IconButton::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    auto r = QRectF(rect()).adjusted(2, 2, -2, -2);
    const auto hover = hoverOf(this);
    if (property("coverPlay").toBool()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 190));
        p.drawEllipse(r);
    }
    if (hover > 0 || isDown() || hasFocus() || (isChecked() && glyph != Icon::Heart)) {
        p.setPen(hasFocus() ? QPen(Qt::white, 1) : Qt::NoPen);
        int level = isDown() ? 48 : (hasFocus() || isChecked()) ? 36 : qRound(36 * hover);
        p.setBrush(QColor(level, level, level));
        if (circle)
            p.drawEllipse(r);
        else
            p.drawRoundedRect(r, 8, 8);
    }
    if (circle) {
        p.setPen(QPen(QColor("#D0D0D0"), 1.5));
        p.setBrush(solid || (hoverFill && underMouse()) ? QColor(Qt::white) : Qt::transparent);
        p.drawEllipse(r.adjusted(1, 1, -1, -1));
    }
    QColor c =
        (solid || (hoverFill && underMouse())) ? Qt::black : (isEnabled() ? Qt::white : QColor("#666666"));
    double half = 12 * (circle ? 1 + .025 * hover : 1);
    if (glyph == Icon::Heart) {
        const double target = isChecked() ? 1. : 0.;
        const bool transitioning = likeAnimation.state() == QAbstractAnimation::Running &&
                                   likeAnimation.endValue().toDouble() == target;
        const double fill = transitioning ? heartFill : target;
        half *= 1. + .07 * qSin(M_PI * fill);
        const QRectF iconRect(width() / 2. - half, height() / 2. - half, half * 2, half * 2);
        drawIcon(p, glyph, iconRect, c, false);
        if (fill > 0) {
            p.setOpacity(fill);
            drawIcon(p, glyph, iconRect, c, true);
        }
    } else
        drawIcon(p, glyph, QRectF(width() / 2. - half, height() / 2. - half, half * 2, half * 2), c,
                 isChecked());
}
NavButton::NavButton(Icon icon, QString text, QWidget *parent) : QPushButton(text, parent), glyph(icon) {
    new HoverMotion(this);
    setCheckable(true);
    setFixedHeight(56);
    setCursor(Qt::PointingHandCursor);
    setAccessibleName(text);
}
void NavButton::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    auto r = QRectF(rect()).adjusted(1, 1, -1, -1);
    p.setPen(hasFocus() ? QPen(Qt::white, 1) : Qt::NoPen);
    int level = isChecked() ? 36 : qRound(20 * hoverOf(this));
    p.setBrush(QColor(level, level, level));
    p.drawRoundedRect(r, 16, 16);
    drawIcon(p, glyph, QRectF(22, height() / 2. - 13, 26, 26));
    auto f = font();
    f.setPixelSize(qBound(17, width() / 15, 22));
    if (glyph == Icon::Devices)
        f.setPixelSize(qMin(16, f.pixelSize()));
    f.setWeight(QFont::Medium);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRect(70, 0, width() - 78, height()), Qt::AlignVCenter, text());
}
QuickTile::QuickTile(QString label, Icon icon, QWidget *parent) : QPushButton(label, parent), glyph(icon) {
    new HoverMotion(this);
    setMinimumHeight(72);
    setMinimumWidth(180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    playControl = new IconButton(Icon::Play, "Play " + label, this);
    playControl->setObjectName("contentPlay");
    playControl->setProperty("coverPlay", true);
    playControl->setCircle(true);
    playControl->setHoverFill(true);
    playControl->hide();
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *) {
        playControl->setVisible(underMouse() || hasFocus() || playControl->hasFocus());
    });
    connect(playControl, &QPushButton::clicked, this, &QuickTile::playRequested);
    setAccessibleName(label);
}
void QuickTile::setContent(QString label, QString detail, Icon icon) {
    setText(label);
    subtitle = detail;
    glyph = icon;
    setAccessibleName(label + ". " + detail);
    setToolTip(label + "\n" + detail);
    playControl->setToolTip("Play " + label);
    playControl->setAccessibleName("Play " + label);
    update();
}
bool QuickTile::event(QEvent *e) {
    bool result = QPushButton::event(e);
    if (e->type() == QEvent::Enter || e->type() == QEvent::Leave)
        if (playControl)
            playControl->setVisible(underMouse() || hasFocus() || playControl->hasFocus());
    return result;
}
void QuickTile::resizeEvent(QResizeEvent *e) {
    QPushButton::resizeEvent(e);
    if (playControl)
        playControl->move(12 + (qMin(52, height() - 16) - 40) / 2, (height() - 40) / 2);
}
void QuickTile::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    int border = 48 + qRound(44 * hoverOf(this));
    int surface = 10 + qRound(18 * hoverOf(this));
    p.setPen(QPen(hasFocus() ? Qt::white : QColor(border, border, border), 1));
    p.setBrush(QColor(surface, surface, surface));
    p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 18, 18);
    const int side = qMin(52, height() - 16);
    QRectF cover(12, (height() - side) / 2., side, side);
    p.save();
    QPainterPath clip;
    clip.addRoundedRect(cover, 8, 8);
    p.setClipPath(clip);
    p.fillRect(cover, QColor("#181818"));
    if (!art.isNull() && glyph != Icon::Heart) {
        auto scaled = art.scaled(side, side, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        p.drawPixmap(cover.topLeft().toPoint(), scaled,
                     QRect((scaled.width() - side) / 2, (scaled.height() - side) / 2, side, side));
    } else
        drawIcon(p, glyph == Icon::Heart ? Icon::Heart : Icon::Library, cover.adjusted(11, 11, -11, -11),
                 Qt::white, glyph == Icon::Heart);
    p.restore();
    if (glyph == Icon::Radio) {
        p.fillRect(QRectF(cover.right() - 20, cover.top(), 20, 20), QColor("#141414"));
        drawIcon(p, Icon::Radio, QRectF(cover.right() - 18, cover.top() + 2, 16, 16));
    }
    const int left = 12 + side + 16;
    const int available = qMax(0, width() - left - 16);
    auto f = font();
    f.setPixelSize(qBound(15, width() / 20, 20));
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRect(left, height() / 2 - 24, available, 26), Qt::AlignVCenter,
               QFontMetrics(f).elidedText(text(), Qt::ElideRight, available));
    f.setPixelSize(qBound(13, width() / 24, 16));
    f.setWeight(QFont::Normal);
    p.setFont(f);
    p.setPen(QColor("#A3A3A3"));
    p.drawText(QRect(left, height() / 2 + 2, available, 22), Qt::AlignVCenter,
               QFontMetrics(f).elidedText(subtitle, Qt::ElideRight, available));
}
SongCard::SongCard(int v, QWidget *parent) : QPushButton(parent), variant(v) {
    setMinimumSize(180, 220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::PointingHandCursor);
    setAccessibleName("Open featured song");
    playControl = new IconButton(Icon::Play, "Play song", this);
    playControl->setObjectName("contentPlay");
    playControl->setProperty("coverPlay", true);
    playControl->setFixedSize(58, 58);
    playControl->setCircle(true);
    playControl->setHoverFill(true);
    playControl->hide();
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *) {
        playControl->setVisible(underMouse() || hasFocus() || playControl->hasFocus());
    });
    connect(playControl, &QPushButton::clicked, this, &SongCard::playRequested);
    animation.setDuration(180);
    connect(&animation, &QVariantAnimation::valueChanged, this, [this](QVariant v) {
        hover = v.toDouble();
        update();
    });
}
void SongCard::enterEvent(QEnterEvent *e) {
    animation.stop();
    animation.setStartValue(hover);
    animation.setEndValue(1.);
    animation.start();
    QPushButton::enterEvent(e);
}
void SongCard::leaveEvent(QEvent *e) {
    animation.stop();
    animation.setStartValue(hover);
    animation.setEndValue(0.);
    animation.start();
    QPushButton::leaveEvent(e);
}
void SongCard::resizeEvent(QResizeEvent *e) {
    QPushButton::resizeEvent(e);
    if (playControl)
        playControl->move((width() - 58) / 2, (height() - 58) / 2);
}
bool SongCard::event(QEvent *e) {
    bool result = QPushButton::event(e);
    if (e->type() == QEvent::Enter || e->type() == QEvent::Leave)
        if (playControl)
            playControl->setVisible(underMouse() || hasFocus() || playControl->hasFocus());
    return result;
}
void SongCard::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    auto r = QRectF(rect()).adjusted(1, 1, -1, -1);
    QPainterPath clip;
    clip.addRoundedRect(r, 24, 24);
    p.setClipPath(clip);
    p.fillRect(r, QColor("#050505"));
    if (art.isNull())
        drawWaves(p, r, variant);
    else {
        auto scaled = art.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        p.drawPixmap((width() - scaled.width()) / 2, (height() - scaled.height()) / 2, scaled);
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0, QColor(0, 0, 0, 15));
        g.setColorAt(.6, QColor(0, 0, 0, 80));
        g.setColorAt(1, QColor(0, 0, 0, 240));
        p.fillRect(r, g);
    }
    p.fillRect(r, QColor(255, 255, 255, qRound(hover * 7)));
    auto f = font();
    f.setPixelSize(qBound(30, width() / 8, 46));
    f.setWeight(QFont::Medium);
    p.setFont(f);
    p.setPen(Qt::white);
    if (art.isNull())
        p.drawText(r.adjusted(0, 0, 0, 3), Qt::AlignCenter, "Song");
    f.setPixelSize(qBound(18, width() / 16, 25));
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    auto title = song.title.isEmpty() ? "Title" : song.title;
    p.drawText(QRect(26, height() - 88, width() - 108, 34), Qt::AlignVCenter,
               QFontMetrics(f).elidedText(title, Qt::ElideRight, width() - 114));
    f.setPixelSize(qBound(15, width() / 20, 20));
    f.setWeight(QFont::Normal);
    p.setFont(f);
    p.setPen(QColor("#A3A3A3"));
    p.drawText(QRect(26, height() - 54, width() - 108, 26), Qt::AlignVCenter,
               QFontMetrics(f).elidedText(song.artist.isEmpty() ? "name" : song.artist, Qt::ElideRight,
                                          width() - 114));
    p.setClipping(false);
    p.setPen(QPen(hasFocus() ? Qt::white : QColor(underMouse() ? "#555555" : "#303030"), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, 24, 24);
}
} // namespace velune
