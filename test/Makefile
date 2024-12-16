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

INCLUDE     :=	-I.\
		-I./include	\
		-I$(IARM_PATH)/core/                            \
		-I$(IARM_PATH)/core/include                      \
		-I$(IARM_PATH)/template/include                 \
		-I$(DFB_ROOT)/usr/local/include/directfb 

CFLAGS      += $(INCLUDE)


LDFLAGS=-L$(IARM_PATH)/core/
LDFLAGS+=-L$(DFB_LIB) -L$(OPENSOURCE_BASE)/lib
ifeq ($(USE_DBUS),y)
FUSION_LIBS=-ldirect -lfusion -lpthread
LDFLAGS += -ldbus-1
else
CFLAGS      += -I$(FUSION_PATH)/src -I$(FUSION_PATH)/include
FUSION_LIBS=-ldirect -lfusiondale -lfusion  -lpthread
endif
LDFLAGS += $(FUSION_LIBS)
LDFLAGS+=-L$(GLIB_LIBRARY_PATH)/
LDFLAGS+=$(GLIBS)

LDFLAGS += -lIARMBus 

.PHONY: $(BINARIES)

BINARIES    := $(patsubst %.c,%,$(wildcard *.c))

all: clean $(BINARIES)  
	@echo "Build Finished...."

$(BINARIES):
	@echo "Building $@ ...."
	$(CXX) $(CFLAGS) $@.c -o $@ $(LDFLAGS)
	
clean:
	@echo "Cleaning the directory..."
	@$(RM) $(BINARIES)



