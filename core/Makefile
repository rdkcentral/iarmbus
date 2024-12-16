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
RM          := rm -rf 
CFLAGS      := -g -fPIC -D_REENTRANT -Wall
LIBNAME     := IARMBus
LIBNAMEFULL := lib$(LIBNAME).so
ifeq ($(USE_DBUS),y)
OBJS        := libIARM-dbus.o  libIBus-dbus.o  libIBusDaemon-dbus.o 
EXECUTABLE  := $(patsubst %-dbus.c,%,$(wildcard *Main-dbus.c))
else
OBJS        := libIARM.o  libIBus.o  libIBusDaemon.o 
EXECUTABLE  := $(patsubst %.c,%,$(wildcard *Main.c))
endif
INCLUDE = -I.\
	-I./include	\
	-I$(DFB_ROOT)/usr/local/include/directfb \
	-I$(GLIB_INCLUDE_PATH) \
	-I$(GLIB_CONFIG_INCLUDE_PATH)/include/	

ifeq ($(RDK_LOGGER_ENABLED),y)

INCLUDE += -I$(COMBINED_ROOT)/rdklogger/include 
LDFLAGS+= -L$(COMBINED_ROOT)/rdklogger/build/lib -lrdkloggers
CFLAGS      += -DRDK_LOGGER_ENABLED 
endif

CFLAGS      += $(INCLUDE)

ifeq ($(USE_DBUS),y)
LDFLAGS += -L. -lpthread -ldbus-1
CFLAGS += -D_USE_DBUS

ifeq ($(RDK_PLATFORM_SOC),intel)
CFLAGS += -I$(TOOLCHAIN_DIR)/usr/include/dbus-1.0 -I$(TOOLCHAIN_DIR)/lib/dbus-1.0/include
else
CFLAGS += -I$(OPENSOURCE_BASE)/include/dbus-1.0 -I$(OPENSOURCE_BASE)/lib/dbus-1.0/include
endif

else #USE_DBUS
CFLAGS      += -I$(FUSION_PATH)/src -I$(FUSION_PATH)/include
FUSION_LIBS=-ldirect -lfusiondale -lfusion  -lpthread
endif

LDFLAGS+=-L$(DFB_LIB) -L$(OPENSOURCE_BASE)/lib
LDFLAGS += $(FUSION_LIBS)  
LDFLAGS+=-L$(GLIB_LIBRARY_PATH)/
LDFLAGS+= -L$(RDK_DIR)/sdk/fsroot/ramdisk/usr/local/lib/ -llog4c
LDFLAGS+=$(GLIBS)

all: clean library executable
	@echo "Build Finished...."

library: $(OBJS)
	@echo "Building $(LIBNAMEFULL) ...."
	$(CXX) $(OBJS) $(CFLAGS) $(LDFLAGS) -shared -o $(LIBNAMEFULL)

ifeq ($(USE_DBUS),y)
executable: IARMDaemonMain-dbus.o library
	$(CXX) $(OBJS) $(CFLAGS) $(LDFLAGS) IARMDaemonMain-dbus.o -o $(EXECUTABLE)
else
executable: IARMDaemonMain.o library
	$(CXX) $(OBJS) $(CFLAGS) $(LDFLAGS) IARMDaemonMain.o -o $(EXECUTABLE)
endif
%.o: %.c
	@echo "Building $@ ...."
	$(CXX) -c $<  $(CFLAGS) -o $@

clean:
	@echo "Cleaning the directory..."
	@$(RM) $(OBJS) $(LIBNAMEFULL)
