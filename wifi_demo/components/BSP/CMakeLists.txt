set(src_dirs
        BUTTON
        XL9555
        CAMERA
)

set(include_dirs
        BUTTON
        XL9555
        CAMERA
)

set(requires
        driver
        log
        freertos
        esp_timer
        usb_stream
)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)