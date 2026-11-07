# Pruebas unitarias y de integracion (doctest). Las imagenes se generan en
# codigo (helpers.hpp), salvo las de golden/: imagenes reales de cada version del
# formato, que comprueban que las versiones nuevas sigan leyendolas.

add_executable(icpu_tests
    main.cpp
    test_core.cpp
    test_crypto.cpp
    test_codecs.cpp
    test_stego.cpp
    test_format.cpp
    test_robustness.cpp
    test_analysis.cpp
    # El analizador de argumentos de la CLI tambien se prueba aqui
    ${PROJECT_SOURCE_DIR}/apps/cli/args.cpp
)

target_include_directories(icpu_tests PRIVATE ${PROJECT_SOURCE_DIR}/apps/cli)
target_compile_definitions(icpu_tests PRIVATE ICPU_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
target_link_libraries(icpu_tests PRIVATE icpu_core icpu_doctest icpu_warnings)

# Pruebas del servidor de la GUI (solo si se compila la GUI)
if(TARGET icpu_gui_server)
    target_sources(icpu_tests PRIVATE test_gui.cpp)
    target_link_libraries(icpu_tests PRIVATE icpu_gui_server)
endif()

add_test(NAME icpu_tests COMMAND icpu_tests)
