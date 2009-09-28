project(googledata)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules" ${CMAKE_MODULE_PATH})

# search packages used by KDE
find_package (KDE4 REQUIRED)
include(KDE4Defaults)
include(MacroLibrary)
include(MacroOptionalAddSubdirectory)
include(CheckIncludeFiles)

find_package(KdepimLibs REQUIRED)

find_package(Boost)
macro_log_feature(Boost_FOUND "Boost" "Boost C++ Libraries" "http://www.boost.org" TRUE "1.33.1" "Required by Akonadi.")

#libgcal implements google data protocols 2.0
find_package(LibGCal)
macro_log_feature(LIBGCAL_FOUND "libgcal" "C library that allows communication with Google calendar and contacts" "http://code.google.com/p/libgcal/" TRUE "0.9.0" "Required to build the Googledata Akonadi resource.")


include_directories( ${KDE4_INCLUDES} ${KDEPIMLIBS_INCLUDE_DIR} ${LIBGCAL_INCLUDE_DIRS} ${Boost_INCLUDE_DIR} )

find_program(XSLTPROC_EXECUTABLE xsltproc REQUIRED)
macro_log_feature(XSLTPROC_EXECUTABLE "xsltproc" "The command line XSLT processor from libxslt" "http://xmlsoft.org/XSLT/" TRUE "" "Needed for building Akonadi resources.")

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

if(WIN32)
    set(LIB_INSTALL_DIR ${LIB_INSTALL_DIR}
                        RUNTIME DESTINATION ${BIN_INSTALL_DIR}
                        LIBRARY DESTINATION ${LIB_INSTALL_DIR}
                        ARCHIVE DESTINATION ${LIB_INSTALL_DIR} )
endif(WIN32)

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${KDE4_ENABLE_EXCEPTIONS}" )

include_directories( ${CMAKE_CURRENT_SOURCE_DIR}/shared )

set( RESOURCE_AKONADI_SHARED_SOURCES
     ${CMAKE_CURRENT_SOURCE_DIR}/shared/googledata.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/shared/dlgGoogleDataConf.cpp
)

add_subdirectory(contacts)
add_subdirectory(calendar)

macro_display_feature_log()

