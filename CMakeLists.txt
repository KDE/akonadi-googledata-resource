project(googledata)

SET( CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules" )

# search packages used by KDE
find_package (KDE4 REQUIRED)
include(KDE4Defaults)
include(MacroLibrary)
include(MacroOptionalAddSubdirectory)
include(CheckIncludeFiles)

find_package(KdepimLibs REQUIRED)
macro_optional_find_package(Boost)
macro_log_feature(Boost_FOUND "boost" "Boost C++ Libraries" "http://www.boost.org" TRUE "1.33.1" "Required by Akonadi.")
if(NOT Boost_FOUND)
  macro_display_feature_log()
endif(NOT Boost_FOUND)

#libgcal implements google data protocols 2.0
FIND_PACKAGE(LibGCal REQUIRED)


find_program(XSLTPROC_EXECUTABLE xsltproc)
macro_log_feature(XSLTPROC_EXECUTABLE "xsltproc" "The command line XSLT processor from libxslt" "http://xmlsoft.org/XSLT/" FALSE "" "Needed for building Akonadi resources. Recommended.")

if (XSLTPROC_EXECUTABLE)
  # generates a D-Bus interface description from a KConfigXT file
  macro( kcfg_generate_dbus_interface _kcfg _name )
    add_custom_command(
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_name}.xml
      COMMAND ${XSLTPROC_EXECUTABLE} --stringparam interfaceName ${_name}
              ${KDEPIMLIBS_DATA_DIR}/akonadi-kde/kcfg2dbus.xsl
              ${_kcfg}
              > ${CMAKE_CURRENT_BINARY_DIR}/${_name}.xml
      DEPENDS ${KDEPIMLIBS_DATA_DIR}/akonadi-kde/kcfg2dbus.xsl
              ${_kcfg}
    )
  endmacro( kcfg_generate_dbus_interface )
endif (XSLTPROC_EXECUTABLE)

if(WIN32)
    set(LIB_INSTALL_DIR ${LIB_INSTALL_DIR}
                        RUNTIME DESTINATION ${BIN_INSTALL_DIR}
                        LIBRARY DESTINATION ${LIB_INSTALL_DIR}
                        ARCHIVE DESTINATION ${LIB_INSTALL_DIR} )
endif(WIN32)

set(KDE4_ICON_DIR ${KDE4_INSTALL_DIR}/share/icons)

include_directories(
    ${KDE4_INCLUDES} ${KDEPIMLIBS_INCLUDE_DIR} ${LIBGCAL_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIR}
)

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${KDE4_ENABLE_EXCEPTIONS}" )

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/shared
  ${CMAKE_CURRENT_BINARY_DIR}/shared
)

set( RESOURCE_AKONADI_SHARED_SOURCES
     ${CMAKE_CURRENT_SOURCE_DIR}/shared/googledata.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/shared/dlgGoogleDataConf.cpp
)

add_subdirectory(contacts)
add_subdirectory(calendar)
