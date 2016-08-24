
find_package (OpenMP)

#include_directories (
#  
#  )

add_executable (
  aiotest
  aiotest.cc
  )

target_link_libraries (
  aiotest
  omp
  aio
  )
