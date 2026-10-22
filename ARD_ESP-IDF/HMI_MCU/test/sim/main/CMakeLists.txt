idf_component_register(
    SRCS "sim_main.c" "../../../main/gfx.c" "../../../main/hmi_ui.c"
         "../../../main/hmi_model.c" "../../../main/hmi_setup.c"
         "../../../../shared/ard_setup_qr.c" "../../../../shared/third_party/qrcodegen.c"
    INCLUDE_DIRS "../../../main" "../../../components/ard_net/include" "../../../../shared"
    REQUIRES esp_http_server wifi_worker
)
