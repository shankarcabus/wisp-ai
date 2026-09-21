# Na placa S3 quem fornece os headers bsp/* — e, de carona, o driver de GPIO —
# é o BSP oficial da Waveshare, que entra por dependência do idf_component.yml.
# No C6 o substituto é um componente local (components/bsp_c6_amoled_216), e
# componente local não vira dependência sozinho quando REQUIRES é explícito.
set(reqs nvs_flash esp_wifi esp_netif esp_http_client json)
if("${IDF_TARGET}" STREQUAL "esp32c6")
    list(APPEND reqs bsp_c6_amoled_216 esp_driver_gpio)
endif()

# mbedtls é PRIV: o SHA-256 que identifica o bridge é usado dentro do main.c e
# não aparece em nenhum header nosso. Ele já está no binário — o WiFi e o
# esp-tls o trazem — e sem esta linha o build para no include, dizendo
# exatamente isto.
idf_component_register(
    SRCS "main.c" "ui.c" "desenho.c" "mascote.c" "mascote_terminal.c" "mascote_bytelo.c" "mascote_bytelo_props.c" "pmic.c" "sons.c" "som.c"
    INCLUDE_DIRS "."
    REQUIRES ${reqs}
    PRIV_REQUIRES mbedtls
)
