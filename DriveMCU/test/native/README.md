# DriveMCU native regression tests
MSVC C++ Build Tools and Python are required on Windows.
Run `python test/native/run_tests.py` from the DriveMCU directory.
The runner compiles production headers with fake I2C and time, and extracts the current production motorTask/emergencyStop for motor queue and stop tests. Generated files stay in test/native/build.
These tests do not measure physical stepping, electrical faults or real network performance. Compile the ESP32 firmware separately with `pio run`.
