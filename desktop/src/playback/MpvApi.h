#pragma once
// Official headers are required only when the optional backend is compiled.
#include <QLibrary>
#include <memory>
#include <mpv/client.h>
namespace velune {
// Function table also permits deterministic native-event tests without audio hardware.
struct MpvApi {
    QLibrary library;
#define VELUNE_MPV_FUNCTIONS(X)                                                                              \
    X(client_api_version)                                                                                    \
    X(create) X(initialize) X(terminate_destroy) X(set_option_string) X(command_async) X(set_property_async) \
        X(observe_property) X(unobserve_property) X(wait_event) X(set_wakeup_callback)
#define DECLARE_MPV(name) decltype(&mpv_##name) name = nullptr;
    VELUNE_MPV_FUNCTIONS(DECLARE_MPV)
#undef DECLARE_MPV
    static std::shared_ptr<MpvApi> open(QString &error);
};
} // namespace velune
