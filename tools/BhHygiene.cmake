# bh_hygiene: architectural gates for demo/. Included at configure time and run again by the bh_hygiene target
# (cmake -DBH_DEMO=<dir> -P tools/BhHygiene.cmake). Any violation aborts with FATAL_ERROR.
file(GLOB_RECURSE bh_hy_files ${BH_DEMO}/*.cpp ${BH_DEMO}/*.hpp)

# Gate 2..5: forbidden tokens, each with its message.
set(bh_hy_gate_regex
    "kGlyphPx|fitName"
    "kTipDelay|kTipW|kGhostH|pinNear|guiLayoutSetRect"
    "kGuiRowIconBase|kGuiRowIcon"
    "\"[^\"\n]*::[^\"\n]*\"")
set(bh_hy_gate_message
    "HYGIENE: Procedural text truncation in demo/ is BANNED! Let GuiEmitter handle ellipsis."
    "HYGIENE: Tooltips, drag ghosts, and coordinate layout belong in core/gui/!"
    "HYGIENE: Packing binary control bytes in strings is BANNED! Use structured GuiRow fields."
    "HYGIENE: Hardcoded DOM paths (a \"::\" inside a string) in C++ are BANNED! Use semantic bind keys.")

foreach(bh_hy_file ${bh_hy_files})
    file(READ ${bh_hy_file} bh_hy_text)
    # Gate 1: demo reaches the GUI only through the GuiHost facade, drag types and icon names.
    if(bh_hy_text MATCHES "#[ \t]*include[ \t]*[\"<][^\">]*(gui/internal/|internal/)")
        message(FATAL_ERROR "HYGIENE: demo/ includes private GUI header! (core/gui/internal) ${bh_hy_file}")
    endif()
    if(bh_hy_text MATCHES "GuiComponents\\.hpp|GuiLayoutEngine\\.hpp|GuiEmitter\\.hpp|GuiScissor\\.hpp")
        message(FATAL_ERROR "HYGIENE: demo/ includes private GUI header! ${bh_hy_file}")
    endif()
    string(REGEX MATCHALL "#[ \t]*include[ \t]*[\"<]gui/[^\">]*[\">]" bh_hy_gui_includes "${bh_hy_text}")
    foreach(bh_hy_inc ${bh_hy_gui_includes})
        if(NOT bh_hy_inc MATCHES "gui/GuiHost\\.hpp|gui/GuiDragTypes\\.hpp|gui/GuiIcons\\.hpp")
            message(FATAL_ERROR "HYGIENE: demo/ includes private GUI header! ${bh_hy_inc} in ${bh_hy_file}")
        endif()
    endforeach()
    list(LENGTH bh_hy_gate_regex bh_hy_gate_count)
    math(EXPR bh_hy_last "${bh_hy_gate_count} - 1")
    foreach(bh_hy_i RANGE ${bh_hy_last})
        list(GET bh_hy_gate_regex ${bh_hy_i} bh_hy_re)
        if(bh_hy_text MATCHES "${bh_hy_re}")
            list(GET bh_hy_gate_message ${bh_hy_i} bh_hy_msg)
            message(FATAL_ERROR "${bh_hy_msg} ${bh_hy_file}")
        endif()
    endforeach()
    # Gate 8: no raw threads, no stream I/O, no raw OS window in application code.
    if(bh_hy_file MATCHES "/demo/app/")
        if(bh_hy_text MATCHES "#[ \t]*include[ \t]*<(thread|condition_variable)>")
            message(FATAL_ERROR "HYGIENE: Raw threads are BANNED in demo/app! Use platform/Jobs.hpp. ${bh_hy_file}")
        endif()
        if(bh_hy_text MATCHES "#[ \t]*include[ \t]*<fstream>")
            message(FATAL_ERROR "HYGIENE: <fstream> is BANNED in demo/app! Use std::filesystem or io/File.hpp. ${bh_hy_file}")
        endif()
        if(bh_hy_text MATCHES "#[ \t]*include[ \t]*\"platform/Window\\.hpp\"")
            message(FATAL_ERROR "HYGIENE: Controllers must not touch raw OS windows! Use platform/Dialog.hpp. ${bh_hy_file}")
        endif()
    endif()
    # Gate 6: controllers under 300 lines.
    if(bh_hy_file MATCHES "/demo/app/[^/]*\\.cpp$")
        string(REGEX MATCHALL "\n" bh_hy_newlines "${bh_hy_text}")
        list(LENGTH bh_hy_newlines bh_hy_lines)
        if(bh_hy_lines GREATER 300)
            message(FATAL_ERROR "HYGIENE: Controller exceeds 300 lines limit! ${bh_hy_file} (${bh_hy_lines})")
        endif()
    endif()
endforeach()

# Gate 7: core/gui/internal is private to bh_gui. No other source (public gui headers, host, rhi, engine, demo) may include
# a header that resolves under it. Tests are the only consumers allowed to reach in.
file(GLOB_RECURSE bh_hy_all ${BH_CORE}/*.cpp ${BH_CORE}/*.hpp ${BH_ENGINE}/*.cpp ${BH_ENGINE}/*.hpp ${BH_DEMO}/*.cpp ${BH_DEMO}/*.hpp)
foreach(bh_hy_file ${bh_hy_all})
    if(bh_hy_file MATCHES "/core/gui/internal/")
        continue()
    endif()
    file(READ ${bh_hy_file} bh_hy_text)
    string(REGEX MATCHALL "#[ \t]*include[ \t]*\"[^\"]+\"" bh_hy_includes "${bh_hy_text}")
    foreach(bh_hy_inc ${bh_hy_includes})
        string(REGEX REPLACE "^[^\"]*\"([^\"]+)\"$" "\\1" bh_hy_rel "${bh_hy_inc}")
        if(EXISTS ${BH_CORE}/gui/internal/${bh_hy_rel} AND NOT EXISTS ${BH_CORE}/${bh_hy_rel})
            message(FATAL_ERROR "HYGIENE: core/gui/internal is private to bh_gui! ${bh_hy_rel} included from ${bh_hy_file}")
        endif()
    endforeach()
endforeach()
message(STATUS "bh_hygiene: demo/ passed all 8 gates, core/gui/internal is sealed")
