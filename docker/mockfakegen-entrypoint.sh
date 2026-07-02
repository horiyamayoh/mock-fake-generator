#!/bin/sh
set -eu

if [ -z "${MOCKFAKEGEN_GMOCK_INCLUDE_DIRS:-}" ]; then
	export MOCKFAKEGEN_GMOCK_INCLUDE_DIRS="/usr/include"
fi

if [ -z "${MOCKFAKEGEN_GMOCK_LINK_FILES:-}" ]; then
	gmock_lib="$(find /usr/lib -name libgmock.a -print -quit 2>/dev/null || true)"
	gtest_lib="$(find /usr/lib -name libgtest.a -print -quit 2>/dev/null || true)"
	if [ -n "${gmock_lib}" ] && [ -n "${gtest_lib}" ]; then
		export MOCKFAKEGEN_GMOCK_LINK_FILES="${gmock_lib}|${gtest_lib}"
	fi
fi

exec /opt/mockfakegen/bin/mockfakegen "$@"
