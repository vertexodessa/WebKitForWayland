# - Try to find OpenWebRTC.
# Once done, this will define
#
#  OPENWEBRTC_FOUND - system has OpenWebRTC.
#  OPENWEBRTC_INCLUDE_DIRS - the OpenWebRTC include directories
#  OPENWEBRTC_LIBRARIES - link these to use OpenWebRTC.
#
# Copyright (C) 2015 Igalia S.L.
# Copyright (C) 2015 Metrological.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1.  Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND ITS CONTRIBUTORS ``AS
# IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR ITS
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

find_package(PkgConfig)
pkg_check_modules(PC_OPENWEBRTC openwebrtc-0.3 openwebrtc-gst-0.3)

if ("${PC_OPENWEBRTC_FOUND}")
    set(OPENWEBRTC_FOUND "${PC_OPENWEBRTC_FOUND}")

    # Find Include paths
    set(OPENWEBRTC_INCLUDE_DIRS "${PC_OPENWEBRTC_INCLUDE_DIRS}")
    find_path(OWR_INCLUDE_DIR NAMES owr.h
        HINTS ${PC_OPENWEBRTC_INCLUDE_DIRS}
        PATH_SUFFIXES owr
    )
    list(APPEND OPENWEBRTC_INCLUDE_DIRS "${OWR_INCLUDE_DIR}" )    
    
    find_path(OPENWEBRTC_GST_INCLUDE_DIR NAMES gst.h 
        HINTS ${PC_OPENWEBRTC_INCLUDE_DIRS}
        PATH_SUFFIXES gst
    )
    list(APPEND OPENWEBRTC_INCLUDE_DIRS "${OPENWEBRTC_GST_INCLUDE_DIR}" )

    # Find Libraries
    find_library(OPENWEBRTC_LIBRARIES NAMES openwebrtc
        HINTS ${PC_OPENWEBRTC_LIBRARY_DIRS} ${PC_OPENWEBRTC_LIBDIR}
    )

    find_library(OPENWEBRTC_GST_LIBRARIES NAMES openwebrtc_gst
        HINTS ${PC_OPENWEBRTC_LIBRARY_DIRS} ${PC_OPENWEBRTC_LIBDIR}
    )
    list(APPEND OPENWEBRTC_LIBRARIES "${OPENWEBRTC_GST_LIBRARIES}")   

    set(VERSION_OK TRUE)
    if (PC_OPENWEBRTC_VERSION)
        if (PC_OPENWEBRTC_FIND_VERSION_EXACT)
            if (NOT("${PC_OPENWEBRTC_FIND_VERSION}" VERSION_EQUAL "${PC_OPENWEBRTC_VERSION}"))
                set(VERSION_OK FALSE)
            endif ()
        else ()
            if ("${PC_OPENWEBRTC_VERSION}" VERSION_LESS "${PC_OPENWEBRTC_FIND_VERSION}")
                set(VERSION_OK FALSE)
            endif ()
        endif ()
    endif ()

endif ()

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(OPENWEBRTC DEFAULT_MSG OPENWEBRTC_INCLUDE_DIRS OPENWEBRTC_LIBRARIES VERSION_OK)
