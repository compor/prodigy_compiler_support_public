# cmake file

set(PRJ_RT_NAME prefetcher_default_rt)

set(SOURCES ${PRJ_RT_NAME}.cpp)

add_library(${PRJ_RT_NAME} SHARED ${SOURCES})

target_include_directories(${PRJ_RT_NAME} PUBLIC "../../../sniper6.1/include")

install(
  TARGETS ${PRJ_RT_NAME}
  EXPORT ${PRJ_NAME}
  ARCHIVE DESTINATION "runtime/lib"
  LIBRARY DESTINATION "runtime/lib")
