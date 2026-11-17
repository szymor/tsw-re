cmake_minimum_required(VERSION 3.20)
project(tsw_re C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(SDL3 3.2 CONFIG REQUIRED)
find_package(SDL3_image 3.0 CONFIG REQUIRED)
find_package(SDL3_ttf 3.0 CONFIG REQUIRED)
find_package(SDL3_mixer 3.0 CONFIG REQUIRED)

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
if(NOT WIN32)
    target_compile_definitions(lua54 PRIVATE LUA_USE_POSIX)
endif()

# Settings every game and test target shares.
function(tsw_target target)
    target_include_directories(${target} PRIVATE src)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_options(${target} PRIVATE -static-libgcc)
    endif()
    if(NOT WIN32)
        target_link_libraries(${target} PRIVATE m)
    endif()
endfunction()

# A test: its sources, SDL3 (fileio) and Lua.
function(tsw_test name)
    add_executable(test_${name} ${ARGN} src/fileio.c)
    tsw_target(test_${name})
    target_link_libraries(test_${name} PRIVATE SDL3::SDL3 lua54)
endfunction()

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
    src/widgets.c
    src/text_layout.c
    src/render_windows.c
    src/audio.c
    src/options.c
    src/input.c
    src/ui.c
    src/log.c
    src/intro.c
    src/events.c
    src/event_vm.c
    src/event_builders.c
    src/ranking.c
    src/event_engine.c
    src/messages.c
    src/itext.c
    src/replay.c
    src/fileio.c
)

add_executable(tsw-re ${TSW_SOURCES})
tsw_target(tsw-re)
target_link_libraries(tsw-re PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
    SDL3_mixer::SDL3_mixer
    lua54
)
target_compile_definitions(tsw-re PRIVATE
    TSW_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(WIN32)
    # A window, no console; the icon comes from src/tsw-re.rc.
    set_target_properties(tsw-re PROPERTIES WIN32_EXECUTABLE ON)
    target_sources(tsw-re PRIVATE src/tsw-re.rc)
endif()

include(CTest)
if(BUILD_TESTING)
    set(GAME_CORE src/game.c src/items.c src/itext.c src/battle.c
        src/monsters.c src/state.c src/map.c src/save.c)
    set(EVENT_CORE src/event_engine.c src/event_builders.c src/event_vm.c
        src/ranking.c src/messages.c src/monsters.c src/map.c src/state.c)

    tsw_test(state tests/test_state.c src/state.c src/map.c src/save.c)
    add_test(NAME state COMMAND test_state)

    tsw_test(intro tests/test_intro.c src/intro.c)
    add_test(NAME intro COMMAND test_intro)

    tsw_test(game tests/test_game.c ${GAME_CORE})
    add_test(NAME game COMMAND test_game)

    tsw_test(items tests/test_items.c ${GAME_CORE})
    add_test(NAME items COMMAND test_items)

    tsw_test(text_layout tests/test_text_layout.c src/text_layout.c)
    add_test(NAME text_layout COMMAND test_text_layout)

    tsw_test(battle tests/test_battle.c ${GAME_CORE})
    add_test(NAME battle COMMAND test_battle)

    tsw_test(replay tests/test_replay.c src/replay.c ${GAME_CORE}
        src/event_engine.c src/event_builders.c src/ranking.c
        src/event_vm.c src/messages.c)
    target_compile_definitions(test_replay PRIVATE
        TSW_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME replay COMMAND test_replay)

    tsw_test(event_vm tests/test_event_vm.c src/event_vm.c src/map.c
        src/state.c)
    add_test(NAME event_vm COMMAND test_event_vm)

    tsw_test(ranking tests/test_ranking.c src/ranking.c)
    add_test(NAME ranking COMMAND test_ranking)

    tsw_test(event_builders tests/test_event_builders.c ${EVENT_CORE})
    target_include_directories(test_event_builders PRIVATE tests)
    target_compile_definitions(test_event_builders PRIVATE
        TSW_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    # Part 2 (golden-vector parity) reads $TSW_EVENT_VECTORS or
    # ~/.cache/tsw/vectors; it is skipped when no vectors were exported.
    add_test(NAME event_builders COMMAND test_event_builders
        "${CMAKE_CURRENT_SOURCE_DIR}/src/events/builders.lua")

    tsw_test(event_engine tests/test_event_engine.c ${EVENT_CORE})
    add_test(NAME event_engine COMMAND test_event_engine
        "${CMAKE_CURRENT_SOURCE_DIR}/src/events/builders.lua")

    list(FILTER TSW_SOURCES EXCLUDE REGEX "src/(main|replay|fileio)\\.c$")
    tsw_test(input tests/test_input.c ${TSW_SOURCES})
    target_compile_definitions(test_input PRIVATE
        TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_input PRIVATE
        SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
        SDL3_mixer::SDL3_mixer
    )
    add_test(NAME input COMMAND test_input)
endif()

if(WIN32)
    # A folder to unzip: tsw-re.exe and the SDL DLLs, data\ and
    # data\events\ beside it (main.c looks for <exe>/data).
    install(TARGETS tsw-re RUNTIME DESTINATION .)
    install(FILES $<TARGET_RUNTIME_DLLS:tsw-re> DESTINATION .)
    install(DIRECTORY data/ DESTINATION data)
    install(DIRECTORY src/events/ DESTINATION data/events
        FILES_MATCHING PATTERN "*.lua")
    install(FILES third_party/lua/LICENSE DESTINATION licenses/lua)
else()
    install(TARGETS tsw-re RUNTIME DESTINATION bin)
    install(DIRECTORY data/ DESTINATION share/tsw)
    install(DIRECTORY src/events/ DESTINATION share/tsw/events
        FILES_MATCHING PATTERN "*.lua")
    install(FILES third_party/lua/LICENSE DESTINATION share/doc/tsw-re/lua)
    install(FILES third_party/lua/README.md DESTINATION share/doc/tsw-re/lua)
endif()
