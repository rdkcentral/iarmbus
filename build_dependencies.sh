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