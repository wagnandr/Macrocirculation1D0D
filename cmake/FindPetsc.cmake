# Copyright (c) 2019    Prashant K. Jha
#
# Distributed under the GNU GENERAL PUBLIC LICENSE, Version 3.0.
# (See accompanying file LICENSE.txt)
find_package(PkgConfig)

set(PETSC_DIR $ENV{PETSC_DIR} CACHE PATH "Petsc installation directory")
set(ENV{PKG_CONFIG_PATH} "${PETSC_DIR}/lib/pkgconfig")

find_library(PETSC_LIBRARIES
        NAMES libpetsc.so libpetsc.dylib
        HINTS "${PETSC_DIR}/lib/")
#        HINTS /usr/lib64 /usr/local/lib64 /usr/lib/ /usr/local/lib
#"${PETSC_DIR}/lib/")


if (NOT PETSC_LIBRARIES)
    message(FATAL_ERROR "PETSC_LIBRARIES Library not found: Specify the PETSC_DIR where petsc is located")
endif ()
