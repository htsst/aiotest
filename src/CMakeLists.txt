
find_package (OpenMP)

add_executable (
  aiotest
  aiotest.cc
  )

target_link_libraries (
  aiotest
  gomp
  aio
  )
