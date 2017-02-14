# OpenVAS-libraries/base
# $Id$
# Description: Top-level cmake control for the base library.
#
# Authors:
# Matthew Mundell <matthew.mundell@greenbone.net>
#
# Copyright:
# Copyright (C) 2009,2010,2013 Greenbone Networks GmbH
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.

## Library


if (NOT MINGW)
  # FIXME: certain flags should be defined on the global level,
  # because they change the API (e.g. _FILE_OFFSET_BITS).
  set (CMAKE_C_FLAGS              "${CMAKE_C_FLAGS} -D_FILE_OFFSET_BITS=64 -Wall -Wextra -fPIC")
  set (CMAKE_C_FLAGS_RELEASE      "${CMAKE_C_FLAGS_RELEASE} ${HARDENING_FLAGS}")

  pkg_check_modules (ZLIB REQUIRED zlib)

  pkg_check_modules (GIO REQUIRED gio-2.0)

endif (NOT MINGW)

include_directories (${GLIB_INCLUDE_DIRS}
                     ${ZLIB_INCLUDE_DIRS}
                     ${LIBGVM_BASE_INCLUDE_DIRS}
                     ${REDIS_INCLUDE_DIRS})

add_definitions (-DOPENVAS_SYSCONF_DIR=\\\"${OPENVAS_SYSCONF_DIR}\\\")
add_definitions (-DOPENVAS_STATE_DIR=\\\"${OPENVAS_STATE_DIR}\\\")

# Link the test-hosts executable
# TODO: This is currently only built for BUILD_SHARED
if (BUILD_SHARED)
  add_executable (test-hosts test-hosts.c)
  target_link_libraries (test-hosts ${GLIB_LDFLAGS} ${LIBGVM_BASE_LDFLAGS} ${REDIS_LDFLAGS})
endif (BUILD_SHARED)

## End
