# The renderer is shared with the desktop build in ../../c.
set(ff ${CMAKE_CURRENT_LIST_DIR}/../../c)

idf_component_register(
  SRCS main.c epd.c net.c forecast.c ${ff}/fishfodder.c ${ff}/fishdraw.c ${ff}/fdlibm.c
  INCLUDE_DIRS . ${ff}
  PRIV_REQUIRES esp_driver_spi esp_driver_gpio esp_wifi esp_netif esp_event esp_http_client
                esp_timer mbedtls nvs_flash)

# Fused multiply-adds would change the fish (see c/Makefile). The renderer is
# also worth the speed of -O2 whatever the project's optimisation level.
target_compile_options(${COMPONENT_LIB} PRIVATE -ffp-contract=off)
set_source_files_properties(${ff}/fishfodder.c ${ff}/fishdraw.c ${ff}/fdlibm.c PROPERTIES COMPILE_OPTIONS -O2)
