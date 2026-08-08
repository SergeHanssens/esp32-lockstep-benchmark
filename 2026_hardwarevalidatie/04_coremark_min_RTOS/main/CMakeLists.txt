idf_component_register(
    SRCS "coremark_wrapper.c" "core_portme.c"
         "core_main.c" "core_list_join.c" "core_matrix.c" "core_state.c" "core_util.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_timer esp_hw_support)

# EEMBC-vereiste: compilerflags rapporteren in de output
target_compile_definitions(${COMPONENT_LIB} PRIVATE "FLAGS_STR=\"-O2 ESP-IDF v5.5\"")
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wno-error=format= -Wno-format)

# core_main.c definieert main(); hernoem naar coremark_main zodat de wrapper hem aanroept
set_source_files_properties(core_main.c PROPERTIES COMPILE_FLAGS "-Dmain=coremark_main")
