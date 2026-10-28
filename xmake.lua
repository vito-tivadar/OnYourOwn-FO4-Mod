-- On Your Own: Fallout 4 F4SE plugin.
--
-- Windows:  xmake f -p windows -a x64 -m release && xmake build OnYourOwn
-- Linux:    scripts/build-arch.sh (cross-build with clang-cl; uses the "mingw" platform name
--           only to allow a Linux host, the output is a normal MSVC-ABI DLL)
-- Old-gen:  --oldgen=y builds for game 1.10.163 / F4SE 0.6.23 (the build scripts do both)
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

option("oldgen")
    set_default(false)
    set_showmenu(true)
    set_description("Build for Fallout 4 1.10.163 (old-gen) with F4SE 0.6.23")
option_end()

if is_plat("mingw") then
    add_defines("SPDLOG_WCHAR_TO_UTF8_SUPPORT")
    add_includedirs("toolchains/compat")  -- one case-sensitive include fix for CommonLibF4
end

if has_config("oldgen") then
    -- RE/IDs.h and RE/IDs_VTABLE.h with old-gen Address Library IDs, shadowing CommonLibF4's.
    local ids = path.join(os.projectdir(), "build/oldgen/ids")
    if not os.isfile(path.join(ids, "RE/IDs.h")) then
        raise("old-gen ID headers missing: run python3 scripts/oldgen.py headers build/oldgen/ids")
    end
    add_includedirs(ids)
    add_defines("OYO_OLD_GEN")
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
