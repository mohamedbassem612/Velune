#include "PlaybackEngineFactory.h"
#include "MpvPlaybackEngine.h"
#include "QtPlaybackEngine.h"
#include <QDebug>
#include <QVariant>
namespace velune {
PlaybackEngineSelection selectPlaybackEngine(QString requested, QByteArray audioDevice) {
    requested = requested.trimmed().toLower();
    QString diagnostic;
#ifndef VELUNE_MULTIMEDIA
    if (requested == "qt")
        diagnostic = "Qt Multimedia is disabled in this build; using libmpv.";
    else if (!requested.isEmpty() && requested != "mpv")
        diagnostic = "Unknown VELUNE_PLAYBACK_BACKEND value; using libmpv.";
    requested = "mpv";
#endif
    if (requested == "mpv") {
        QString mpvDevice = QString::fromUtf8(audioDevice);
#ifdef Q_OS_LINUX
        if (!mpvDevice.isEmpty())
            mpvDevice.prepend("pulse/");
#endif
        auto mpv = std::make_unique<MpvPlaybackEngine>(nullptr, mpvDevice);
        if (mpv->available())
            return {std::move(mpv), diagnostic};
        diagnostic = "Requested mpv backend unavailable: " + mpv->initializationError();
#ifndef VELUNE_MULTIMEDIA
        return {std::move(mpv), diagnostic};
#else
        auto qt = std::make_unique<QtPlaybackEngine>(audioDevice);
        if (!qt->available())
            return {std::move(mpv), diagnostic + " Qt Multimedia is also unavailable."};
        return {std::move(qt), diagnostic + " Using Qt Multimedia."};
#endif
    }
    if (!requested.isEmpty() && requested != "qt")
        diagnostic = "Unknown VELUNE_PLAYBACK_BACKEND value; using Qt Multimedia (accepted: qt, mpv).";
    return {std::make_unique<QtPlaybackEngine>(audioDevice), diagnostic};
}
std::unique_ptr<IPlaybackEngine> createPlaybackEngine() {
    auto selection = selectPlaybackEngine();
    selection.engine->setProperty("backendDiagnostic", selection.diagnostic);
    if (!selection.diagnostic.isEmpty())
        qWarning().noquote() << selection.diagnostic;
    return std::move(selection.engine);
}
} // namespace velune
