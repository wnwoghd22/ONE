if (NOT BUILD_TRIX_LOADER)
  return()
endif ()

nnfw_find_package(TRIXEngine QUIET 2.5.0)
if(TRIXEngine_FOUND)
  list(APPEND SOURCES src/trix_loader.cc)
else()
  list(APPEND SOURCES src/trix_loader_dummy.cc)
endif(TRIXEngine_FOUND)

add_library(trix_loader STATIC ${SOURCES})
set_target_properties(trix_loader PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(trix_loader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(trix_loader PRIVATE onert_core)
target_link_libraries(trix_loader PRIVATE nnfw_common nnfw_coverage)

if(TRIXEngine_FOUND)
  target_include_directories(trix_loader PUBLIC ${TRIXEngine_INCLUDE_DIR})
  target_link_libraries(trix_loader PRIVATE trix_engine)
endif(TRIXEngine_FOUND)
