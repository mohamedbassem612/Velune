// A Qt-free libmpv process. Each source has one process and one fenced generation.
// Input is a bounded, tab-delimited command protocol; output is JSON events.
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <mpv/client.h>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
struct Api {
#ifdef _WIN32
    HMODULE library = nullptr;
#else
    void *library = nullptr;
#endif
#define FUNCTIONS(X)                                                                                         \
    X(client_api_version)                                                                                    \
    X(create) X(initialize) X(terminate_destroy) X(set_option_string) X(command_async) X(set_property_async) \
        X(observe_property) X(wait_event) X(wakeup)
#define DECLARE(n) decltype(&mpv_##n) n = nullptr;
    FUNCTIONS(DECLARE)
#undef DECLARE
    bool open() {
        const char *path = std::getenv("VELUNE_MPV_LIBRARY");
#ifdef _WIN32
        library = LoadLibraryA(path ? path : "mpv-2.dll");
#define LOAD(n)                                                                                              \
    n = reinterpret_cast<decltype(n)>(GetProcAddress(library, "mpv_" #n));                                   \
    if (!n)                                                                                                  \
        return false;
#else
        library = dlopen(path ? path : "libmpv.so.2", RTLD_NOW | RTLD_LOCAL);
#define LOAD(n)                                                                                              \
    n = reinterpret_cast<decltype(n)>(dlsym(library, "mpv_" #n));                                            \
    if (!n)                                                                                                  \
        return false;
#endif
        if (!library)
            return false;
        FUNCTIONS(LOAD)
#undef LOAD
        const auto version = client_api_version();
        return (version >> 16) == 2 && version >= MPV_MAKE_VERSION(2, 2);
    }
};
std::mutex output;
void emit(const std::string &event) {
    std::lock_guard<std::mutex> lock(output);
    std::cout << event << '\n' << std::flush;
}
std::string unhex(const std::string &value) {
    if (value.size() % 2 || value.size() > 32768)
        return {};
    std::string result;
    for (size_t i = 0; i < value.size(); i += 2) {
        const auto digit = [](char c) -> int {
            return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        };
        const int a = digit(value[i]), b = digit(value[i + 1]);
        if (a < 0 || b < 0 || !(a || b))
            return {};
        result.push_back(char(a * 16 + b));
    }
    return result;
}
int main() {
    Api api;
    if (!api.open()) {
        emit(R"({"event":"error","message":"Compatible libmpv 0.37+ runtime is unavailable."})");
        return 1;
    }
    auto *core = api.create();
    if (!core)
        return 1;
    bool valid = true;
    for (const auto &[key, value] : {std::pair{"config", "no"},
                                     {"load-scripts", "no"},
                                     {"ytdl", "no"},
                                     {"terminal", "no"},
                                     {"input-default-bindings", "no"},
                                     {"input-vo-keyboard", "no"},
                                     {"vid", "no"},
                                     {"sid", "no"},
                                     {"audio-display", "no"},
                                     {"force-window", "no"},
                                     {"tls-verify", "yes"},
                                     {"pause", "yes"},
                                     {"volume", "75"}})
        valid = api.set_option_string(core, key, value) >= 0 && valid;
    // Allows decoder/transition integration tests without sending audio to speakers.
    const char *testOutput = std::getenv("VELUNE_AUDIO_OUTPUT");
    if (testOutput && std::string(testOutput) == "null")
        valid = api.set_option_string(core, "ao", "null") >= 0 && valid;
    if (!valid || api.initialize(core) < 0) {
        api.terminate_destroy(core);
        emit(R"({"event":"error","message":"libmpv initialization failed."})");
        return 1;
    }
    for (auto name : {"time-pos", "duration", "cache-buffering-state", "speed", "volume"})
        api.observe_property(core, 0, name, MPV_FORMAT_DOUBLE);
    for (auto name : {"pause", "seekable"})
        api.observe_property(core, 0, name, MPV_FORMAT_FLAG);
    std::atomic<bool> running{true}, loaded{false}, wantsPlay{false};
    std::atomic<uint64_t> serial{0}, pendingSeek{0}, releaseCommand{0};
    std::thread input([&] {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.size() > 32768)
                break;
            std::istringstream parts(line);
            std::string op, value;
            std::getline(parts, op, '\t');
            std::getline(parts, value);
            int result = 0;
            if (op == "load") {
                loaded = false;
                wantsPlay = false;
                pendingSeek = 0;
                const auto source = unhex(value);
                if (source.empty())
                    break;
                const char *args[]{"loadfile", source.c_str(), "replace", nullptr};
                result = api.command_async(core, ++serial, args);
            } else if (op == "pause" || op == "release") {
                int paused = op == "release" || value == "1";
                wantsPlay = !paused;
                if (paused || (loaded && !pendingSeek)) {
                    const auto command = ++serial;
                    if (op == "release")
                        releaseCommand = command;
                    result = api.set_property_async(core, command, "pause", MPV_FORMAT_FLAG, &paused);
                }
            } else if (op == "seek" || op == "volume" || op == "speed") {
                double number;
                try {
                    number = std::stod(value);
                } catch (...) {
                    break;
                }
                if (!std::isfinite(number))
                    break;
                if (op == "seek") {
                    if (number < 0 || number > 9e12)
                        break;
                    const auto position = std::to_string(number / 1000.0);
                    const char *args[]{"seek", position.c_str(), "absolute+exact", nullptr};
                    const auto command = ++serial;
                    pendingSeek = command;
                    result = api.command_async(core, command, args);
                } else {
                    if ((op == "volume" && (number < 0 || number > 100)) ||
                        (op == "speed" && (number < .25 || number > 4)))
                        break;
                    result = api.set_property_async(core, ++serial, op.c_str(), MPV_FORMAT_DOUBLE, &number);
                }
            } else if (op == "quit")
                break;
            else
                break;
            if (result < 0)
                emit(R"({"event":"error","message":"libmpv rejected a playback command."})");
        }
        running = false;
        api.wakeup(core);
    });
    emit(R"({"event":"initialized"})");
    while (running) {
        auto *event = api.wait_event(core, .2);
        if (!event)
            continue;
        if (event->event_id == MPV_EVENT_FILE_LOADED) {
            loaded = true;
            emit(R"({"event":"ready"})");
        } else if (event->event_id == MPV_EVENT_PLAYBACK_RESTART)
            emit(R"({"event":"active"})");
        else if (event->event_id == MPV_EVENT_END_FILE) {
            auto *end = static_cast<mpv_event_end_file *>(event->data);
            if (end->reason == MPV_END_FILE_REASON_EOF)
                emit(R"({"event":"ended"})");
            else if (end->reason == MPV_END_FILE_REASON_ERROR)
                emit(R"({"event":"error","message":"Could not decode this audio source."})");
        } else if (event->event_id == MPV_EVENT_COMMAND_REPLY) {
            if (event->error < 0)
                emit(R"({"event":"error","message":"A native playback command failed."})");
            if (releaseCommand && event->reply_userdata == releaseCommand && event->error >= 0) {
                releaseCommand = 0;
                emit(R"({"event":"pause-confirmed"})");
            }
            if (pendingSeek && event->reply_userdata == pendingSeek) {
                pendingSeek = 0;
                if (wantsPlay && loaded) {
                    int pause = 0;
                    api.set_property_async(core, ++serial, "pause", MPV_FORMAT_FLAG, &pause);
                }
            }
        } else if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            auto *property = static_cast<mpv_event_property *>(event->data);
            if (!property->data)
                continue;
            std::ostringstream message;
            message << "{\"event\":\"property\",\"name\":\"" << property->name << "\",\"value\":";
            if (property->format == MPV_FORMAT_DOUBLE) {
                const double value = *static_cast<double *>(property->data);
                if (!std::isfinite(value))
                    continue;
                message << value;
            } else
                message << (*static_cast<int *>(property->data) ? "true" : "false");
            message << '}';
            emit(message.str());
        }
    }
    input.join();
    api.terminate_destroy(core);
    return 0;
}
