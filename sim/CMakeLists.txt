cmake_minimum_required(VERSION 3.16)
project(wisp-sim C)

set(CMAKE_C_STANDARD 11)

find_package(SDL2 REQUIRED)

# —— LVGL ——
# Preferimos a cópia que o build do firmware já baixou, porque aí o simulador e
# a placa rodam exatamente a mesma versão. Ela é gitignored, então num clone
# limpo caímos na v9.5.0 do upstream — mesma versão, origem diferente. Se esta
# configuração cair no `else`, a paridade é por número de versão e não por
# bytes; está anotado porque importa saber qual dos dois se está vendo.
set(LVGL_LOCAL "${CMAKE_CURRENT_SOURCE_DIR}/../firmware/managed_components/lvgl__lvgl")

# O CMake de desktop do LVGL liga demos, exemplos e ThorVG por padrão. Os três
# são peso morto aqui, e o ThorVG ainda é C++: este projeto declara apenas C,
# então deixá-lo ligado quebra a configuração antes de compilar qualquer coisa.
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS        OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)

if(EXISTS "${LVGL_LOCAL}/lvgl.h")
    message(STATUS "LVGL: cópia local do firmware (paridade exata)")
    add_subdirectory("${LVGL_LOCAL}" lvgl EXCLUDE_FROM_ALL)
else()
    message(STATUS "LVGL: buscando v9.5.0 do upstream")
    include(FetchContent)
    FetchContent_Declare(lvgl
        GIT_REPOSITORY https://github.com/lvgl/lvgl.git
        GIT_TAG v9.5.0)
    FetchContent_MakeAvailable(lvgl)
endif()

# O LVGL não procura o SDL por conta própria; o driver dele precisa dos headers
# e da lib como qualquer consumidor.
target_link_libraries(lvgl PUBLIC SDL2::SDL2)

add_executable(wisp-sim
    main.c
    cena.c
    captura.c
    relogio.c
    shim/esp_mmap_assets.c
    shim/esp_lv_decoder.c
    ../firmware/main/ui.c              # sem modificação de simulador
    ../firmware/main/mascote.c
    ../firmware/main/mascote_terminal.c
    ../firmware/main/mascote_pixel.c
    ../firmware/main/mascote_pixel_props.c
)

# A ORDEM IMPORTA. shim/ vem primeiro para que esp_log.h, bsp/esp-bsp.h,
# esp_mmap_assets.h e esp_lv_decoder.h resolvam para os nossos. É isso que
# permite compilar o ui.c do firmware sem um único #ifdef.
target_include_directories(wisp-sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/shim
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/main
)

target_link_libraries(wisp-sim PRIVATE lvgl SDL2::SDL2)
