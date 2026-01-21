idf.py -p /dev/ttyUSB0 erase_flash


/* 5. 配置配网管理器 */
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_softap, 
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    // -------------- 新增这一行 --------------
    // 强制清除配网信息（仅用于调试，正式发布要删掉或改成按键触发）
    wifi_prov_mgr_reset_provisioning(); 
    // ---------------------------------------

    /* 6. 检查是否已经配过网 */
    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));