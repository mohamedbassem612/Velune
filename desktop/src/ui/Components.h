#pragma once
#include "core/Library.h"
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVariantAnimation>
namespace velune {
class ElidedLabel : public QLabel {
  public:
    explicit ElidedLabel(const QString &text, QWidget *parent = nullptr) : QLabel(parent), fullText(text) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setText(text);
    }
    void setText(const QString &text) {
        fullText = text;
        setToolTip(text);
        QLabel::setText(fontMetrics().elidedText(fullText, Qt::ElideRight, width()));
    }

  protected:
    void resizeEvent(QResizeEvent *event) override {
        QLabel::resizeEvent(event);
        QLabel::setText(fontMetrics().elidedText(fullText, Qt::ElideRight, width()));
    }

  private:
    QString fullText;
};
enum class Icon {
    None,
    Logo,
    Home,
    Search,
    Explore,
    Library,
    Devices,
    Settings,
    Play,
    Pause,
    Next,
    Previous,
    Shuffle,
    Repeat,
    Queue,
    Fullscreen,
    Volume,
    Muted,
    Heart,
    Radio,
    Minimize,
    Maximize,
    Close,
    Google
};
void drawIcon(QPainter &p, Icon icon, QRectF rect, QColor color = Qt::white, bool filled = false);
void drawWaves(QPainter &p, QRectF rect, int variant);
class IconButton : public QPushButton {
    Q_OBJECT
  public:
    IconButton(Icon icon, QString tooltip, QWidget *parent = nullptr);
    void setGlyph(Icon icon) {
        glyph = icon;
        update();
    }
    void setCircle(bool value) {
        circle = value;
        update();
    }
    void setSolid(bool value) {
        solid = value;
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    Icon glyph;
    bool circle = false, solid = false;
};
class NavButton : public QPushButton {
    Q_OBJECT
  public:
    NavButton(Icon icon, QString text, QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    Icon glyph;
};
class QuickTile : public QPushButton {
    Q_OBJECT
  public:
    QuickTile(QString label, Icon icon, QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    Icon glyph;
};
class SongCard : public QPushButton {
    Q_OBJECT
  public:
    SongCard(int variant, QWidget *parent = nullptr);
    void setTrack(const Track &track) {
        song = track;
        update();
    }
    void setArtwork(QPixmap pixmap) {
        art = std::move(pixmap);
        update();
    }
    Track track() const {
        return song;
    }
    QSize sizeHint() const override {
        return {300, 290};
    }

  protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

  private:
    int variant;
    Track song;
    QPixmap art;
    QVariantAnimation animation;
    double hover = 0;
};
} // namespace velune
