#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QPointer>
#include <QSet>
#include <functional>
namespace velune {
// Desktop port of InnerTube SearchPage/SearchSummaryPage and the Android search view models.
class SearchParser {
  public:
    static QJsonObject item(const QJsonObject &renderer, bool summary = false);
    static QJsonObject suggestions(const QJsonObject &response);
    static QJsonObject page(const QJsonObject &response, bool summary);
    static QJsonArray visible(const QJsonArray &items, const QJsonObject &settings);
    static QString filterParams(const QString &filter);
};
class SearchEngine : public QObject {
    Q_OBJECT
  public:
    using Done = std::function<void(QJsonObject)>;
    using Transport = std::function<QNetworkReply *(QString, QJsonObject, Done, bool)>;
    SearchEngine(QString directory, Transport transport, QObject *parent = nullptr);
    void suggest(QString query);
    void search(QString query, QString filter = "all", bool record = true);
    void loadMore();
    void cancel();
    void invalidate() {
        cancel();
        cache.clear();
    }
    QJsonObject settings() const {
        return preferences;
    }
    void setSettings(QJsonObject settings);
    QStringList history(QString query = {}) const;
    void removeHistory(QString query);
    void clearHistory();
    void remember(QString query);
    static QJsonObject localPage(QString query, QString filter, const QJsonArray &items);
  signals:
    void suggestionsReady(QJsonObject suggestions);
    void pageReady(QJsonObject page, bool append);
    void busyChanged(bool busy);
    void failed(QString error);
    void historyChanged();
    void settingsChanged();

  private:
    QString directory;
    Transport transport;
    QJsonObject preferences, currentPage;
    QStringList queries;
    QList<QPointer<QNetworkReply>> requests;
    QHash<QString, QJsonObject> cache;
    QSet<QString> consumedTokens;
    QString query, filter;
    quint64 generation = 0;
    bool busy = false;
    void setBusy(bool value);
    void save();
    void request(QJsonObject body, quint64 ticket, Done done);
    void send(QString endpoint, QJsonObject body, quint64 ticket, Done done, bool session, int attempt = 0);
    void publish(QJsonObject page, bool append = false);
    void fallback(QJsonObject page, int stage, quint64 ticket);
    QString cacheKey() const;
};
} // namespace velune
