cmake_minimum_required(VERSION 3.21)
project(VeluneDesktop VERSION 0.3.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 WebEngineQuick)
find_package(OpenSSL REQUIRED)
find_package(Qt6Keychain REQUIRED)
qt_standard_project_setup()
add_subdirectory(native)
add_library(velune_core
 src/core/Library.cpp
 src/playback/IPlaybackEngine.h src/playback/ProcessPlaybackEngine.cpp
 src/playback/PlaybackEngineFactory.cpp src/playback/PlaybackTrace.cpp
 src/playback/CrossfadePlaybackEngine.cpp
 src/services/ConnectClient.cpp src/services/ConnectHost.cpp src/services/ConnectMediaCache.cpp
 src/services/ConnectMediaProvider.cpp
 src/services/Discovery.cpp src/services/EmbeddedSession.cpp
 src/services/LibraryCatalog.cpp src/services/SearchEngine.cpp
 src/services/MusicService.cpp src/services/MusicServiceConnect.cpp
 src/services/AudioResolver.cpp src/services/NativeAudioResolver.cpp src/services/AudioStream.cpp
 src/services/DownloadService.cpp src/services/Mp3Tags.cpp
 src/services/CacheStorage.cpp
 src/services/BackupService.cpp
)
target_include_directories(velune_core PUBLIC src)
target_compile_definitions(velune_core PRIVATE VELUNE_KEYCHAIN)
target_link_libraries(velune_core PUBLIC Qt6::Gui Qt6::Network OpenSSL::Crypto PRIVATE qt6keychain)
qt_add_executable(velune src/main.cpp src/DesktopController.cpp)
target_link_libraries(velune PRIVATE velune_core Qt6::Quick Qt6::QuickControls2)
qt_add_qml_module(velune URI Velune VERSION 1.0 RESOURCE_PREFIX / QML_FILES
 qml/Main.qml qml/ActionButton.qml qml/TrackRow.qml qml/Artwork.qml
 qml/SettingsPage.qml qml/SettingsGroup.qml qml/SettingsToggle.qml qml/SettingsChoice.qml)
qt_add_executable(velune-login src/login/main.cpp)
target_link_libraries(velune-login PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::WebEngineQuick)
qt_add_qml_module(velune-login URI VeluneLogin VERSION 1.0 RESOURCE_PREFIX / QML_FILES qml/Login.qml)
qt_add_resources(velune assets PREFIX / FILES assets/velune.svg)
install(TARGETS velune velune-login RUNTIME DESTINATION bin)
option(VELUNE_DOWNLOAD_TESTS "Build downloads and Android MP3 compatibility tests" OFF)
if(VELUNE_DOWNLOAD_TESTS)
 find_package(Qt6 REQUIRED COMPONENTS Test)
 enable_testing()
 qt_add_executable(velune-connect-tests tests/ConnectHandoverTests.cpp)
 target_link_libraries(velune-connect-tests PRIVATE velune_core Qt6::Test)
 add_test(NAME connect-handover COMMAND velune-connect-tests)
 qt_add_executable(velune-download-tests tests/DownloadFeatureTests.cpp)
 target_link_libraries(velune-download-tests PRIVATE velune_core Qt6::Test)
 add_test(NAME downloads COMMAND velune-download-tests)
 qt_add_executable(velune-resume-tests tests/PlaybackResumeTests.cpp)
 target_link_libraries(velune-resume-tests PRIVATE velune_core Qt6::Test)
 add_test(NAME playback-resume COMMAND velune-resume-tests)
 qt_add_executable(velune-storage-tests tests/StorageFeatureTests.cpp)
 target_link_libraries(velune-storage-tests PRIVATE velune_core Qt6::Test)
 add_test(NAME storage COMMAND velune-storage-tests)
 qt_add_executable(velune-crossfade-tests tests/CrossfadeTests.cpp)
 target_link_libraries(velune-crossfade-tests PRIVATE velune_core Qt6::Test)
 add_test(NAME crossfade COMMAND velune-crossfade-tests)
 qt_add_executable(velune-settings-ui-tests tests/SettingsUiTests.cpp)
 target_link_libraries(velune-settings-ui-tests PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Test)
 target_compile_definitions(velune-settings-ui-tests PRIVATE VELUNE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
 add_test(NAME settings-ui COMMAND velune-settings-ui-tests)
 set_tests_properties(settings-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic")
endif()
