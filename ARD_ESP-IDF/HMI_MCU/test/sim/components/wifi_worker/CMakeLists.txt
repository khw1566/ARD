idf_component_register(
    SRCS "worker_tests.c"
    PRIV_INCLUDE_DIRS "fake_sdk" "../../../../../shared/test" "../../../../components/ard_net/include"
)
# Logging is disabled by the fake SDK, so its TAG variable is intentionally unused.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable)
