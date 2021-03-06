#!/bin/sh

SRC_DIR="$1"
BIN_DIR="$2"
ORIG_DIR=$(pwd)

VERSION="unknown version"

if which svnversion > /dev/null 2>&1; then
	VERSION=$(svnversion "${SRC_DIR}")
	if [ "$VERSION" = "Unversioned directory" ]; then
		VERSION="unknown version"
	else
		DONE=1
	fi
fi
if [ -z ${DONE} ]; then
	cd "${SRC_DIR}" # git is retarded and cannot be pointed to other directories
	if which git > /dev/null 2>&1; then
		VERSION=$(git rev-parse HEAD)
		if [ -z "$VERSION" ]; then
			VERSION="unknown version"
		else
			DONE=1
		fi
	fi
fi
echo '#define serverVersionString "'${VERSION}'"' > "${BIN_DIR}/"server_version.h
