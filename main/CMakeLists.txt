# All .c files of the main program.
# Adding a new .c file? Put it in this list.
idf_component_register(
    SRCS "main.c" "display_port.c" "hx8357.c" "wifi.c" "weather.c" "ui.c"
    INCLUDE_DIRS ".")
