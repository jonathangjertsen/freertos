# The PreLoad.cmake feature is not documented or recommended,
# but it helps avoid having to write "-G Ninja" each time when invoking cmake.
#
# For more info, see:
#    https://gitlab.kitware.com/cmake/cmake/-/issues/17231
set(CMAKE_GENERATOR "Ninja" CACHE INTERNAL "" FORCE)
