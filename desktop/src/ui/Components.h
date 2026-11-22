#pragma once
#include "core/Library.h"
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QPushButton>
#include <QSlider>
#include <QVariantAnimation>
namespace velune {
class TrackListWidget : public QListWidget {
    Q_OBJECT
  public:
    explicit TrackListWidget(QWidget *parent = nullptr);
    static QRect coverRect(const QRect &row);
    static QRect playRect(const QRect &row);
  signals:
    void playRequested(QListWidgetItem *item);
    void openRequested(QListWidgetItem *item);

  protected:
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

  private:
    QPersistentModelIndex pressedCover;
    bool coverPressed = false;
};
class SeekSlider : public QSlider {
  public:
    explicit SeekSlider(QWidget *parent = nullptr) : QSlider(Qt::Horizontal, parent) {
        setTracking(false);
        setSingleStep(5000);
        setPageStep(10000);
    }

  protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

  private:
    int positionAt(int x) const;
};
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
    Google,
    Back,
    More
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
    void setHoverFill(bool value) {
        hoverFill = value;
        update();
    }
    void setSolid(bool value) {
        solid = value;
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override;
    void setCheckedAppearance(bool checked, bool animate);

  private:
    Icon glyph;
    QVariantAnimation likeAnimation;
    double heartFill = 0;
    bool circle = false, solid = false, hoverFill = false;
};
class LikeButton : public IconButton {
    Q_OBJECT
  public:
    LikeButton(Library *library, const Track &track = {}, QWidget *parent = nullptr);
    void setTrack(const Track &track);

  protected:
    void nextCheckState() override {} // Only a successful library mutation changes the state.
    void paintEvent(QPaintEvent *) override;

  private:
    Library *library;
    Track song;
    void sync(bool animate = true);
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
    void setContent(QString label, QString subtitle, Icon icon = Icon::None);
    void setArtwork(QPixmap pixmap) {
        art = std::move(pixmap);
        update();
    }
    IconButton *playButton() const {
        return playControl;
    }
  signals:
    void playRequested();

  protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    bool event(QEvent *) override;

  private:
    Icon glyph;
    QString subtitle;
    QPixmap art;
    IconButton *playControl = nullptr;
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
    IconButton *playButton() const {
        return playControl;
    }
  signals:
    void playRequested();

  public:
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
    void resizeEvent(QResizeEvent *) override;
    bool event(QEvent *) override;

  private:
    IconButton *playControl = nullptr;
    int variant;
    Track song;
    QPixmap art;
    QVariantAnimation animation;
    double hover = 0;
};
} // namespace velune
