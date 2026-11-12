cmake_minimum_required(VERSION 3.20)
project(tsw_re C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(SDL3 3.2 CONFIG REQUIRED)
find_package(SDL3_image 3.0 CONFIG REQUIRED)
find_package(SDL3_ttf 3.0 CONFIG REQUIRED)

set(LUA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua)
set(LUA_SOURCES
    ${LUA_DIR}/lapi.c
    ${LUA_DIR}/lcode.c
    ${LUA_DIR}/lctype.c
    ${LUA_DIR}/ldebug.c
    ${LUA_DIR}/ldo.c
    ${LUA_DIR}/ldump.c
    ${LUA_DIR}/lfunc.c
    ${LUA_DIR}/lgc.c
    ${LUA_DIR}/llex.c
    ${LUA_DIR}/lmem.c
    ${LUA_DIR}/lobject.c
    ${LUA_DIR}/lopcodes.c
    ${LUA_DIR}/lparser.c
    ${LUA_DIR}/lstate.c
    ${LUA_DIR}/lstring.c
    ${LUA_DIR}/ltable.c
    ${LUA_DIR}/ltm.c
    ${LUA_DIR}/lundump.c
    ${LUA_DIR}/lvm.c
    ${LUA_DIR}/lzio.c
    ${LUA_DIR}/lauxlib.c
    ${LUA_DIR}/lbaselib.c
    ${LUA_DIR}/lcorolib.c
    ${LUA_DIR}/ldblib.c
    ${LUA_DIR}/liolib.c
    ${LUA_DIR}/lmathlib.c
    ${LUA_DIR}/loadlib.c
    ${LUA_DIR}/loslib.c
    ${LUA_DIR}/lstrlib.c
    ${LUA_DIR}/ltablib.c
    ${LUA_DIR}/lutf8lib.c
    ${LUA_DIR}/linit.c
)
add_library(lua54 STATIC ${LUA_SOURCES})
target_include_directories(lua54 PUBLIC ${LUA_DIR})
target_compile_definitions(lua54 PRIVATE LUA_USE_POSIX)

set(TSW_SOURCES
    src/main.c
    src/app.c
    src/state.c
    src/map.c
    src/save.c
    src/game.c
    src/items.c
    src/battle.c
    src/monsters.c
    src/assets.c
    src/render.c
    src/input.c
    src/ui.c
    src/log.c
    src/intro.c
    src/events.c
    src/event_vm.c
    src/event_builders.c
    src/event_engine.c
    src/messages.c
    src/replay.c
)

add_executable(tsw-re ${TSW_SOURCES})
target_include_directories(tsw-re PRIVATE src)
target_link_libraries(tsw-re PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
    lua54
    m
)
target_compile_options(tsw-re PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(tsw-re PRIVATE
    TSW_VERSION="0.1.0"
    TSW_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    _POSIX_C_SOURCE=200809L
)

include(CTest)
if(BUILD_TESTING)
    add_executable(test_state tests/test_state.c src/state.c src/map.c src/save.c)
    target_include_directories(test_state PRIVATE src)
    target_compile_options(test_state PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(test_state PRIVATE _POSIX_C_SOURCE=200809L)
    target_link_libraries(test_state PRIVATE m)
    add_test(NAME state COMMAND test_state)

    add_executable(test_intro tests/test_intro.c src/intro.c)
    target_include_directories(test_intro PRIVATE src)
    target_compile_options(test_intro PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME intro COMMAND test_intro)

    add_executable(test_game tests/test_game.c src/game.c src/items.c src/battle.c src/monsters.c src/state.c src/map.c src/save.c)
    target_include_directories(test_game PRIVATE src)
    target_compile_options(test_game PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(test_game PRIVATE _POSIX_C_SOURCE=200809L)
    target_link_libraries(test_game PRIVATE m)
    add_test(NAME game COMMAND test_game)

    # Movement code vs the original's handan (golden vectors action_*;
    # skipped when no vectors were exported).
    add_executable(test_game_parity tests/test_game_parity.c src/game.c
        src/items.c src/battle.c src/monsters.c src/state.c src/map.c src/save.c
        src/event_builders.c src/event_vm.c)
    target_include_directories(test_game_parity PRIVATE src tests)
    target_compile_options(test_game_parity PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(test_game_parity PRIVATE _POSIX_C_SOURCE=200809L)
    target_link_libraries(test_game_parity PRIVATE lua54 m)
    add_test(NAME game_parity COMMAND test_game_parity)

    add_executable(test_items
        tests/test_items.c
        src/game.c
        src/items.c
        src/battle.c
        src/monsters.c
        src/state.c
        src/map.c
        src/save.c
    )
    target_include_directories(test_items PRIVATE src)
    target_compile_options(test_items PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(test_items PRIVATE _POSIX_C_SOURCE=200809L)
    target_link_libraries(test_items PRIVATE m)
    add_test(NAME items COMMAND test_items)

    add_executable(test_battle
        tests/test_battle.c
        src/game.c
        src/items.c
        src/battle.c
        src/monsters.c
        src/state.c
        src/map.c
        src/save.c
    )
    target_include_directories(test_battle PRIVATE src)
    target_compile_options(test_battle PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(test_battle PRIVATE _POSIX_C_SOURCE=200809L)
    target_link_libraries(test_battle PRIVATE m)
    add_test(NAME battle COMMAND test_battle)

    add_executable(test_replay
        tests/test_replay.c
        src/replay.c
        src/game.c
        src/items.c
        src/battle.c
        src/monsters.c
        src/state.c
        src/map.c
        src/save.c
    )
    target_include_directories(test_replay PRIVATE src)
    target_compile_options(test_replay PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(test_replay PRIVATE _POSIX_C_SOURCE=200809L)
    target_link_libraries(test_replay PRIVATE m)
    add_test(NAME replay COMMAND test_replay)

    add_executable(test_event_vm tests/test_event_vm.c src/event_vm.c src/map.c src/state.c)
    target_include_directories(test_event_vm PRIVATE src)
    target_compile_options(test_event_vm PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(test_event_vm PRIVATE _POSIX_C_SOURCE=200809L)
    target_link_libraries(test_event_vm PRIVATE m)
    add_test(NAME event_vm COMMAND test_event_vm)

    add_executable(test_event_builders
        tests/test_event_builders.c
        src/event_builders.c
        src/event_engine.c
        src/messages.c
        src/event_vm.c
        src/monsters.c
        src/map.c
        src/state.c
    )
    target_include_directories(test_event_builders PRIVATE src tests)
    target_compile_options(test_event_builders PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(test_event_builders PRIVATE _POSIX_C_SOURCE=200809L
        TSW_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    target_link_libraries(test_event_builders PRIVATE lua54 m)
    # Part 2 (golden-vector parity) reads $TSW_EVENT_VECTORS or
    # ~/.cache/tsw/vectors; it is skipped when no vectors were exported.
    add_test(NAME event_builders COMMAND test_event_builders
        "${CMAKE_CURRENT_SOURCE_DIR}/src/events/builders.lua")

    add_executable(test_event_engine
        tests/test_event_engine.c
        src/event_engine.c
        src/messages.c
        src/event_builders.c
        src/event_vm.c
        src/monsters.c
        src/map.c
        src/state.c
    )
    target_include_directories(test_event_engine PRIVATE src)
    target_compile_options(test_event_engine PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(test_event_engine PRIVATE _POSIX_C_SOURCE=200809L)
    target_link_libraries(test_event_engine PRIVATE lua54 m)
    add_test(NAME event_engine COMMAND test_event_engine
        "${CMAKE_CURRENT_SOURCE_DIR}/src/events/builders.lua")

    add_executable(test_input
        tests/test_input.c
        src/input.c
        src/ui.c
        src/log.c
        src/intro.c
        src/map.c
        src/state.c
        src/save.c
        src/game.c
        src/items.c
        src/battle.c
        src/monsters.c
        src/app.c
        src/assets.c
        src/render.c
        src/events.c
        src/event_vm.c
        src/event_builders.c
        src/event_engine.c
        src/messages.c
    )
    target_include_directories(test_input PRIVATE src)
    target_compile_options(test_input PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(test_input PRIVATE
        _POSIX_C_SOURCE=200809L
        TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(test_input PRIVATE
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
        lua54
        m
    )
    add_test(NAME input COMMAND test_input)
endif()

install(TARGETS tsw-re RUNTIME DESTINATION bin)
install(DIRECTORY data/ DESTINATION share/tsw OPTIONAL)
install(DIRECTORY src/events/ DESTINATION share/tsw/events OPTIONAL)
install(FILES third_party/lua/LICENSE DESTINATION share/doc/tsw-re/lua)
install(FILES third_party/lua/README.md DESTINATION share/doc/tsw-re/lua)
