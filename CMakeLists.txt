# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

# Definir padrões C e C++
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Habilitar geração do compile_commands.json para IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Definir o tipo de placa como Pico W
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Importar o Pico SDK
include(pico_sdk_import.cmake)

# Definir o projeto
project(Arrow_Game C CXX ASM)

# Inicializar o Pico SDK
pico_sdk_init()

# Definir o executável e suas fontes
add_executable(${PROJECT_NAME}
    Arrow_Game.c        # Código principal
    lib/ssd1306.c       # Biblioteca para o display OLED
)

# Incluir o arquivo PIO para a matriz de LEDs
pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/lib/pio_matrix.pio)

# Definir diretórios de inclusão
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/lib
)

# Vincular bibliotecas necessárias
target_link_libraries(${PROJECT_NAME} PRIVATE
    pico_stdlib
    hardware_adc
    hardware_i2c
    hardware_pwm
    hardware_pio
    hardware_clocks
)

# Habilitar saída USB e desabilitar UART
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# Gerar arquivos adicionais (UF2, HEX, etc.)
pico_add_extra_outputs(${PROJECT_NAME})