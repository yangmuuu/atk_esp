set(src_dirs
	"HTTP"
    "WIFI"
	"SR")

set(include_dirs
	"HTTP"
    "WIFI"
	"SR")

set(requires
    "nvs_flash"
    "esp_wifi"
    "esp_event"
    "log"
	"esp_http_client"
    "esp-tls"
    "json"
    "78__esp-wifi-connect"
	"freertos"
    "espressif__esp-sr")

idf_component_register(SRC_DIRS ${src_dirs}
                       INCLUDE_DIRS ${include_dirs}
                       REQUIRES ${requires})