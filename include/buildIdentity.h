#pragma once
// PlatformIO's pre-build script creates the public source identity. Native
// tests deliberately use inert labels instead of a previous firmware build.
#ifdef YOKIS_NATIVE_TEST
#define YOKIS_BUILD_ID "native-test"
#define YOKIS_BUILD_GIT_REVISION "native-test"
#define YOKIS_BUILD_SOURCE_SHA256 "native-test"
#else
#include "build_provenance.h"
#endif
