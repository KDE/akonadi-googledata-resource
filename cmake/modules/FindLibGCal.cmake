# - Find LibGCal
#
# Find the LibGCal includes and library
#
# This module defines:
#	LIBGCAL_INCLUDE_DIR
#	LIBGCAL_LIBRARIES
#	LIBGCAL_FOUND
#	LIBGCAL_LIBRARY
#
# Copyright (c) 2009 Mike Arthur <mike@mikearthur.co.uk>
find_path(LIBGCAL_INCLUDE_DIR gcal.h)

set(LIBGCAL_NAMES ${LIBGCAL_NAMES} gcal)
find_library(LIBGCAL_LIBRARY NAMES ${LIBGCAL_NAMES})

# handle the QUIETLY and REQUIRED arguments and set PNG_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibGCal DEFAULT_MSG LIBGCAL_LIBRARY LIBGCAL_INCLUDE_DIR)

if(LIBGCAL_FOUND)
	set(LIBGCAL_LIBRARIES ${LIBGCAL_LIBRARY})
	set(LIBGCAL_INCLUDE_DIRS ${LIB_GCAL_INCLUDE_DIR})
else()
	set(LIBGCAL_LIBRARIES)
	set(LIBGCAL_INCLUDE_DIRS)
endif()