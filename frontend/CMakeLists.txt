set(MCSM_GUI_SOURCES
    src/main.cpp
    src/app/AppContext.cpp
    src/app/BackendClient.cpp
    src/app/ThemeManager.cpp
    src/app/Easing.cpp
    src/model/ServerModel.cpp
    src/model/ServerTypes.cpp
    src/ui/MainWindow.cpp
    src/ui/TitleBar.cpp
    src/ui/Sidebar.cpp
    src/ui/ToastHost.cpp
    src/ui/FramelessHelper.cpp
    src/widgets/Common.cpp
    src/widgets/AnimatedStack.cpp
    src/widgets/LogView.cpp
    src/widgets/Chip.cpp
    src/pages/DashboardPage.cpp
    src/pages/ServersPage.cpp
    src/pages/ConsolePage.cpp
    src/pages/ConfigPage.cpp
    src/pages/BackupPage.cpp
    src/pages/PluginPage.cpp
    src/pages/SettingsPage.cpp
    src/pages/CreateServerDialog.cpp
)

set(MCSM_GUI_HEADERS
    src/app/AppContext.h
    src/app/BackendClient.h
    src/app/ThemeManager.h
    src/app/Easing.h
    src/model/ServerModel.h
    src/model/ServerTypes.h
    src/ui/MainWindow.h
    src/ui/TitleBar.h
    src/ui/Sidebar.h
    src/ui/ToastHost.h
    src/ui/FramelessHelper.h
    src/widgets/Common.h
    src/widgets/AnimatedStack.h
    src/widgets/LogView.h
    src/widgets/Chip.h
    src/pages/DashboardPage.h
    src/pages/ServersPage.h
    src/pages/ConsolePage.h
    src/pages/ConfigPage.h
    src/pages/BackupPage.h
    src/pages/PluginPage.h
    src/pages/SettingsPage.h
    src/pages/CreateServerDialog.h
)

qt_add_executable(McServerManager
    ${MCSM_GUI_SOURCES}
    ${MCSM_GUI_HEADERS}
)

target_include_directories(McServerManager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(McServerManager PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

set_target_properties(McServerManager PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE OFF
    OUTPUT_NAME McServerManager
)

install(TARGETS McServerManager RUNTIME DESTINATION bin)
