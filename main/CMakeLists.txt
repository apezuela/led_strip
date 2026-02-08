idf_component_register(
    SRCS "led_strip_custom.c"
    PRIV_REQUIRES esp_http_client app_update esp_https_ota
                  nvs_flash esp_netif esp_wifi efuse bt
                  protocomm
                  esp_event freertos driver
    EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem
)
