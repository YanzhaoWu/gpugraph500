#!/bin/bash

# Installs score-P on $HOME.


function exit_error {
  local error=$1
  if [ $error -neq 0 ]; then
    echo "Error detected. Quitting..."
    exit 1
 fi
}

function banner {
  local text="$1"
  echo
  echo "============================================================================"
  echo "== $text"
  echo "============================================================================"
}

function section_banner {
  local text="$1"
  echo
  echo "--- $text"
}

function install {

  export LD_LIBRARY_PATH=$apps/openmpi/lib64:$LD_LIBRARY_PATH
  export PATH=$apps/openmpi/bin:$PATH
  export OMPI_CC=$cc
  export OMPI_CXX=$cxx

  mkdir -p $apps &> /dev/null

  banner "OpenMPI"
  if [ ! -f ${openmpi}.tar.gz ]; then
    section_banner "Downloading"
    wget http://www.open-mpi.org/software/ompi/v1.8/downloads/${openmpi}.tar.gz
    res=$?
    exit_error $res
  fi
  if [ ! -d ${openmpi} ]; then
    section_banner "Decompressing"
    rm -rf ${openmpi}
    tar -xzvf ${openmpi}.tar.gz
  fi
  cd ${openmpi}
  section_banner "Checking"
  ./configure CC=$cc CXX=$cxx  --prefix=$apps/openmpi --with-cuda=$cuda_dir
  res=$?
  exit_error $res
  section_banner "Making"
  make clean
  make -j 12
  res=$?
  exit_error $res
  section_banner "Installing"
  make install
  res=$?
  exit_error $res
  cd ..

  banner "Opari"
  if [ ! -f  ${opari}.tar.gz ]; then
    section_banner "Downloading"
    wget http://www.vi-hps.org/upload/packages/opari2/${opari}.tar.gz
    res=$?
    exit_error $res
  fi
  if [ ! -d  ${opari} ]; then
    section_banner "Decompressing"
    rm -rf ${opari}
    tar -xzvf ${opari}.tar.gz
  fi
  cd ${opari}
  section_banner "Checking"
  ./configure CC=$cc CXX=$cxx  --prefix=$apps/opari2
  res=$?
  exit_error $res
  section_banner "Making"
  make clean
  make -j 12
  res=$?
  exit_error $res
  section_banner "Installing"
  make install
  res=$?
  exit_error $res
  cd ..

  banner "Cube"
  if [ ! -f  ${cube}.tar.gz ]; then
    section_banner "Downloading"
    wget http://apps.fz-juelich.de/scalasca/releases/cube/4.2/dist/${cube}.tar.gz
    res=$?
    exit_error $res
  fi
  if [ ! -d  ${cube} ]; then
    section_banner "Decompressing"
    rm -rf ${cube}
    tar -xzvf ${cube}.tar.gz
  fi
  cd ${cube}
  section_banner "Checking"
  ./configure CC=$cc CXX=$cxx  --prefix=$apps/cube --without-gui
  res=$?
  exit_error $res
  section_banner "Making"
  make clean
  make -j 12
  res=$?
  exit_error $res
  section_banner "Installing"
  make install
  res=$?
  exit_error $res
  cd ..

  banner "Score-P"
  if [ ! -f  ${scorep}.tar.gz ]; then
    section_banner "Downloading"
  	wget http://www.vi-hps.org/upload/packages/scorep/${scorep}.tar.gz
    res=$?
    exit_error $res
  fi
  if [ ! -d  ${scorep} ]; then
    section_banner "Decompressing"
    rm -rf ${scorep}
    tar -xzvf ${scorep}.tar.gz
  fi
  cd ${scorep}
  section_banner "Checking"
  ./configure CC=$cc CXX=$cxx --prefix=$apps/score_p --with-cube=$apps/cube --with-opari2=$apps/opari2 --with-cuda=/usr/local/cuda
  res=$?
  exit_error $res
  section_banner "Making"
  make clean
  make -j 12
  res=$?
  exit_error $res
  section_banner "Installing"
  make install
  return $?
}

cxx=`locate bin/g++- | grep "bin/g++-[0-9]" | tail -1`
cc=`locate bin/gcc- | grep "bin/gcc-[0-9]" | tail -1`
nvcc=`locate bin/nvcc | grep bin/nvcc$$ | tail -1`
cuda_dir=`echo $nvcc | sed 's,/bin/nvcc$$,,'`
apps="$HOME/distlibs"
openmpi="openmpi-1.6.5"
opari="opari2-1.1.2"
cube="cube-4.2.3"
scorep="scorep-1.3"

install
exit $?

