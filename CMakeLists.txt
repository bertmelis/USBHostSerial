set(COMPONENT_SRCDIRS
    "src"
)

set(COMPONENT_ADD_INCLUDEDIRS
    "src"
)

set(COMPONENT_REQUIRES
    "arduino-esp32"
    "usb_host_ch34x_vcp"
    "usb_host_cp210x_vcp"
    "usb_host_ftdi_vcp"
    "usb_host_vcp"
)

register_component()

target_compile_options(${COMPONENT_TARGET} PRIVATE -fno-rtti)
