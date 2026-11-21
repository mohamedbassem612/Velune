#include "Components.h"
#include <QEvent>
#include <QPainterPath>
#include <QStyleOption>
#include <QtMath>
namespace velune {
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
}
void IconButton::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    auto r = QRectF(rect()).adjusted(2, 2, -2, -2);
    const auto hover = hoverOf(this);
    if (hover > 0 || isDown() || hasFocus() || isChecked()) {
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
        p.setBrush(solid ? QColor(Qt::white) : Qt::transparent);
        p.drawEllipse(r.adjusted(1, 1, -1, -1));
    }
    QColor c = solid ? Qt::black : (isEnabled() ? Qt::white : QColor("#666666"));
    double half = 12 * (circle ? 1 + .025 * hover : 1);
    drawIcon(p, glyph, QRectF(width() / 2. - half, height() / 2. - half, half * 2, half * 2), c, isChecked());
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
    setMinimumHeight(54);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
}
void QuickTile::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    int border = 48 + qRound(44 * hoverOf(this));
    int surface = 10 + qRound(10 * hoverOf(this));
    p.setPen(QPen(hasFocus() ? Qt::white : QColor(border, border, border), 1));
    p.setBrush(QColor(surface, surface, surface));
    p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 18, 18);
    auto f = font();
    f.setPixelSize(qBound(18, width() / 13, 25));
    f.setWeight(QFont::Medium);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRect(24, 0, width() - (glyph == Icon::None ? 36 : 78), height()), Qt::AlignVCenter,
               fontMetrics().elidedText(text(), Qt::ElideRight, width() - 90));
    if (glyph != Icon::None) {
        auto r = QRectF(width() - 56, height() / 2. - 18, 36, 36);
        if (glyph == Icon::Play) {
            p.setPen(QPen(QColor("#D0D0D0"), 1.5));
            p.drawEllipse(r);
        }
        drawIcon(p, glyph, r.adjusted(5, 5, -5, -5));
    }
}
SongCard::SongCard(int v, QWidget *parent) : QPushButton(parent), variant(v) {
    setMinimumSize(180, 220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::PointingHandCursor);
    setAccessibleName("Play featured song");
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
    auto play = QRectF(width() - 80, height() - 84, 54, 54);
    p.setPen(QPen(QColor("#D0D0D0"), 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(play.adjusted(-hover, -hover, hover, hover));
    drawIcon(p, Icon::Play, play.adjusted(12, 12, -12, -12));
    p.setClipping(false);
    p.setPen(QPen(hasFocus() ? Qt::white : QColor(underMouse() ? "#555555" : "#303030"), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, 24, 24);
}
} // namespace velune
