"""Exercise the CMake source-generation hook without building firmware."""
from pathlib import Path
import argparse
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]
PARSER = argparse.ArgumentParser()
PARSER.add_argument("--cmake", required=True, type=Path)
PARSER.add_argument("--idf", type=Path, default=Path("C:/esp/v5.5.5/esp-idf"))
ARGS, REMAINING = PARSER.parse_known_args()


class CameraDiagnosticsTests(unittest.TestCase):
    def fixture(self, source, success=True, short_component_name=False):
        with tempfile.TemporaryDirectory(prefix="ard-camera-cmake-") as folder:
            root = Path(folder)
            component = root / "camera component"
            (component / "driver").mkdir(parents=True)
            original = component / "driver/cam_hal.c"
            original.write_text(source, encoding="utf-8")
            # Use the installed IDF resolver with a fixture component registry.
            # Target metadata and source replacement are modeled in script mode;
            # no compiler/configure/build is invoked.
            hook = PROJECT / "cmake/camera_diagnostics.cmake"
            if short_component_name:
                invalid_hook = root / "short-name.cmake"
                invalid_hook.write_text(hook.read_text(encoding="utf-8").replace(
                    "espressif__esp32-camera", "esp32-camera"), encoding="utf-8")
                hook = invalid_hook
            script = root / "fixture.cmake"
            script.write_text(f'''
cmake_minimum_required(VERSION 3.16)
set(CMAKE_CURRENT_BINARY_DIR "{root.as_posix()}")
include("{(ARGS.idf / 'tools/cmake/component.cmake').as_posix()}")
function(idf_build_get_property out property)
    if(property STREQUAL "__COMPONENT_TARGETS")
        set(${{out}} "___idf_espressif__esp32-camera" PARENT_SCOPE)
    else()
        set(${{out}} "" PARENT_SCOPE)
    endif()
endfunction()
function(idf_build_set_property)
    # Resolution caching is unnecessary for this single-component fixture.
endfunction()
function(get_property out scope target keyword property)
    if(NOT scope STREQUAL "TARGET" OR NOT target STREQUAL "___idf_espressif__esp32-camera")
        message(FATAL_ERROR "Unexpected component registry lookup")
    endif()
    if(property STREQUAL "COMPONENT_NAME")
        set(${{out}} "espressif__esp32-camera" PARENT_SCOPE)
    elseif(property STREQUAL "COMPONENT_DIR")
        set(${{out}} "{component.as_posix()}" PARENT_SCOPE)
    elseif(property STREQUAL "COMPONENT_LIB")
        set(${{out}} "camera" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Unexpected component property")
    endif()
endfunction()
function(get_target_property out target property)
    set(${{out}} "{original.as_posix()};driver/sensor.c" PARENT_SCOPE)
endfunction()
function(set_property)
    if(ARGV0 STREQUAL "TARGET")
        file(WRITE "{root.as_posix()}/sources.txt" "${{ARGV4}}")
    endif()
endfunction()
function(target_include_directories)
    if(NOT EXISTS "${{ARGV2}}/cam_overflow_log.h")
        message(FATAL_ERROR "Diagnostic header path is missing")
    endif()
endfunction()
include("{hook.as_posix()}")
''', encoding="utf-8")
            result = subprocess.run([str(ARGS.cmake), "-P", str(script)], capture_output=True, text=True)
            if success:
                self.assertEqual(result.returncode, 0, result.stderr)
                generated = root / "ard_camera/cam_hal.c"
                patched = generated.read_text(encoding="utf-8")
                self.assertEqual(patched.count("ard_camera_report_overflow();"), 2)
                self.assertNotIn('cam_hal: FB-OVF', patched)
                self.assertIn('#include "cam_overflow_log.h"', patched)
                self.assertEqual(original.read_text(encoding="utf-8"), source)
                self.assertEqual((root / "sources.txt").read_text(),
                                 generated.as_posix() + ";driver/sensor.c")
            else:
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Failed to resolve component 'esp32-camera'" if short_component_name
                              else "expected two known prints", result.stderr)

    def test_installed_driver_and_repeatable_generation(self):
        source = (PROJECT / "managed_components/espressif__esp32-camera/driver/cam_hal.c").read_text(encoding="utf-8")
        self.fixture(source)
        self.fixture(source)

    def test_changed_driver_fails_clearly(self):
        self.fixture('void cam_task(void) { /* upstream changed */ }', success=False)

    def test_short_component_name_reproduces_reported_failure(self):
        self.fixture("unused", success=False, short_component_name=True)


if __name__ == "__main__":
    unittest.main(argv=[__file__] + REMAINING)
