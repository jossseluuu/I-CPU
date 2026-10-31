# Interfaz grafica: servidor HTTP local (solo 127.0.0.1) que sirve una
# aplicacion web embebida en el ejecutable y la abre en el navegador.

set(ICPU_WEB_FILES index.html app.css app.js favicon.svg)

set(web_deps "")
foreach(f IN LISTS ICPU_WEB_FILES)
    list(APPEND web_deps ${CMAKE_CURRENT_SOURCE_DIR}/web/${f})
endforeach()
string(REPLACE ";" "," web_files_arg "${ICPU_WEB_FILES}")

set(web_assets_cpp ${CMAKE_CURRENT_BINARY_DIR}/web_assets.cpp)
add_custom_command(
    OUTPUT ${web_assets_cpp}
    COMMAND ${CMAKE_COMMAND}
        -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/web
        -DFILES=${web_files_arg}
        -DOUT=${web_assets_cpp}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_assets.cmake
    DEPENDS ${web_deps} ${CMAKE_CURRENT_SOURCE_DIR}/embed_assets.cmake
    COMMENT "Embebiendo el frontend web"
    VERBATIM)

# El servidor va en una biblioteca para que las pruebas puedan arrancarlo en un hilo.
add_library(icpu_gui_server STATIC
    server.cpp
    ${web_assets_cpp}
)
target_include_directories(icpu_gui_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(icpu_gui_server PUBLIC icpu_core icpu_httplib PRIVATE icpu_warnings)
if(WIN32)
    # cpp-httplib exige Windows 10+; ShellExecuteA (abrir el navegador) esta en shell32
    target_compile_definitions(icpu_gui_server PUBLIC _WIN32_WINNT=0x0A00)
    target_link_libraries(icpu_gui_server PUBLIC shell32)
endif()

add_executable(icpu-gui main.cpp)
target_link_libraries(icpu-gui PRIVATE icpu_gui_server icpu_warnings)
