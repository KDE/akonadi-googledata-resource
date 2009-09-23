
set( gcalresource_SRCS
  gcalresource.cpp
  ${RESOURCE_AKONADI_SHARED_SOURCES}
)

kde4_add_ui_files( gcalresource_SRCS GoogleData.ui )
install( FILES gcalresource.desktop DESTINATION "${CMAKE_INSTALL_PREFIX}/share/akonadi/agents" )

kde4_add_kcfg_files(gcalresource_SRCS settings.kcfgc)
kcfg_generate_dbus_interface(${CMAKE_CURRENT_SOURCE_DIR}/gcalresource.kcfg org.kde.Akonadi.gcal.Settings)
qt4_add_dbus_adaptor(gcalresource_SRCS
  ${CMAKE_CURRENT_BINARY_DIR}/org.kde.Akonadi.gcal.Settings.xml settings.h Settings
)

kde4_add_executable(akonadi_gcal_resource RUN_UNINSTALLED ${gcalresource_SRCS})

target_link_libraries(akonadi_gcal_resource ${KDEPIMLIBS_AKONADI_LIBS} ${QT_QTCORE_LIBRARY} ${QT_QTDBUS_LIBRARY} ${KDE4_KDECORE_LIBS} ${KDE4_KIO_LIBS} ${KDEPIMLIBS_KABC_LIBS} ${KDEPIMLIBS_KCAL_LIBS} ${LIBGCAL_LIBRARIES})

install(TARGETS akonadi_gcal_resource ${INSTALL_TARGETS_DEFAULT_ARGS})
