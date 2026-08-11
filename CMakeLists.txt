cmake_minimum_required(VERSION 3.16)

project(download_url LANGUAGES CXX)

set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Installation prefix")
set(CMAKE_INSTALL_LIBDIR "lib" CACHE STRING "Library installation directory")

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CURL REQUIRED)

add_executable(download_url
    download_url.cpp
)

target_link_libraries(download_url
    PRIVATE CURL::libcurl
)

install(
    TARGETS download_url
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/obs/service
)

install(
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/download_url.service
    DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/obs/service
)
