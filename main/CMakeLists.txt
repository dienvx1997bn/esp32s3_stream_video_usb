set(srcs "main.c"
    "st7789.c"
    "decode_image.c"
    )

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS ".")
