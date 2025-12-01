cmake_minimum_required(VERSION 3.16)
project(SchoolBot)
set(CMAKE_CXX_STANDARD 17)

if(EMSCRIPTEN)
    set(BUILD_WEB ON)
    add_definitions(-D_WEB_BUILD)
else()
    set(BUILD_DESKTOP ON)
endif()

set(IMGUI_DIR "imgui")
file(GLOB IMGUI_SOURCES 
    "${IMGUI_DIR}/imgui.cpp" "${IMGUI_DIR}/imgui_draw.cpp" 
    "${IMGUI_DIR}/imgui_tables.cpp" "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp" "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)

if(BUILD_DESKTOP)
    find_package(SDL2 REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(Boost REQUIRED COMPONENTS json)
    find_package(Threads REQUIRED)
endif()

add_executable(SchoolBot main.cpp ${IMGUI_SOURCES})
target_include_directories(SchoolBot PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends)

if(BUILD_WEB)
    target_compile_definitions(SchoolBot PRIVATE 
        BOOST_JSON_HEADER_ONLY
        BOOST_JSON_NO_LIB  
    )
    
    target_compile_options(SchoolBot PRIVATE 
        -sUSE_SDL=2 
        -sUSE_BOOST_HEADERS=1
    )
    
    target_link_options(SchoolBot PRIVATE 
        -sUSE_SDL=2 
        -sFETCH=1 
        -sALLOW_MEMORY_GROWTH=1
        -sUSE_BOOST_HEADERS=1
        --shell-file=${CMAKE_CURRENT_SOURCE_DIR}/imgui/examples/libs/emscripten/shell_minimal.html
    )
    
    set_target_properties(SchoolBot PROPERTIES SUFFIX ".html")
else()
    target_include_directories(SchoolBot PRIVATE 
        ${SDL2_INCLUDE_DIRS} 
        ${Boost_INCLUDE_DIRS} 
        ${OPENSSL_INCLUDE_DIR}
    )
    
    target_link_libraries(SchoolBot PRIVATE 
        ${SDL2_LIBRARIES} 
        OpenGL::GL 
        OpenSSL::SSL 
        OpenSSL::Crypto 
        Threads::Threads
        Boost::json  
    )
    
    if(WIN32)
        target_link_libraries(SchoolBot PRIVATE ws2_32 crypt32)
    endif()
endif()