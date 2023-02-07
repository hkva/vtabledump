cmake_minimum_required(VERSION 3.12.0)
project(vtabledump)

add_executable(vtabledump
    "${CMAKE_CURRENT_LIST_DIR}/vtabledump.cc"
    "${CMAKE_CURRENT_LIST_DIR}/thirdparty/llvm/lib/Demangle/ItaniumDemangle.cpp"
)
target_include_directories(vtabledump PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/thirdparty/llvm/include"
)
target_compile_features(vtabledump PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(vtabledump PRIVATE /W4)
else()
    target_compile_options(vtabledump PRIVATE -Wall)
endif()