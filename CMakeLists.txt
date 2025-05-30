cmake_minimum_required(VERSION 3.14)
set(PROJECT_NAME fuzzy)
project(${PROJECT_NAME} LANGUAGES CXX)

# Force C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use Clang++ if available
if(NOT CMAKE_CXX_COMPILER)
    find_program(CLANGXX clang++)
    if(CLANGXX)
        message(STATUS "Using Clang++: ${CLANGXX}")
        set(CMAKE_CXX_COMPILER ${CLANGXX} CACHE STRING "Clang++ compiler" FORCE)
    else()
        message(WARNING "Clang++ not found, using default compiler.")
    endif()
endif()

# Set optimization flags for Release build
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto")

# Path to installed Fuzzylite
set(EXTERNAL_DIR ${CMAKE_SOURCE_DIR}/external)
set(FL_INSTALL_DIR ${EXTERNAL_DIR}/fuzzylite-install)

# Download and build Fuzzylite if not already installed
if(NOT EXISTS ${FL_INSTALL_DIR})
    include(ExternalProject)
    ExternalProject_Add(fuzzylite_deb
        PREFIX ${CMAKE_BINARY_DIR}/fuzzylite
        GIT_REPOSITORY https://github.com/fuzzylite/fuzzylite.git
        GIT_TAG main
        UPDATE_COMMAND ""
        CMAKE_ARGS 
            -DCMAKE_INSTALL_PREFIX=${FL_INSTALL_DIR}
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        INSTALL_DIR ${FL_INSTALL_DIR}
        BUILD_COMMAND make -j ${PROCESSOR_COUNT}
    )
    set(FUZZYLITE_DEP fuzzylite_deb)
else()
    message(STATUS "Using existing Fuzzylite in ${FL_INSTALL_DIR}")
    set(FUZZYLITE_DEP "")
endif()


set(JSON_INSTALL_DIR ${EXTERNAL_DIR}/json-install)
set(JSON_SOURCE_DIR ${EXTERNAL_DIR}/json)
set(JSON_INCLUDE_DIR ${JSON_SOURCE_DIR}/single_include)
# Download and setup nlohmann/json (header-only, no build step)
if(NOT EXISTS ${JSON_INCLUDE_DIR})
    include(ExternalProject)
    ExternalProject_Add(nlohmann_json
        PREFIX ${CMAKE_BINARY_DIR}/nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG master
        UPDATE_COMMAND ""
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND ""
        SOURCE_DIR ${JSON_SOURCE_DIR}
    )
    set(NLOHMANN_DEP nlohmann_json)
else()
    message(STATUS "Using existing nlohmann/json in ${JSON_INCLUDE_DIR}")
    set(NLOHMANN_DEP "")
endif()

# Include directories
include_directories(${FL_INSTALL_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/incl)
include_directories(${JSON_INCLUDE_DIR})

# Library directories
link_directories(${FL_INSTALL_DIR}/lib)
link_directories(${NLOHMANN_INSTALL_DIR}/lib)
# Add executable
add_executable(${PROJECT_NAME} 
    src/main.cpp
    src/utils.cpp
    src/system_model.cpp)

# Dependency and linking
# Dependency and linking
if(FUZZYLITE_DEP)
    add_dependencies(${PROJECT_NAME} ${FUZZYLITE_DEP})
endif()

if(NLOHMANN_DEP)
    add_dependencies(${PROJECT_NAME} ${NLOHMANN_DEP})
endif()

target_link_libraries(${PROJECT_NAME} fuzzylite)
