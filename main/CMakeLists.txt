# idf_component_register(SRCS 
# "esp_udp.c"
# # "sih2026.c"
# "inmp441.c"
# "preprocessing.c"
# # "frames.c"
#                     INCLUDE_DIRS ".")
idf_component_register(
    SRCS
        # "esp_udp.c"
        # "inmp441.c"
        # "preprocessing.c"
        "kws_testing.cpp"

    INCLUDE_DIRS "."

    EMBED_FILES
        "marvin_kws_int8.tflite"
)