# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/marwinka/Музыка/BurnhopeEngine/build-core/_deps/volk-src")
  file(MAKE_DIRECTORY "/home/marwinka/Музыка/BurnhopeEngine/build-core/_deps/volk-src")
endif()
file(MAKE_DIRECTORY
  "/home/marwinka/Музыка/BurnhopeEngine/build-core/_deps/volk-build"
  "/home/marwinka/Музыка/BurnhopeEngine/build-core/_deps/volk-subbuild/volk-populate-prefix"
  "/home/marwinka/Музыка/BurnhopeEngine/build-core/_deps/volk-subbuild/volk-populate-prefix/tmp"
  "/home/marwinka/Музыка/BurnhopeEngine/build-core/_deps/volk-subbuild/volk-populate-prefix/src/volk-populate-stamp"
  "/home/marwinka/Музыка/BurnhopeEngine/build-core/_deps/volk-subbuild/volk-populate-prefix/src"
  "/home/marwinka/Музыка/BurnhopeEngine/build-core/_deps/volk-subbuild/volk-populate-prefix/src/volk-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/marwinka/Музыка/BurnhopeEngine/build-core/_deps/volk-subbuild/volk-populate-prefix/src/volk-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/marwinka/Музыка/BurnhopeEngine/build-core/_deps/volk-subbuild/volk-populate-prefix/src/volk-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
