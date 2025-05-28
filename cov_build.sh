#!/bin/bash
set -x
set -e
WORKDIR=`pwd`
export ROOT=/usr
export INSTALL_DIR=${ROOT}/local
mkdir -p $INSTALL_DIR

cd $ROOT
#build log4c
echo "##### Building log4c module"
wget --no-check-certificate https://sourceforge.net/projects/log4c/files/log4c/1.2.4/log4c-1.2.4.tar.gz/download -O log4c-1.2.4.tar.gz
tar -xvf log4c-1.2.4.tar.gz
cd log4c-1.2.4
./configure
make clean && make && make install

echo "##### Building IARMBus module"
cd $WORKDIR

export GLIBS='-lglib-2.0 -lz'
export IARM_PATH="`readlink -m .`"
export ROOT_INC="/usr/lib/x86_64-linux-gnu"
export GLIB_INCLUDE_PATH="/usr/include/glib-2.0"
export GLIB_CONFIG_INCLUDE_PATH="${ROOT_INC}/glib-2.0/include"
export DBUS_INCLUDE_PATH="/usr/include/dbus-1.0"
export DBUS_CONFIG_INCLUDE_PATH="${ROOT_INC}/dbus-1.0/include"
CFLAGS="$CFLAGS -O2 -Wall -fPIC -I./include -I${GLIB_INCLUDE_PATH} -I${GLIB_CONFIG_INCLUDE_PATH} \
	-I${WORKDIR}/stubs \
	-I/usr/include \
	-I${DBUS_INCLUDE_PATH} \
	-I${DBUS_CONFIG_INCLUDE_PATH} \
	-I/usr/include/libsoup-2.4 \
	-I/usr/include/gssdp-1.0"
export CFLAGS
LDFLAGS="$LDFLAGS -Wl,-rpath, -L/usr/lib"
export LDFLAGS
export OPENSOURCE_BASE=${FSROOT}/usr
export CC="$CROSS_COMPILE-gcc $CFLAGS"
export CXX="$CROSS_COMPILE-g++ $CFLAGS $LDFLAGS"
export RDK_PLATFORM_SOC=standalone
export USE_DBUS=y

make

