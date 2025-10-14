idf_component_register(
  SRCS
    "main.c"
    "usb_cdc_init.c"
    "wifi_manager.c"
    "app_nvs.c"
    "web_portal.c"
    "mqtt_manager.c"
    "espnow_manager.c"
    "device_registry.c"
    "mqtt_bridge.c"
    "factory_reset.c"
    "internal_device.c"

  INCLUDE_DIRS 
    "include"

  EMBED_FILES 
    "web/index.html" 
    "web/app.js" 
    "web/style.css"

  REQUIRES
    esp_tinyusb
    tinyusb
    driver
    esp_http_server
    esp_wifi
    esp_netif
    esp_event
    nvs_flash
    mqtt
    json
    esp_timer
)
