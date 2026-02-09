set(src_dirs
        BUTTON
        XL9555
        CAMERA
		LCD
)

set(include_dirs
        BUTTON
        XL9555
        CAMERA
		LCD
)

set(requires
        driver
        log
        freertos
        esp_timer
        usb_stream
		esp_lcd               # 乐鑫 LCD 框架
        esp_lcd_touch_ft5x06  # 触摸驱动 (兼容 FT6x36)
        esp_lvgl_port         # LVGL 接口组件
        lvgl                  # LVGL 核心库
)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)