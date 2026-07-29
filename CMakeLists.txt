target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Irq_handlers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Init_entry.cpp
)

zephyr_linker_sources(ROM_SECTIONS ../cmd/linker/tflm_init.ld)
