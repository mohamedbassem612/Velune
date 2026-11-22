#pragma once
#include "playback/MpvApi.h"
#include <deque>
#include <memory>
// This models event ordering, not media decoding. Real decoding is tested separately.
struct FakeMpv {
    struct Event {
        mpv_event event{};
        mpv_event_start_file start{};
        mpv_event_end_file end{};
        mpv_event_property property{};
        QByteArray name;
        double number = 0;
        int flag = 0;
    };
    std::deque<std::unique_ptr<Event>> events;
    std::unique_ptr<Event> current;
    void (*wakeup)(void *) = nullptr;
    void *wakeupData = nullptr;
    quint64 observer = 0;
    qint64 entry = 0;
    int loads = 0, seeks = 0;
    QStringList operations;
    bool destroyed = false;
    bool startFiles = true, notifyIdleOnStop = true;
    inline static FakeMpv *constructing = nullptr;
    static FakeMpv &self(mpv_handle *handle) {
        return *reinterpret_cast<FakeMpv *>(handle);
    }
    Event &push(mpv_event_id kind, quint64 id = 0) {
        auto event = std::make_unique<Event>();
        event->event.event_id = kind;
        event->event.reply_userdata = id;
        auto &result = *event;
        events.push_back(std::move(event));
        return result;
    }
    void wake() {
        if (wakeup)
            wakeup(wakeupData);
    }
    void property(const char *name, double value, quint64 epoch = 0) {
        auto &item = push(MPV_EVENT_PROPERTY_CHANGE, epoch ? epoch : observer);
        item.name = name;
        item.number = value;
        item.property = {item.name.constData(), MPV_FORMAT_DOUBLE, &item.number};
        item.event.data = &item.property;
        wake();
    }
    void idle(bool value) {
        auto &item = push(MPV_EVENT_PROPERTY_CHANGE, 0);
        item.name = "idle-active";
        item.flag = value;
        item.property = {item.name.constData(), MPV_FORMAT_FLAG, &item.flag};
        item.event.data = &item.property;
        wake();
    }
    void ended(qint64 id, mpv_end_file_reason reason) {
        auto &item = push(MPV_EVENT_END_FILE);
        item.end.playlist_entry_id = id;
        item.end.reason = reason;
        item.event.data = &item.end;
        wake();
    }
    std::shared_ptr<velune::MpvApi> api(const std::shared_ptr<FakeMpv> &owner) {
        struct OwnedApi : velune::MpvApi {
            std::shared_ptr<FakeMpv> state;
        };
        auto api = std::make_shared<OwnedApi>();
        api->state = owner;
        constructing = this;
        api->create = []() { return reinterpret_cast<mpv_handle *>(constructing); };
        api->initialize = [](mpv_handle *) { return 0; };
        api->terminate_destroy = [](mpv_handle *h) { self(h).destroyed = true; };
        api->set_option_string = [](mpv_handle *, const char *, const char *) { return 0; };
        api->observe_property = [](mpv_handle *h, uint64_t id, const char *name, mpv_format) {
            if (QByteArray(name) == "idle-active")
                self(h).idle(true);
            else
                self(h).observer = id;
            return 0;
        };
        api->unobserve_property = [](mpv_handle *, uint64_t) { return 0; };
        api->set_wakeup_callback = [](mpv_handle *h, void (*cb)(void *), void *data) {
            auto &s = self(h);
            s.wakeup = cb;
            s.wakeupData = data;
            s.wake();
        };
        api->wait_event = [](mpv_handle *h, double) -> mpv_event * {
            auto &s = self(h);
            if (s.events.empty())
                s.current = std::make_unique<Event>();
            else {
                s.current = std::move(s.events.front());
                s.events.pop_front();
            }
            return &s.current->event;
        };
        api->set_property_async = [](mpv_handle *h, uint64_t id, const char *name, mpv_format format,
                                     void *value) {
            const auto number =
                format == MPV_FORMAT_FLAG ? *static_cast<int *>(value) : *static_cast<double *>(value);
            self(h).operations.append(QString::fromLatin1(name) + "=" + QString::number(number));
            self(h).push(MPV_EVENT_SET_PROPERTY_REPLY, id);
            self(h).wake();
            return 0;
        };
        api->command_async = [](mpv_handle *h, uint64_t id, const char **args) {
            auto &s = self(h);
            if (QByteArray(args[0]) == "loadfile") {
                ++s.loads;
                if (s.startFiles) {
                    s.idle(false);
                    auto &item = s.push(MPV_EVENT_START_FILE);
                    item.start.playlist_entry_id = ++s.entry;
                    item.event.data = &item.start;
                    s.push(MPV_EVENT_FILE_LOADED);
                }
            } else if (QByteArray(args[0]) == "stop") {
                s.ended(s.entry, MPV_END_FILE_REASON_STOP);
                if (s.notifyIdleOnStop)
                    s.idle(true);
            } else if (QByteArray(args[0]) == "seek")
                ++s.seeks;
            s.push(MPV_EVENT_COMMAND_REPLY, id);
            s.wake();
            return 0;
        };
        return api;
    }
};
