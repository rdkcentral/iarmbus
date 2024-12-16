##########################################################################
# If not stated otherwise in this file or this component's LICENSE
# file the following copyright and licenses apply:
#
# Copyright 2016 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
##########################################################################

SUBDIRS =
DIST_SUBDIRS =

SOLIBLDFLAGS = -ldbus-1 -lglib-2.0

INCLUDE_FILES = -I=$(includedir)/directfb \
                -I=$(includedir)/glib-2.0 \
                -I=$(libdir)/glib-2.0/include \
                -I$(abs_top_srcdir)/core/include \
                -I=$(includedir)/dbus-1.0 \
                -I=$(libdir)/dbus-1.0/include

CC = $(CXX)

AM_CPPFLAGS = $(INCLUDE_FILES)

lib_LTLIBRARIES = libIARMBus.la
libIARMBus_la_SOURCES = libIARM-dbus.c libIBus-dbus.c libIBusDaemon-dbus.c
libIARMBus_la_CFLAGS = -D_USE_DBUS $(INCLUDE_FILES)

libIARMBus_la_LDFLAGS = $(SOLIBLDFLAGS)


bin_PROGRAMS = IARMDaemonMain

IARMDaemonMain_SOURCES = IARMDaemonMain-dbus.c
IARMDaemonMain_CFLAGS = $(INCLUDE_FILES)
IARMDaemonMain_LDADD = -L. -lIARMBus -lpthread -lglib-2.0 -ldbus-1
IARMDaemonMain_DEPENDENCIES = libIARMBus.la
