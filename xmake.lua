-- On Your Own: Fallout 4 F4SE plugin.
--
-- Windows:  xmake f -p windows -a x64 -m release && xmake build OnYourOwn
-- Linux:    scripts/build-arch.sh (cross-build with clang-cl; uses the "mingw" platform name
--           only to allow a Linux host, the output is a normal MSVC-ABI DLL)
-- Tests:    xmake build -g tests && xmake run -g tests   (any platform)

set_xmakever("3.0.0")
set_project("OnYourOwn")
set_version("1.0.0")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")
add_rules("mode.debug", "mode.releasedbg")

add_toolchaindirs("toolchains")
add_repositories("oyo-packages .")  -- packages/: spdlog recipe adapted for the Linux cross-build

if is_plat("mingw") then
    add_defines("SPDLOG_WCHAR_TO_UTF8_SUPPORT")
    add_includedirs("toolchains/compat")  -- one case-sensitive include fix for CommonLibF4
end

includes("lib/commonlibf4")

if is_plat("mingw") then
    -- lld-link expects MSVC-style library names.
    target("commonlibf4", function()
        set_filename("commonlibf4.lib")
    end)
    target("commonlib-shared", function()
        set_filename("commonlib-shared.lib")
    end)
end

if is_plat("windows", "mingw") then
    target("OnYourOwn")
        set_filename("OnYourOwn.dll")
        if is_plat("mingw") then
            add_links("commonlibf4", "commonlib-shared")
        end
        add_rules("commonlibf4.plugin", {
            name = "OnYourOwn",
            author = "Vito Tivadar",
            description = "Independent Pip-Boy map and HUD marker visibility"
        })
        add_files(
            "src/Plugin.cpp",
            "src/PipboyFilter.cpp",
            "src/HudCompass.cpp",
            "src/MarkerPolicy.cpp",
            "src/CompassPolicy.cpp",
            "src/MarkerClassifier.cpp",
            "src/IniSettings.cpp"
        )
        set_pcxxheader("src/PCH.h")
    target_end()
end

-- Host tests for the game-independent code.
local logic_sources = { "src/MarkerPolicy.cpp", "src/CompassPolicy.cpp", "src/MarkerClassifier.cpp", "src/IniSettings.cpp" }

target("marker_policy_tests")
    set_kind("binary")
    set_default(false)
    set_group("tests")
    add_includedirs("src")
    add_files("src/MarkerPolicy.cpp", "tests/MarkerPolicyTests.cpp")
target_end()

target("logic_tests")
    set_kind("binary")
    set_default(false)
    set_group("tests")
    add_includedirs("src")
    add_files(logic_sources, "tests/logic_tests.cpp")
target_end()

target("defaults_test")
    set_kind("binary")
    set_default(false)
    set_group("tests")
    add_includedirs("src")
    add_files(logic_sources, "tests/defaults_test.cpp")
    set_rundir("$(projectdir)")
target_end()
