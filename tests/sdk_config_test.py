"""Exercise installed SDK admission using CMake, without a native compiler/GPU.

Windows and Linux toolchain profiles are explicit configure-only fixtures. They
do not claim to compile or load MSVC modules on a non-Windows host.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


cmake = sys.argv[1]
generator = sys.argv[2]
build_program = sys.argv[3]
repo = Path(__file__).resolve().parents[1]
windows = {
    "CMAKE_SYSTEM_NAME": "Windows",
    "CMAKE_CXX_COMPILER_ID": "MSVC",
    "CMAKE_CXX_COMPILER_VERSION": "19.44.35229.0",
    "CMAKE_SIZEOF_VOID_P": "8",
    "CMAKE_SYSTEM_PROCESSOR": "AMD64",
    "CMAKE_BUILD_TYPE": "Release",
    "CMAKE_CXX_FLAGS": "/DWIN32 /D_WINDOWS /EHsc",
    "CMAKE_CXX_FLAGS_RELEASE": "/O2 /Ob2 /DNDEBUG",
    "MSVC_TOOLSET_VERSION": "143",
    "CMAKE_MSVC_RUNTIME_LIBRARY": "MultiThreadedDLL",
    "WIN32": "TRUE",
    "MSVC": "TRUE",
}
linux = dict(windows, CMAKE_SYSTEM_NAME="Linux", CMAKE_CXX_COMPILER_ID="GNU",
             CMAKE_CXX_COMPILER_VERSION="12.2.0", CMAKE_SYSTEM_PROCESSOR="x86_64",
             CMAKE_CXX_FLAGS="", CMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG",
             MSVC_TOOLSET_VERSION="", CMAKE_MSVC_RUNTIME_LIBRARY="",
             WIN32="FALSE", MSVC="FALSE")


def assignments(values):
    return "\n".join(f"set({key} [==[{value}]==])" if value is not None
                     else f"unset({key})" for key, value in values.items())


def run(arguments):
    result = subprocess.run([cmake, *map(str, arguments)], capture_output=True,
                            text=True, timeout=30)
    return result.returncode, result.stdout + result.stderr


failures = []
checks = 0
with tempfile.TemporaryDirectory(prefix="forge sdk config ") as temporary:
    root = Path(temporary)
    for label, profile in (("windows", windows), ("linux", linux)):
        sdk = root / label / "sdk"
        sdk.mkdir(parents=True)
        shutil.copyfile(repo / "cmake/module_kit_install.cmake.in",
                        sdk / "module_kit_install.cmake.in")
        producer = root / f"{label}-producer.cmake"
        producer.write_text(assignments(dict(
            profile, FORGE_SDK_CONFIG_FLAGS=profile["CMAKE_CXX_FLAGS_RELEASE"],
            FORGE_NATIVE_SDK_FINGERPRINT="configure-only-test",
            CMAKE_STATIC_LIBRARY_PREFIX="", CMAKE_STATIC_LIBRARY_SUFFIX=".lib")) +
            f'\nconfigure_file("{(repo / "cmake/ForgeNativeSdkConfig.cmake.in").as_posix()}" '
            f'"{(sdk / "ForgeNativeSdkConfig.cmake").as_posix()}" @ONLY)\n',
            encoding="utf-8")
        code, output = run(["-P", producer])
        assert code == 0, output

    def check(name, profile, overrides=None, expected=None, deployment=False):
        global checks
        checks += 1
        source = root / name
        source.mkdir()
        sdk = root / ("windows" if profile["WIN32"] == "TRUE" else "linux") / "sdk"
        client = dict(profile, **(overrides or {}))
        content = ("cmake_minimum_required(VERSION 3.24)\n"
                   "project(SdkAdmission LANGUAGES NONE)\n" + assignments(client) +
                   f'\nfind_package(ForgeNativeSdk CONFIG REQUIRED PATHS "{sdk.as_posix()}" '
                   "NO_DEFAULT_PATH)\n")
        if deployment:
            content += ('add_library(gameplay MODULE IMPORTED)\n'
                        'set_target_properties(gameplay PROPERTIES '
                        'IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/gameplay.dll")\n'
                        'forge_install_gameplay_runtime(gameplay NAME project.gameplay)\n')
        content += ('get_target_property(_links ForgeNativeSdk::Client INTERFACE_LINK_LIBRARIES)\n'
                    'get_target_property(_features ForgeNativeSdk::Client INTERFACE_COMPILE_FEATURES)\n'
                    'file(WRITE "${CMAKE_BINARY_DIR}/accepted.txt" '
                    '"${_links}\n${_features}\n${CMAKE_MSVC_RUNTIME_LIBRARY}\n")\n')
        (source / "CMakeLists.txt").write_text(content, encoding="utf-8")
        build = source / "build"
        arguments = ["-S", source, "-B", build, "-G", generator]
        if build_program:
            arguments.append("-DCMAKE_MAKE_PROGRAM=" + build_program)
        code, output = run(arguments)
        accepted = build / "accepted.txt"
        if expected:
            okay = code != 0 and not accepted.exists()
            okay = okay and all(" ".join(item.split()) in " ".join(output.split())
                                for item in expected)
        else:
            okay = code == 0 and accepted.is_file()
            if okay:
                okay = accepted.read_text().splitlines() == [
                    "ForgeNativeSdk::Flecs;ForgeNativeSdk::Values", "cxx_std_20",
                    profile["CMAKE_MSVC_RUNTIME_LIBRARY"]]
        if not okay:
            failures.append(f"{name}: expected {expected or 'matching SDK accepted'}\n{output}")
        print(f"{'PASS' if okay else 'FAIL'} {name}")

    check("matching-windows", windows, deployment=True)
    check("default-windows-crt", windows, {"CMAKE_MSVC_RUNTIME_LIBRARY": None})
    check("matching-linux", linux, deployment=True)
    # Only Windows deployment needs the newer runtime-dependency policy.
    check("linux-older-cmake", linux, {"CMAKE_VERSION": "3.31.6"}, deployment=True)
    check("windows-older-cmake-without-deployment", windows,
          {"CMAKE_VERSION": "3.31.6"})
    mismatches = {
        "CMAKE_SYSTEM_NAME": "Linux",
        "CMAKE_CXX_COMPILER_ID": "Clang",
        "CMAKE_CXX_COMPILER_VERSION": "19.44.10000.0",
        "CMAKE_SIZEOF_VOID_P": "4",
        "CMAKE_SYSTEM_PROCESSOR": "ARM64",
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_CXX_FLAGS": "/DWIN32 /D_WINDOWS /EHsc /DFORGE_MISMATCH",
        "CMAKE_CXX_FLAGS_RELEASE": "/Od /DNDEBUG",
        "MSVC_TOOLSET_VERSION": "142",
        "CMAKE_MSVC_RUNTIME_LIBRARY": "MultiThreaded",
    }
    for field, found in mismatches.items():
        check(field, windows, {field: found}, expected=[
            f'{field}: expected "{windows[field]}", found "{found}"',
            "matching SDK toolchain", "ForgeNativeSdkConfig.cmake"])
    check("missing-compiler-version", windows, {"CMAKE_CXX_COMPILER_VERSION": ""},
          expected=['CMAKE_CXX_COMPILER_VERSION: expected "19.44.35229.0", found ""'])
    check("windows-older-cmake", windows, {"CMAKE_VERSION": "3.31.6"},
          expected=["CMake 4.3 or newer", "3.31.6", "Compiler setup"], deployment=True)
    # An install may be invoked with a different CMake than configuration used.
    script = root / "old-install.cmake"
    script.write_text('set(WIN32 TRUE)\nset(CMAKE_VERSION "3.31.6")\n' +
                      f'include("{(repo / "cmake/module_kit_install.cmake.in").as_posix()}")\n',
                      encoding="utf-8")
    code, output = run(["-P", script])
    checks += 1
    if code == 0 or "CMake 4.3 or newer" not in output or "3.31.6" not in output:
        failures.append("older-install-cmake: expected version diagnosis before dependency scan\n" + output)
    else:
        print("PASS older-install-cmake")

assert not failures, "\n\n".join(failures)
print(f"{checks} installed SDK configuration checks passed (explicit profiles; no native compilation)")
