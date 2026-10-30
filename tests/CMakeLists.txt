# Pruebas unitarias y de integracion (doctest). Las imagenes se generan en
# codigo (helpers.hpp): no hay ficheros binarios de prueba en el repositorio.

add_executable(icpu_tests
    main.cpp
    test_core.cpp
    test_crypto.cpp
    test_codecs.cpp
    test_stego.cpp
    test_analysis.cpp
    # El analizador de argumentos de la CLI tambien se prueba aqui
    ${PROJECT_SOURCE_DIR}/apps/cli/args.cpp
)

target_include_directories(icpu_tests PRIVATE ${PROJECT_SOURCE_DIR}/apps/cli)
target_link_libraries(icpu_tests PRIVATE icpu_core icpu_doctest icpu_warnings)

add_test(NAME icpu_tests COMMAND icpu_tests)
