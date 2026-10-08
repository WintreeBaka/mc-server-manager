set(MCSM_BACKEND_SOURCES
    src/main.cpp
    src/cli/ArgParser.cpp
    src/cli/CommandRouter.cpp
    src/core/AppPaths.cpp
    src/core/Archive.cpp
    src/core/JsonUtil.cpp
    src/core/Logger.cpp
    src/core/ProcessRunner.cpp
    src/core/StringUtil.cpp
    src/net/HttpClient.cpp
    src/net/VersionResolver.cpp
    src/docker/DockerManager.cpp
    src/docker/TemplateWriter.cpp
    src/java/JavaManager.cpp
    src/server/ServerRecord.cpp
    src/server/ServerStore.cpp
    src/server/ServerInstaller.cpp
    src/server/ServerLifecycle.cpp
    src/server/RconClient.cpp
    src/config/ConfigSchema.cpp
    src/config/ConfigManager.cpp
    src/backup/BackupService.cpp
    src/plugin/PluginManager.cpp
    src/plugin/PackageManager.cpp
    src/plugin/PluginRuntime.cpp
    src/schedule/Scheduler.cpp
)

set(MCSM_BACKEND_HEADERS
    src/cli/ArgParser.h
    src/cli/CommandRouter.h
    src/core/AppPaths.h
    src/core/Archive.h
    src/core/JsonUtil.h
    src/core/Logger.h
    src/core/ProcessRunner.h
    src/core/StringUtil.h
    src/net/HttpClient.h
    src/net/VersionResolver.h
    src/docker/DockerManager.h
    src/docker/TemplateWriter.h
    src/java/JavaManager.h
    src/server/ServerRecord.h
    src/server/ServerStore.h
    src/server/ServerInstaller.h
    src/server/ServerLifecycle.h
    src/server/RconClient.h
    src/config/ConfigSchema.h
    src/config/ConfigManager.h
    src/backup/BackupService.h
    src/plugin/PluginManager.h
    src/plugin/PackageManager.h
    src/plugin/PluginRuntime.h
    src/schedule/Scheduler.h
)

qt_add_executable(mcsm-cli
    ${MCSM_BACKEND_SOURCES}
    ${MCSM_BACKEND_HEADERS}
)

target_include_directories(mcsm-cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(mcsm-cli PRIVATE Qt6::Core Qt6::Network)

set_target_properties(mcsm-cli PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    OUTPUT_NAME mcsm-cli
)

install(TARGETS mcsm-cli RUNTIME DESTINATION bin)
