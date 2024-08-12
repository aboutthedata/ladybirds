cmake_minimum_required(VERSION 3.20)
project(ladybirds)


find_package(Clang REQUIRED)
find_package(Lua 5.3 REQUIRED)

if(LLVM_VERSION_MAJOR LESS 15)
	message(FATAL_ERROR "Need CLang version 15 or later")
endif()

# Add path to LLVM modules
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}" ${CLANG_CMAKE_DIR})

# import LLVM CMake functions
include(AddLLVM)
include(AddClang)

include(CheckIncludeFile)
check_include_file(dlfcn.h HAVE_DLFCN)

add_clang_executable(ladybirds
    src/graph/itemset.cpp
    src/lua/luadump.cpp
    src/lua/luaenv.cpp
    src/lua/luaload.cpp
    src/lua/methodinterface.cpp
    src/lua/pass.cpp
    src/opt/bankassignment.cpp
    src/opt/cacheindexopt.cpp
    src/opt/ifaceassignment.cpp
    src/opt/insertionschedule.cpp
    src/opt/schedule.cpp
    src/parse/state-eval.cpp
    src/parse/annotatingrewriter.cpp
    src/parse/cinterface.cpp
    src/parse/clanghandler.cpp
    src/parse/clanghandler-kernelcallparser.cpp
    src/parse/clanghandler-metakernel.cpp
    src/parse/exprcmp.cpp
    src/passes/arraymerger.cpp
    src/passes/assignbanks.cpp
    src/passes/export.cpp
    src/passes/loadaccesses.cpp
    src/passes/loadcost.cpp
    src/passes/loadmapping.cpp
    src/passes/loadprojectinfo.cpp
    src/passes/mergeports.cpp
    src/passes/platform.cpp
    src/passes/populategroups.cpp
    src/passes/stupidbankassign.cpp
    src/passes/succmatrix.cpp
    src/passes/tasktoposort.cpp
    src/passes/tools.cpp
    src/passes/transient.cpp
    src/passes/bufferallocation.cpp
    src/passes/bufferpreallocation.cpp
    src/passes/parse.cpp
    src/spec/platform.cpp
    src/basetype.cpp
    src/cmdlineoptions.cpp
    src/dependency.cpp
    src/diophant.cpp
    src/kernel.cpp
    src/loadstore.cpp
    src/main.cpp
    src/metakernel.cpp
    src/metakernelseq.cpp
    src/msgui.cpp
    src/packet.cpp
    src/program.cpp
    src/range.cpp
    src/task.cpp
    src/taskgroup.cpp
    src/tools.cpp
    src/buffer.cpp
)

target_include_directories(ladybirds PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}/src/
                           ${LLVM_INCLUDE_DIRS}
                           ${CLANG_INCLUDE_DIRS}
                           ${LUA_INCLUDE_DIR})
target_link_libraries(ladybirds PRIVATE clang-cpp ${LUA_LIBRARIES})

set_target_properties(ladybirds PROPERTIES
                      CXX_STANDARD 14
                      CXX_STANDARD_REQUIRED on)

target_compile_definitions(ladybirds PRIVATE
                           ${LLVM_DEFINITIONS}
                           ${CLANG_DEFINITIONS}
                           CFG_HAVE_DLFCN=${HAVE_DLFCN})
target_compile_options(ladybirds PRIVATE
                       ${LLVM_COMPILE_FLAGS}
                       ${CLANG_COMPILE_FLAGS})

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/share/
                    ${CMAKE_CURRENT_BINARY_DIR}/lib/clang/${CLANG_VERSION}/include)
execute_process(COMMAND
                ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_SOURCE_DIR}/res ${CMAKE_CURRENT_BINARY_DIR}/share/ladybirds)
execute_process(COMMAND
                ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_SOURCE_DIR}/export/ladybirds.h
                                                   ${CMAKE_CURRENT_BINARY_DIR}/lib/clang/${CLANG_VERSION}/include/ladybirds.h)

install(TARGETS ladybirds DESTINATION bin/)
install(DIRECTORY res/ DESTINATION share/ladybirds/)

set(PACKAGE_NAME Ladybirds CACHE STRING "Package name to be displayed in package information when shipping")
set(CPACK_PACKAGE_NAME Ladybirds CACHE STRING "Package name to be displayed in package information when shipping")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Ladybirds" CACHE STRING "Package file name for shipping")
set(PACKAGE_STRING "${PACKAGE_NAME} using LLVM ${PACKAGE_VERSION}" CACHE STRING "Package name+version string to be displayed in package information when shipping")

set(CPACK_PACKAGE_VENDOR "tik.ee.ethz.ch" CACHE STRING "Package vendor to be displayed in package information when shipping")
set(CPACK_PACKAGE_CONTACT "tik.ee.ethz.ch" CACHE STRING "Package contact to be displayed in package information when shipping")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64" CACHE STRING "Package target architecture to be displayed in package information when shipping")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "The Ladybirds framework. Currently includes LLVM+Clang." 
                                        CACHE STRING "Package summary to be displayed in package information when shipping")
