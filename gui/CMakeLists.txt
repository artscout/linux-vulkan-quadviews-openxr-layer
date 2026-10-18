find_package(Qt6 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)

add_executable(qv-vulkan-settings
  src/main.cpp src/mainwindow.cpp src/mainwindow.h src/preview.cpp src/preview.h
  src/layerinstall.cpp src/layerinstall.h)
target_link_libraries(qv-vulkan-settings PRIVATE qv_common Qt6::Widgets)

install(TARGETS qv-vulkan-settings RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES qv-vulkan-settings.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
