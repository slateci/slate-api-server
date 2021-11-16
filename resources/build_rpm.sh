#!/bin/sh

set -xe

usage () {
  echo "Invalid arguments, must use: "
  echo "$0 (binary|source|all) [working_dir]"
  exit 1
}

PKG_NAME="slate-api-server-0.1.0"
PKG_SHORT_NAME="slate-api-server"
GIT_REPO="https://github.com/slateci/slate-api-server.git"
# might be able to use a later sdk version but better not to risk it
AWS_SDK_LOCATION="https://codeload.github.com/aws/aws-sdk-cpp/tar.gz/refs/tags/1.5.25"                                                                     
#AWS_SDK_LOCATION="https://github.com/aws/aws-sdk-cpp/archive/refs/tags/1.5.25.tar.gz" 
if [ -z "$1" ];
then
  usage 
fi

if [ "$1" == "all" ];
then
  RPMBUILD_COMMAND="-ba" # build all rpm types (source, binary)
elif [ "$1" == "binary" ];
then
  RPMBUILD_COMMAND="-bb" # build binary rpms
elif [ "$1" == "source" ];
then
  RPMBUILD_COMMAND="-bs" # build source rpms
else
  usage
fi

if [ -z  "${CMAKE_BINARY_DIR}" ]; 
then
  WORKDIR=$2
else 
  WORKDIR=${CMAKE_BINARY_DIR}
fi

if [ -z "$WORKDIR" ];
then
  echo "Must provide CMAKE_BINARY_DIR or pass in a working directory as an argument"
  exit 1
fi


mkdir -p "${WORKDIR}/SOURCES"

rm -rf "${WORKDIR}/SOURCES/${PKG_SHORT_NAME}" "${WORKDIR}/SOURCES/${PKG_NAME}.tar.gz"

# Get a clean copy of the source tree into a tarball
git clone "${GIT_REPO}" "${WORKDIR}/SOURCES/${PKG_SHORT_NAME}"
tar czf "${WORKDIR}/SOURCES/${PKG_NAME}.tar.gz" -C "${WORKDIR}/SOURCES"  --exclude "${PKG_SHORT_NAME}/\.*" "${PKG_SHORT_NAME}"

# Install aws sdk
# need to clone the git repo with recursive and then install the right tag
cur_dir=`pwd`
cd ${WORKDIR}/SOURCES/
git clone -b 1.9.145 https://github.com/aws/aws-sdk-cpp.git --recursive
cd aws-sdk-cpp
mv aws-sdk-cpp aws-sdk-cpp-1.9.145
tar cvzf aws-sdk-cpp-1.5.25.tar.gz aws-sdk-cpp-1.5.25
cd $cur_dir

# Exceuting rpmbuild
rpmbuild --define "_topdir ${WORKDIR}" $RPMBUILD_COMMAND "${WORKDIR}/SOURCES/${PKG_SHORT_NAME}/resources/rpm_specs/aws-sdk-cpp.spec"
rpmbuild --define "_topdir ${WORKDIR}" $RPMBUILD_COMMAND "${WORKDIR}/SOURCES/${PKG_SHORT_NAME}/resources/rpm_specs/slate-api-server.spec"
