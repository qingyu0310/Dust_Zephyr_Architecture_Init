# init/ 分层：startup（启动器 / 注册项）/ thread（线程模板与注册表）/ irq（中断分发）

target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/startup/Init_entry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/startup/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/irq/Irq_handlers.cpp
)

# 三个子目录都加到全局 include：业务线程仍然只写 #include "thread.hpp" / "Init_entry.hpp"
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/startup
    ${CMAKE_CURRENT_SOURCE_DIR}/thread
    ${CMAKE_CURRENT_SOURCE_DIR}/irq
)

zephyr_linker_sources(ROM_SECTIONS ../cmd/linker/dust_init.ld)
