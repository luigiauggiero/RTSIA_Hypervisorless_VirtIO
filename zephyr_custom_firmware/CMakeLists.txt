cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hvl_net_rng)

zephyr_compile_definitions(METAL_MAX_DEVICE_REGIONS=2)

FILE(GLOB app_sources src/*.c)

# Iniezione chirurgica: carichiamo SOLO i driver (Frontend) e il livello HVL
# Ignoriamo volontariamente i file _dev.c (Backend/PMM) e i driver non richiesti
target_sources(app PRIVATE 
    ${app_sources}
    ${ZEPHYR_BASE}/../modules/lib/open-amp/open-amp/lib/virtio_mmio/virtio_mmio_drv.c
    ${ZEPHYR_BASE}/../modules/lib/open-amp/open-amp/lib/virtio_mmio/virtio_mmio_hvl.c
    ${ZEPHYR_BASE}/../modules/lib/open-amp/open-amp/lib/virtio_mmio/virtio_net_drv.c
    ${ZEPHYR_BASE}/../modules/lib/open-amp/open-amp/lib/virtio_mmio/virtio_rng_drv.c
)
