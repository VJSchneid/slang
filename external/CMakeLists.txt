# ~~~
# SPDX-FileCopyrightText: Michael Popoloski
# SPDX-License-Identifier: MIT
# ~~~

set(find_pkg_args "")
if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.24.0")
  set(find_pkg_args "FIND_PACKAGE_ARGS" "10.0.0")
endif()

FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 10.0.0
  GIT_SHALLOW ON
  ${find_pkg_args})

# Force fmtlib to always be a static lib if we pull it via FetchContent (i.e.
# not intercepted by find_package), since it's a private dependency that's
# mostly header-only already.
set(saved_build_shared_libs ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(fmt)
set(BUILD_SHARED_LIBS ${saved_build_shared_libs})

if((SLANG_INCLUDE_PYLIB OR BUILD_SHARED_LIBS) AND NOT fmt_FOUND)
  set_target_properties(fmt PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(fmt_FOUND)
  get_target_property(FMT_CFG fmt::fmt IMPORTED_CONFIGURATIONS)
  get_target_property(FMT_LIB fmt::fmt IMPORTED_LOCATION_${FMT_CFG})
  get_target_property(FMT_INC fmt::fmt INTERFACE_INCLUDE_DIRECTORIES)
  message(STATUS "Found system fmt library: ${FMT_LIB}")
  message(STATUS "Using system fmt include: ${FMT_INC}")
else()
  message(STATUS "Using remote fmt library")
endif()

find_package(Boost 1.82.0 QUIET)
if(NOT Boost_FOUND)
  message(STATUS "Using vendored boost_unordered header")

  add_library(boost_unordered INTERFACE)
  add_library(Boost::headers ALIAS boost_unordered)
  target_compile_definitions(boost_unordered
                             INTERFACE SLANG_BOOST_SINGLE_HEADER)
else()
  message(
    STATUS "Found system boost ${Boost_VERSION_STRING} at ${Boost_INCLUDE_DIRS}"
  )
endif()

if(SLANG_USE_MIMALLOC)
  if(CMAKE_SYSTEM_NAME MATCHES "Windows" AND BUILD_SHARED_LIBS)
    message(
      FATAL_ERROR "mimalloc cannot be used with Windows shared lib builds")
  endif()

  set(find_pkg_args "")
  if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.24.0")
    set(find_pkg_args "FIND_PACKAGE_ARGS" "2.1")
  endif()

  set(MI_OVERRIDE
      OFF
      CACHE INTERNAL "")
  set(MI_BUILD_SHARED
      OFF
      CACHE INTERNAL "")
  set(MI_BUILD_OBJECT
      OFF
      CACHE INTERNAL "")
  set(MI_BUILD_TESTS
      OFF
      CACHE INTERNAL "")
  set(MI_SKIP_COLLECT_ON_EXIT
      ON
      CACHE INTERNAL "")

  FetchContent_Declare(
    mimalloc
    GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
    GIT_TAG v2.1.2
    GIT_SHALLOW ON
    ${find_pkg_args})
  FetchContent_MakeAvailable(mimalloc)

  if(mimalloc_FOUND)
    message(STATUS "Found system mimalloc version: ${Catch2_VERSION}")
  else()
    message(STATUS "Using remote mimalloc library")
    if(IS_DIRECTORY "${mimalloc_SOURCE_DIR}")
      set_property(DIRECTORY ${mimalloc_SOURCE_DIR} PROPERTY EXCLUDE_FROM_ALL
                                                             YES)

      get_target_property(lib_include_dirs mimalloc-static INCLUDE_DIRECTORIES)
      target_include_directories(mimalloc-static SYSTEM
                                 PUBLIC ${lib_include_dirs})
    endif()
  endif()
endif()

if(SLANG_INCLUDE_TESTS)
  set(find_pkg_args "")
  if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.24.0")
    set(find_pkg_args "FIND_PACKAGE_ARGS" "3.0.0")
  endif()

  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.3.2
    GIT_SHALLOW ON
    ${find_pkg_args})
  FetchContent_MakeAvailable(Catch2)

  if(Catch2_FOUND)
    get_target_property(Catch2_INCLUDE_DIR Catch2::Catch2
                        INTERFACE_INCLUDE_DIRECTORIES)
    message(STATUS "Found system Catch2 version: ${Catch2_VERSION}")
    message(STATUS "Using system Catch2 include: ${Catch2_INCLUDE_DIR}")
  else()
    message(STATUS "Using remote Catch2 library")
  endif()
endif()

if(SLANG_INCLUDE_INSTALL)
  install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/external/ieee1800/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ieee1800
    COMPONENT slang_Development)

  if(NOT fmt_FOUND)
    install(
      TARGETS fmt
      EXPORT slangTargets
      LIBRARY COMPONENT slang_Development
      ARCHIVE COMPONENT slang_Development
      PUBLIC_HEADER EXCLUDE_FROM_ALL
      PRIVATE_HEADER EXCLUDE_FROM_ALL)
    install(
      DIRECTORY ${fmt_SOURCE_DIR}/include/fmt
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
      COMPONENT slang_Development)
  endif()

  if(NOT Boost_FOUND)
    install(
      TARGETS boost_unordered
      EXPORT slangTargets
      COMPONENT slang_Development)
    install(
      FILES ${PROJECT_SOURCE_DIR}/external/boost_unordered.hpp
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
      COMPONENT slang_Development)
  endif()

  if(SLANG_USE_MIMALLOC AND NOT mimalloc_FOUND)
    install(
      TARGETS mimalloc-static
      EXPORT slangTargets
      COMPONENT slang_Development)
    install(
      DIRECTORY ${mimalloc_SOURCE_DIR}/include/
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
      COMPONENT slang_Development)
  endif()
endif()
