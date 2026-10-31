# Interfaz de linea de comandos: icpu embed | extract | info | analyze | bench | formats

add_executable(icpu
    main.cpp
    args.cpp
    common.cpp
    console.cpp
    cmd_embed.cpp
    cmd_extract.cpp
    cmd_info.cpp
    cmd_analyze.cpp
    cmd_bench.cpp
    help.cpp
    guided.cpp
)

target_link_libraries(icpu PRIVATE icpu_core icpu_warnings)
if(WIN32)
    # CommandLineToArgvW (argumentos en UTF-8)
    target_link_libraries(icpu PRIVATE shell32)
endif()
