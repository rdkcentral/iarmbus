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
# List of Libraries
lib_core := core/
install_dir := ./install
install_bin_dir := ./install/bin

exe_template        := template/
exe_test            := test/
exe_iarm_daemon     := core/IARMDaemonMain

# List of Executable
libraries := $(lib_core)
executable := $(exe_template) $(exe_test) 

.PHONY: clean all $(libraries) install

all: clean $(libraries) $(executable) install

$(libraries) $(executable):
	$(MAKE) -C $@

$(executable): $(libraries)

install:
	echo "Creating directory.."
	mkdir -p $(install_dir)
	mkdir -p $(install_bin_dir)
	echo "Copying files now..from $(lib_core) to $(install_dir)"
	cp $(lib_core)/*.so $(install_dir)
	echo "Copying files now..from $(exe_iarm_daemon) to $(install_bin_dir)"
	cp $(exe_iarm_daemon) $(install_bin_dir)
	mkdir -p $(FSROOT)/usr/local/lib
	cp $(lib_core)/*.so $(FSROOT)/usr/local/lib
clean:
	rm -rf $(install_dir) 

