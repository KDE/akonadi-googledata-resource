
set( googlecontactresource_SRCS
  googledataresource.cpp
  ${RESOURCE_AKONADI_SHARED_SOURCES}
)

kde4_add_ui_files( googlecontactresource_SRCS GoogleData.ui )
install( FILES googledataresource.desktop DESTINATION "${CMAKE_INSTALL_PREFIX}/share/akonadi/agents" )

kde4_add_kcfg_files(googlecontactresource_SRCS settings.kcfgc)
kcfg_generate_dbus_interface(${CMAKE_CURRENT_SOURCE_DIR}/googledataresource.kcfg org.kde.Akonadi.googledata.Settings)
qt4_add_dbus_adaptor(googlecontactresource_SRCS
  ${CMAKE_CURRENT_BINARY_DIR}/org.kde.Akonadi.googledata.Settings.xml settings.h Settings
)

kde4_add_executable(akonadi_googledata_resource RUN_UNINSTALLED ${googlecontactresource_SRCS})

target_link_libraries(akonadi_googledata_resource ${KDEPIMLIBS_AKONADI_LIBS} ${QT_QTCORE_LIBRARY} ${QT_QTDBUS_LIBRARY} ${KDE4_KDECORE_LIBS} ${KDE4_KIO_LIBS} ${KDEPIMLIBS_KABC_LIBS} ${LIBGCAL_LIBRARIES})

install(TARGETS akonadi_googledata_resource ${INSTALL_TARGETS_DEFAULT_ARGS})
