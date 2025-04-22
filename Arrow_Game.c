#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pio_matrix.pio.h"
#include "./lib/ssd1306.h"
#include "./lib/font.h"
#include "./lib/frames.h"

// Comunicação Serial I2C
#define I2C_PORT i2c1
#define I2C_SDA_PIN 14
#define I2C_SCL_PIN 15

// Definições do Display
#define SSD_ADDR 0x3C
#define SSD_WIDTH 128
#define SSD_HEIGHT 64

// Definições do PWM para Buzzers (2 kHz)
#define BUZZER_WRAP 62500
#define BUZZER_CLK_DIV 1.0
#define BUZZER_DURATION_MS 200 // Som curto para erro
#define BUZZER_SUCCESS_MS 100  // Som curto para acerto

// Buzzers
#define BUZZER_A_PIN 21
#define BUZZER_B_PIN 10

// LED RGB
#define RGB_RED_PIN 13   // PWM
#define RGB_GREEN_PIN 11 // PWM
#define RGB_BLUE_PIN 12  // PWM
#define RGB_PWM_WRAP 255
#define RGB_CLK_DIV 1.0

// Matriz de LEDs
#define MATRIZ_LEDS_PIN 7
#define NUM_LEDS 25

// Joystick
#define JOY_X_PIN 27 // ADC0
#define JOY_Y_PIN 26 // ADC1
#define BUTTON_JOY_PIN 22

// Botões
#define BUTTON_CONFIRM_PIN 6 // Confirmação

// Definições do Jogo
#define MAX_SEQUENCE 10
#define BASE_TIME_LIMIT_MS 5000 // 5s para testes
#define TIME_DECREMENT_MS 200   // Redução por nível
#define MIN_TIME_LIMIT_MS 2000  // 2s para testes
#define ROUNDS_PER_LEVEL 5      // Rounds para subir de nível
#define SQUARE_SIZE 8           // Quadrado 8x8 no OLED

// Faixa calibrada do joystick (ajustar após depuração)
#define JOY_MIN 500
#define JOY_MAX 3500
#define JOY_CENTER ((JOY_MIN + JOY_MAX) / 2)
#define JOY_THRESHOLD 300 // Reduzido para maior sensibilidade

// Frames das setas (0: cima, 1: baixo, 2: esquerda, 3: direita, 4-7: diagonais)
extern float arrow_frames[8][5][5];

// Estrutura do estado do jogo
typedef struct {
    uint8_t lives;           // Vidas restantes
    uint8_t sequence[MAX_SEQUENCE]; // Sequência de setas
    uint8_t sequence_length; // Tamanho atual da sequência
    uint8_t current_step;    // Passo atual na sequência
    uint32_t rounds;         // Rounds sobrevividos
    uint8_t difficulty_level;// Nível de dificuldade
    bool game_over;          // Estado de game over
    uint32_t last_action_time; // Tempo da última ação
    uint8_t square_x;        // Posição do quadrado no OLED
    uint8_t square_y;
} GameState;

// Variáveis globais
ssd1306_t ssd;
PIO pio = pio0;
uint sm;
GameState game;
volatile bool button_confirm_pressed = false;
volatile bool button_joy_pressed = false;
uint16_t joy_x_min = 4095, joy_x_max = 0, joy_y_min = 4095, joy_y_max = 0; // Rastrear faixa

// Protótipos
void setup();
void init_buttons();
void init_buzzers();
void init_rgb();
void init_joystick();
void init_i2c_display(ssd1306_t *ssd);
void init_matrix_leds();
void button_irq_handler(uint gpio, uint32_t events);
void display_arrow(uint8_t arrow_index);
void play_buzzer_error();
void play_buzzer_success();
void update_rgb_lives();
void update_oled_square();
void reset_game();
void generate_sequence();
uint32_t matrix_led_color(float red, float green, float blue);
uint32_t get_time_limit();
void read_joystick(uint16_t *joy_x, uint16_t *joy_y);

// Função para ler joystick com média para reduzir ruído
void read_joystick(uint16_t *joy_x, uint16_t *joy_y) {
    uint32_t sum_x = 0, sum_y = 0;
    const int samples = 10; // Aumentado para maior estabilidade
    for (int i = 0; i < samples; i++) {
        adc_select_input(0); // Eixo X
        sum_x += adc_read();
        adc_select_input(1); // Eixo Y
        sum_y += adc_read();
        sleep_us(100);
    }
    *joy_x = sum_x / samples;
    *joy_y = sum_y / samples;

    // Rastrear valores extremos
    if (*joy_x < joy_x_min) joy_x_min = *joy_x;
    if (*joy_x > joy_x_max) joy_x_max = *joy_x;
    if (*joy_y < joy_y_min) joy_y_min = *joy_y;
    if (*joy_y > joy_y_max) joy_y_max = *joy_y;
    printf("Extremos: X=[%u,%u], Y=[%u,%u]\n", joy_x_min, joy_x_max, joy_y_min, joy_y_max);
}

int main() {
    stdio_init_all();
    setup();
    reset_game();

    sleep_ms(2000);
    printf("Jogo iniciado\n");

    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);
    ssd1306_draw_string(&ssd, "JOGO DE SETAS", (SSD_WIDTH/2) - ((sizeof("JOGO DE SETAS") * 8) / 2), 20);
    ssd1306_draw_string(&ssd, "Pressione Botao", (SSD_WIDTH/2) - ((sizeof("Pressione Botao") * 8) / 2), 40);
    ssd1306_send_data(&ssd);

    while (!button_confirm_pressed) tight_loop_contents();
    button_confirm_pressed = false;

    while (true) {
        if (game.game_over) {
            char buffer[20];
            snprintf(buffer, sizeof(buffer), "Rounds: %lu Nivel: %u", game.rounds, game.difficulty_level);
            ssd1306_fill(&ssd, false);
            ssd1306_send_data(&ssd);
            ssd1306_draw_string(&ssd, "GAME OVER", (SSD_WIDTH/2) - ((sizeof("GAME OVER") * 8) / 2), 20);
            ssd1306_draw_string(&ssd, buffer, (SSD_WIDTH/2) - ((strlen(buffer) * 8) / 2), 40);
            ssd1306_send_data(&ssd);
            sleep_ms(5000);
            reset_game();
            continue;
        }

        char buffer[20];
        snprintf(buffer, sizeof(buffer), "Nivel: %u Rodada: %lu", game.difficulty_level, game.rounds + 1);
        ssd1306_fill(&ssd, false);
        ssd1306_send_data(&ssd);
        ssd1306_draw_string(&ssd, buffer, (SSD_WIDTH/2) - ((strlen(buffer) * 8) / 2), 30);
        ssd1306_send_data(&ssd);
        sleep_ms(1000);

        generate_sequence();
        printf("Sequência: ");
        for (uint8_t i = 0; i < game.sequence_length; i++) {
            printf("%u ", game.sequence[i]);
        }
        printf("\n");

        for (uint8_t i = 0; i < game.sequence_length; i++) {
            display_arrow(game.sequence[i]);
            sleep_ms(1000);
            update_oled_square();
        }

        game.current_step = 0;
        game.last_action_time = to_ms_since_boot(get_absolute_time());
        while (game.current_step < game.sequence_length) {
            update_oled_square();

            uint32_t current_time = to_ms_since_boot(get_absolute_time());
            if (current_time - game.last_action_time > get_time_limit()) {
                printf("Tempo esgotado! Vidas: %u\n", game.lives);
                game.lives--;
                play_buzzer_error();
                update_rgb_lives();
                game.current_step = game.sequence_length;
            }

            if (button_confirm_pressed) {
                button_confirm_pressed = false;
                uint16_t joy_x, joy_y;
                read_joystick(&joy_x, &joy_y);
                printf("Joystick: X=%u, Y=%u\n", joy_x, joy_y);

                // Determinar direção dominante
                int16_t delta_x = (int16_t)joy_x - JOY_CENTER;
                int16_t delta_y = (int16_t)joy_y - JOY_CENTER;
                uint8_t direction = 0; // Cima por padrão

                // Comparar deslocamentos absolutos
                if (abs(delta_y) > abs(delta_x)) {
                    // Priorizar Y (cima/baixo)
                    if (delta_y < -JOY_THRESHOLD) direction = 0; // Cima
                    else if (delta_y > JOY_THRESHOLD) direction = 1; // Baixo
                } else {
                    // Priorizar X (esquerda/direita)
                    if (delta_x < -JOY_THRESHOLD) direction = 2; // Esquerda
                    else if (delta_x > JOY_THRESHOLD) direction = 3; // Direita
                }
                printf("Direção detectada: %u (Esperado: %u)\n", direction, game.sequence[game.current_step]);

                // Verificar com tolerância
                bool correct = (direction == game.sequence[game.current_step]);
                if (!correct && abs(delta_x) > JOY_THRESHOLD && abs(delta_y) > JOY_THRESHOLD) {
                    // Aceitar diagonais
                    if (game.sequence[game.current_step] == 0 && delta_y < -JOY_THRESHOLD) correct = true; // Cima
                    else if (game.sequence[game.current_step] == 1 && delta_y > JOY_THRESHOLD) correct = true; // Baixo
                    else if (game.sequence[game.current_step] == 2 && delta_x < -JOY_THRESHOLD) correct = true; // Esquerda
                    else if (game.sequence[game.current_step] == 3 && delta_x > JOY_THRESHOLD) correct = true; // Direita
                }

                if (correct) {
                    printf("Correto!\n");
                    play_buzzer_success();
                    game.current_step++;
                    game.last_action_time = current_time;
                } else {
                    printf("Errado! Vidas: %u\n", game.lives);
                    game.lives--;
                    play_buzzer_error();
                    update_rgb_lives();
                    game.current_step = game.sequence_length;
                }
            }

            if (game.lives == 0) {
                printf("Game Over! Vidas: 0\n");
                game.game_over = true;
                break;
            }
        }

        game.rounds++;
        game.difficulty_level = (game.rounds / ROUNDS_PER_LEVEL) + 1;
        game.sequence_length = game.difficulty_level;
        if (game.sequence_length > MAX_SEQUENCE) {
            game.sequence_length = MAX_SEQUENCE;
        }
    }
}

void setup() {
    init_buttons();
    init_buzzers();
    init_rgb();
    init_joystick();
    init_matrix_leds();
    init_i2c_display(&ssd);
}

void init_buttons() {
    gpio_init(BUTTON_CONFIRM_PIN);
    gpio_set_dir(BUTTON_CONFIRM_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_CONFIRM_PIN);
    gpio_set_irq_enabled_with_callback(BUTTON_CONFIRM_PIN, GPIO_IRQ_EDGE_FALL, true, button_irq_handler);

    gpio_init(BUTTON_JOY_PIN);
    gpio_set_dir(BUTTON_JOY_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_JOY_PIN);
    gpio_set_irq_enabled_with_callback(BUTTON_JOY_PIN, GPIO_IRQ_EDGE_FALL, true, button_irq_handler);
}

void init_buzzers() {
    gpio_set_function(BUZZER_A_PIN, GPIO_FUNC_PWM);
    gpio_set_function(BUZZER_B_PIN, GPIO_FUNC_PWM);
    uint buzzer_slice = pwm_gpio_to_slice_num(BUZZER_A_PIN);
    pwm_set_clkdiv(buzzer_slice, BUZZER_CLK_DIV);
    pwm_set_wrap(buzzer_slice, BUZZER_WRAP);
    pwm_set_chan_level(buzzer_slice, PWM_CHAN_A, 0);
    pwm_set_chan_level(buzzer_slice, PWM_CHAN_B, 0);
    pwm_set_enabled(buzzer_slice, false);
}

void init_rgb() {
    gpio_set_function(RGB_RED_PIN, GPIO_FUNC_PWM);
    gpio_set_function(RGB_GREEN_PIN, GPIO_FUNC_PWM);
    gpio_set_function(RGB_BLUE_PIN, GPIO_FUNC_PWM);
    uint slice_red_green = pwm_gpio_to_slice_num(RGB_RED_PIN);
    uint slice_blue = pwm_gpio_to_slice_num(RGB_BLUE_PIN);
    pwm_set_clkdiv(slice_red_green, RGB_CLK_DIV);
    pwm_set_wrap(slice_red_green, RGB_PWM_WRAP);
    pwm_set_chan_level(slice_red_green, PWM_CHAN_A, 0);
    pwm_set_chan_level(slice_red_green, PWM_CHAN_B, 0);
    pwm_set_clkdiv(slice_blue, RGB_CLK_DIV);
    pwm_set_wrap(slice_blue, RGB_PWM_WRAP);
    pwm_set_chan_level(slice_blue, PWM_CHAN_A, 0);
    pwm_set_enabled(slice_red_green, true);
    pwm_set_enabled(slice_blue, true);
}

void init_joystick() {
    adc_init();
    adc_gpio_init(JOY_X_PIN);
    adc_gpio_init(JOY_Y_PIN);
}

void init_matrix_leds() {
    sm = pio_claim_unused_sm(pio, true);
    uint offset = pio_add_program(pio, &pio_matrix_program);
    pio_matrix_program_init(pio, sm, offset, MATRIZ_LEDS_PIN);
    pio_sm_set_enabled(pio, sm, true);
}

void init_i2c_display(ssd1306_t *ssd) {
    i2c_init(I2C_PORT, 400000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
    ssd1306_init(ssd, SSD_WIDTH, SSD_HEIGHT, false, SSD_ADDR, I2C_PORT);
    ssd1306_config(ssd);
    ssd1306_fill(ssd, false);
    ssd1306_send_data(ssd);
}

void button_irq_handler(uint gpio, uint32_t events) {
    static uint32_t last_time = 0;
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    if (current_time - last_time > 300) { // Aumentado para 300ms
        if (gpio == BUTTON_CONFIRM_PIN) {
            button_confirm_pressed = true;
            printf("Botão de confirmação pressionado\n");
        } else if (gpio == BUTTON_JOY_PIN) {
            button_joy_pressed = true;
            printf("Botão do joystick pressionado\n");
        }
        last_time = current_time;
    }
}

void display_arrow(uint8_t arrow_index) {
    uint32_t colors[NUM_LEDS] = {0};
    for (uint8_t row = 0; row < 5; row++) {
        for (uint8_t col = 0; col < 5; col++) {
            if (arrow_frames[arrow_index][row][col] > 0) {
                colors[row * 5 + col] = matrix_led_color(0.0, 1.0, 0.0);
            }
        }
    }
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        pio_sm_put_blocking(pio, sm, colors[i]);
    }
}

void play_buzzer_error() {
    uint buzzer_slice = pwm_gpio_to_slice_num(BUZZER_A_PIN);
    pwm_set_chan_level(buzzer_slice, PWM_CHAN_A, BUZZER_WRAP / 2);
    pwm_set_chan_level(buzzer_slice, PWM_CHAN_B, BUZZER_WRAP / 2);
    pwm_set_enabled(buzzer_slice, true);
    sleep_ms(BUZZER_DURATION_MS);
    pwm_set_enabled(buzzer_slice, false);
    pwm_set_chan_level(buzzer_slice, PWM_CHAN_A, 0);
    pwm_set_chan_level(buzzer_slice, PWM_CHAN_B, 0);
}

void play_buzzer_success() {
    uint buzzer_slice = pwm_gpio_to_slice_num(BUZZER_A_PIN);
    pwm_set_chan_level(buzzer_slice, PWM_CHAN_A, BUZZER_WRAP / 4);
    pwm_set_chan_level(buzzer_slice, PWM_CHAN_B, BUZZER_WRAP / 4);
    pwm_set_enabled(buzzer_slice, true);
    sleep_ms(BUZZER_SUCCESS_MS);
    pwm_set_enabled(buzzer_slice, false);
    pwm_set_chan_level(buzzer_slice, PWM_CHAN_A, 0);
    pwm_set_chan_level(buzzer_slice, PWM_CHAN_B, 0);
}

void update_rgb_lives() {
    uint slice_red_green = pwm_gpio_to_slice_num(RGB_RED_PIN);
    uint slice_blue = pwm_gpio_to_slice_num(RGB_BLUE_PIN);
    pwm_set_chan_level(slice_red_green, PWM_CHAN_A, 0);
    pwm_set_chan_level(slice_red_green, PWM_CHAN_B, 0);
    pwm_set_chan_level(slice_blue, PWM_CHAN_A, 0);

    if (game.lives == 3) {
        pwm_set_chan_level(slice_red_green, PWM_CHAN_B, RGB_PWM_WRAP);
    } else if (game.lives == 2) {
        pwm_set_chan_level(slice_red_green, PWM_CHAN_A, RGB_PWM_WRAP / 2);
        pwm_set_chan_level(slice_red_green, PWM_CHAN_B, RGB_PWM_WRAP / 2);
    } else if (game.lives == 1) {
        pwm_set_chan_level(slice_red_green, PWM_CHAN_A, RGB_PWM_WRAP);
    }
}

void update_oled_square() {
    static uint8_t last_x = 255, last_y = 255; // Última posição desenhada
    uint16_t joy_x, joy_y;
    read_joystick(&joy_x, &joy_y);
    printf("Joystick OLED: X=%u, Y=%u\n", joy_x, joy_y);

    int32_t mapped_x = ((int32_t)(joy_x - JOY_MIN) * (SSD_WIDTH - SQUARE_SIZE)) / (JOY_MAX - JOY_MIN);
    int32_t mapped_y = ((int32_t)(joy_y - JOY_MIN) * (SSD_HEIGHT - SQUARE_SIZE)) / (JOY_MAX - JOY_MIN);

    mapped_y = (SSD_HEIGHT - SQUARE_SIZE) - mapped_y;

    game.square_x = (mapped_x < 0) ? 0 : (mapped_x > SSD_WIDTH - SQUARE_SIZE) ? SSD_WIDTH - SQUARE_SIZE : mapped_x;
    game.square_y = (mapped_y < 0) ? 0 : (mapped_y > SSD_HEIGHT - SQUARE_SIZE) ? SSD_HEIGHT - SQUARE_SIZE : mapped_y;

    // Desenhar apenas se a posição mudou significativamente
    if (abs((int)game.square_x - last_x) > 1 || abs((int)game.square_y - last_y) > 1) {
        ssd1306_fill(&ssd, false);
        ssd1306_send_data(&ssd);
        ssd1306_rect(&ssd, game.square_y, game.square_x, SQUARE_SIZE, SQUARE_SIZE, true, true);
        ssd1306_send_data(&ssd);
        last_x = game.square_x;
        last_y = game.square_y;
    }
}

void reset_game() {
    game.lives = 3;
    game.sequence_length = 1;
    game.current_step = 0;
    game.rounds = 0;
    game.difficulty_level = 1;
    game.game_over = false;
    game.square_x = (SSD_WIDTH - SQUARE_SIZE) / 2;
    game.square_y = (SSD_HEIGHT - SQUARE_SIZE) / 2;
    generate_sequence();
    update_rgb_lives();
    button_confirm_pressed = false;
    button_joy_pressed = false;
    joy_x_min = 4095; joy_x_max = 0; joy_y_min = 4095; joy_y_max = 0; // Resetar extremos
    printf("Jogo reiniciado\n");
}

void generate_sequence() {
    for (uint8_t i = 0; i < game.sequence_length; i++) {
        game.sequence[i] = rand() % 4;
    }
}

uint32_t get_time_limit() {
    uint32_t time_limit = BASE_TIME_LIMIT_MS - (game.difficulty_level * TIME_DECREMENT_MS);
    return time_limit < MIN_TIME_LIMIT_MS ? MIN_TIME_LIMIT_MS : time_limit;
}

uint32_t matrix_led_color(float red, float green, float blue) {
    unsigned char G = green * 255;
    unsigned char R = red * 255;
    unsigned char B = blue * 255;
    return (G << 24) | (R << 16) | (B << 8);
}