#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "./lib/ssd1306.h"
#include "./lib/font.h"
#include "./lib/frames.h"
#include "pio_matrix.pio.h"

// Comunicação Serial I2C
#define I2C_PORT i2c1
#define I2C_SDA_PIN 14
#define I2C_SCL_PIN 15

// Definições do Display
#define SSD_ADDR 0x3C
#define SSD_WIDTH 128
#define SSD_HEIGHT 64
#define SQUARE_SIZE 8

// Definições do PWM para Buzzers (2 kHz)
#define BUZZER_WRAP 62500
#define BUZZER_CLK_DIV 1.0
#define BUZZER_START_MS 100

// Buzzers
#define BUZZER_A_PIN 21
#define BUZZER_B_PIN 10

// LED RGB
#define RGB_RED_PIN 13
#define RGB_GREEN_PIN 11
#define RGB_BLUE_PIN 12
#define RGB_PWM_WRAP 255
#define RGB_CLK_DIV 1.0

// Matriz de LEDs
#define MATRIZ_LEDS_PIN 7
#define NUM_LEDS 25

// Joystick
#define JOY_X_PIN 27 // ADC0
#define JOY_Y_PIN 26 // ADC1
#define BUTTON_CONFIRM_PIN 6

// Definições do Jogo
#define MAX_SEQUENCE 20
#define BASE_TIME_LIMIT_MS 10000 // 10s inicial
#define TIME_DECREMENT_MS 200 // Decremento por nível
#define MIN_TIME_LIMIT_MS 6000 // 6s mínimo
#define ROUNDS_PER_LEVEL 5
#define JOY_MARGIN 300
#define JOY_UP_MIN (4095 - JOY_MARGIN)
#define JOY_DOWN_MAX JOY_MARGIN
#define JOY_LEFT_MAX JOY_MARGIN
#define JOY_RIGHT_MIN (4095 - JOY_MARGIN)
#define BASE_REACTION_MS 3500
#define REACTION_DECREMENT_MS 100
#define MIN_REACTION_MS 2000
#define REACTION_GAMEOVER_MS 4000
#define BASE_ARROW_DISPLAY_MS 2000
#define ARROW_DECREMENT_MS 50
#define MIN_ARROW_DISPLAY_MS 1000
#define ARROW_PAUSE_MS 200
#define LEVEL_DISPLAY_MS 1000

// Frames das setas e reações
extern float arrow_frames[4][5][5];
extern float reaction_frames[3][5][5];

// Estrutura do estado do jogo
typedef struct {
    uint8_t lives;
    uint8_t sequence[MAX_SEQUENCE];
    uint8_t player_sequence[MAX_SEQUENCE];
    uint8_t sequence_length;
    uint8_t player_steps;
    uint32_t rounds;
    uint8_t difficulty_level;
    bool game_over;
} GameState;

// Variáveis globais
ssd1306_t ssd;
PIO pio = pio0;
uint sm;
GameState game;
volatile bool button_confirm_pressed = false;

// Protótipos
void setup();
void init_buttons();
void init_buzzers();
void init_rgb();
void init_joystick();
void init_i2c_display();
void init_matrix_leds();
void button_irq_handler(uint gpio, uint32_t events);
void display_arrow(uint8_t arrow_index);
void display_reaction(uint8_t reaction_index, float r, float g, float b);
void start_buzzer();
void update_rgb_lives();
void update_oled_square();
void reset_game();
void generate_sequence();
uint32_t matrix_led_color(float red, float green, float blue);
uint32_t get_time_limit();
uint32_t get_arrow_display_time();
uint32_t get_reaction_time();
void read_joystick(uint16_t *joy_x, uint16_t *joy_y);
void clear_matrix();
void update_dynamic_arrow();
void show_level();
void show_sequence();
bool player_input();
void show_reaction(bool success);
void show_game_over();

int main() {
    stdio_init_all();
    setup();
    reset_game();

    // Tela inicial
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "JOGO DE SETAS", (SSD_WIDTH/2) - ((sizeof("JOGO DE SETAS") * 8) / 2), 20);
    ssd1306_draw_string(&ssd, "Pressione Botao", (SSD_WIDTH/2) - ((sizeof("Pressione Botao") * 8) / 2), 40);
    ssd1306_send_data(&ssd);

    while (!button_confirm_pressed) tight_loop_contents();
    button_confirm_pressed = false;

    while (true) {
        show_level();
        show_sequence();
        bool success = player_input();
        show_reaction(success);
        if (game.lives == 0) {
            show_game_over();
            reset_game();
        } else {
            game.rounds++;
            game.difficulty_level = (game.rounds / ROUNDS_PER_LEVEL) + 1;
            game.sequence_length = game.difficulty_level;
            if (game.sequence_length > MAX_SEQUENCE) {
                game.sequence_length = MAX_SEQUENCE;
            }
        }
    }
}

void setup() {
    init_buttons();
    init_buzzers();
    init_rgb();
    init_joystick();
    init_matrix_leds();
    init_i2c_display();
}

void init_buttons() {
    gpio_init(BUTTON_CONFIRM_PIN);
    gpio_set_dir(BUTTON_CONFIRM_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_CONFIRM_PIN);
    gpio_set_irq_enabled_with_callback(BUTTON_CONFIRM_PIN, GPIO_IRQ_EDGE_FALL, true, button_irq_handler);
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

void init_i2c_display() {
    i2c_init(I2C_PORT, 400000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
    ssd1306_init(&ssd, SSD_WIDTH, SSD_HEIGHT, false, SSD_ADDR, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);
}

void button_irq_handler(uint gpio, uint32_t events) {
    static uint32_t last_time = 0;
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    if (current_time - last_time >= 300) {
        if (gpio == BUTTON_CONFIRM_PIN) {
            button_confirm_pressed = true;
        }
        last_time = current_time;
    }
}

void display_arrow(uint8_t arrow_index) {
    uint32_t colors[NUM_LEDS] = {0};
    for (uint8_t row = 0; row < 5; row++) {
        for (uint8_t col = 0; col < 5; col++) {
            if (arrow_frames[arrow_index][row][col] > 0) {
                colors[row * 5 + col] = matrix_led_color(0.0, 0.0, 1.0); // Azul
            }
        }
    }
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        pio_sm_put_blocking(pio, sm, colors[i]);
    }
}

void display_reaction(uint8_t reaction_index, float r, float g, float b) {
    uint32_t colors[NUM_LEDS] = {0};
    for (uint8_t row = 0; row < 5; row++) {
        for (uint8_t col = 0; col < 5; col++) {
            if (reaction_frames[reaction_index][4 - row][col] > 0) {
                colors[row * 5 + col] = matrix_led_color(r, g, b);
            }
        }
    }
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        pio_sm_put_blocking(pio, sm, colors[i]);
    }
}

void clear_matrix() {
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        pio_sm_put_blocking(pio, sm, 0);
    }
}

void start_buzzer() {
    uint buzzer_slice = pwm_gpio_to_slice_num(BUZZER_A_PIN);
    pwm_set_chan_level(buzzer_slice, PWM_CHAN_A, BUZZER_WRAP / 4);
    pwm_set_chan_level(buzzer_slice, PWM_CHAN_B, BUZZER_WRAP / 4);
    pwm_set_enabled(buzzer_slice, true);
    sleep_ms(BUZZER_START_MS);
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
        pwm_set_chan_level(slice_red_green, PWM_CHAN_B, RGB_PWM_WRAP); // Verde
    } else if (game.lives == 2) {
        pwm_set_chan_level(slice_red_green, PWM_CHAN_A, RGB_PWM_WRAP / 2); // Amarelo
        pwm_set_chan_level(slice_red_green, PWM_CHAN_B, RGB_PWM_WRAP / 2);
    } else if (game.lives == 1) {
        pwm_set_chan_level(slice_red_green, PWM_CHAN_A, RGB_PWM_WRAP); // Vermelho
    }
}

void update_oled_square() {
    static uint8_t current_pos_x = (SSD_HEIGHT / 2) - (SQUARE_SIZE / 2);
    static uint8_t current_pos_y = (SSD_WIDTH / 2) - (SQUARE_SIZE / 2);
    uint16_t joy_x, joy_y;
    read_joystick(&joy_x, &joy_y);

    uint mov_div_x = 4096 / SSD_HEIGHT;
    uint mov_div_y = 4096 / SSD_WIDTH;

    ssd1306_fill(&ssd, false);
    uint8_t new_pos_x = (uint8_t)((4095 - joy_x) / mov_div_x);
    uint8_t new_pos_y = (uint8_t)(joy_y / mov_div_y);

    if (new_pos_x <= SSD_HEIGHT - SQUARE_SIZE) current_pos_x = new_pos_x;
    if (new_pos_y <= SSD_WIDTH - SQUARE_SIZE) current_pos_y = new_pos_y;

    ssd1306_rect(&ssd, current_pos_x, current_pos_y, SQUARE_SIZE, SQUARE_SIZE, true, true);
    ssd1306_send_data(&ssd);
}

void reset_game() {
    game.lives = 3;
    game.sequence_length = 1;
    game.player_steps = 0;
    game.rounds = 0;
    game.difficulty_level = 1;
    game.game_over = false;
    memset(game.sequence, 0, MAX_SEQUENCE);
    memset(game.player_sequence, 0, MAX_SEQUENCE);
    update_rgb_lives();
    button_confirm_pressed = false;
    clear_matrix();
}

void generate_sequence() {
    memset(game.sequence, 0, MAX_SEQUENCE);
    memset(game.player_sequence, 0, MAX_SEQUENCE);
    // Inicializa srand com o tempo atual para maior aleatoriedade
    srand(to_ms_since_boot(get_absolute_time()));
    for (uint8_t i = 0; i < game.sequence_length; i++) {
        game.sequence[i] = rand() % 4; // 0 a 3 para direções diferentes
    }
    game.player_steps = 0;
}

uint32_t get_time_limit() {
    uint32_t time_limit = BASE_TIME_LIMIT_MS - (game.difficulty_level * TIME_DECREMENT_MS);
    return time_limit < MIN_TIME_LIMIT_MS ? MIN_TIME_LIMIT_MS : time_limit;
}

uint32_t get_arrow_display_time() {
    uint32_t display_time = BASE_ARROW_DISPLAY_MS - (game.difficulty_level * ARROW_DECREMENT_MS);
    return display_time < MIN_ARROW_DISPLAY_MS ? MIN_ARROW_DISPLAY_MS : display_time;
}

uint32_t get_reaction_time() {
    uint32_t reaction_time = BASE_REACTION_MS - (game.difficulty_level * REACTION_DECREMENT_MS);
    return reaction_time < MIN_REACTION_MS ? MIN_REACTION_MS : reaction_time;
}

uint32_t matrix_led_color(float red, float green, float blue) {
    unsigned char G = green * 255;
    unsigned char R = red * 255;
    unsigned char B = blue * 255;
    return (G << 24) | (R << 16) | (B << 8);
}

void read_joystick(uint16_t *joy_x, uint16_t *joy_y) {
    uint32_t sum_x = 0, sum_y = 0;
    const int samples = 3;
    for (int i = 0; i < samples; i++) {
        adc_select_input(0);
        sum_x += adc_read();
        adc_select_input(1);
        sum_y += adc_read();
    }
    *joy_x = sum_x / samples;
    *joy_y = sum_y / samples;
}

void update_dynamic_arrow() {
    uint16_t joy_x, joy_y;
    read_joystick(&joy_x, &joy_y);
    if (joy_x >= JOY_UP_MIN) display_arrow(1); // Baixo
    else if (joy_x <= JOY_DOWN_MAX) display_arrow(0); // Cima
    else if (joy_y <= JOY_LEFT_MAX) display_arrow(2); // Esquerda
    else if (joy_y >= JOY_RIGHT_MIN) display_arrow(3); // Direita
    else clear_matrix();
}

void show_level() {
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "Nivel: %u Rodada: %lu", game.difficulty_level, game.rounds + 1);
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, buffer, (SSD_WIDTH/2) - ((strlen(buffer) * 8) / 2), 30);
    ssd1306_send_data(&ssd);
    printf("Nível: %u, Rodada: %lu\n", game.difficulty_level, game.rounds + 1);
    sleep_ms(LEVEL_DISPLAY_MS);
}

void show_sequence() {
    generate_sequence();
    for (uint8_t i = 0; i < game.sequence_length; i++) {
        uint32_t arrow_time = get_arrow_display_time();
        printf("Seta %u/%u: %lums (Direção=%u)\n", i + 1, game.sequence_length, arrow_time, game.sequence[i]);
        display_arrow(game.sequence[i]);
        sleep_ms(arrow_time);
        clear_matrix();
        sleep_ms(ARROW_PAUSE_MS);
    }
}

bool player_input() {
    start_buzzer();
    game.player_steps = 0;
    // Inicia o tempo após a sequência, quando entrada do jogador começa
    uint32_t start_time = to_ms_since_boot(get_absolute_time());
    uint32_t time_limit = get_time_limit();
    printf("Entrada do jogador, limite: %lums\n", time_limit);

    while (game.player_steps < game.sequence_length) {
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        if (current_time - start_time > time_limit) {
            game.lives--;
            update_rgb_lives();
            printf("Timeout! Vidas: %u\n", game.lives);
            return false;
        }

        update_oled_square();
        update_dynamic_arrow();

        if (button_confirm_pressed) {
            button_confirm_pressed = false;
            uint16_t joy_x, joy_y;
            read_joystick(&joy_x, &joy_y);
            uint8_t direction = 0;
            if (joy_x >= JOY_UP_MIN) direction = 1;
            else if (joy_x <= JOY_DOWN_MAX) direction = 0;
            else if (joy_y <= JOY_LEFT_MAX) direction = 2;
            else if (joy_y >= JOY_RIGHT_MIN) direction = 3;

            game.player_sequence[game.player_steps] = direction;
            game.player_steps++;
            printf("Entrada %u: Direção=%u (Esperado=%u)\n", 
                   game.player_steps, direction, game.sequence[game.player_steps - 1]);
        }
        sleep_ms(10);
    }

    for (uint8_t i = 0; i < game.sequence_length; i++) {
        if (game.player_sequence[i] != game.sequence[i]) {
            game.lives--;
            update_rgb_lives();
            printf("Erro! Vidas: %u\n", game.lives);
            return false;
        }
    }
    printf("Sucesso!\n");
    return true;
}

void show_reaction(bool success) {
    uint32_t reaction_time = get_reaction_time();
    if (success) {
        printf("Reação de sucesso: %lums\n", reaction_time);
        display_reaction(0, 0.0, 1.0, 0.0); // Verde
        sleep_ms(reaction_time);
    } else {
        printf("Reação de erro: %lums\n", reaction_time);
        display_reaction(1, 1.0, 0.0, 0.0); // Vermelho
        sleep_ms(reaction_time);
    }
    clear_matrix();
}

void show_game_over() {
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "Rounds: %lu Nivel: %u", game.rounds, game.difficulty_level);
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "GAME OVER", (SSD_WIDTH/2) - ((sizeof("GAME OVER") * 8) / 2), 20);
    ssd1306_draw_string(&ssd, buffer, (SSD_WIDTH/2) - ((strlen(buffer) * 8) / 2), 40);
    ssd1306_send_data(&ssd);
    printf("Game Over: %lums\n", REACTION_GAMEOVER_MS);
    display_reaction(2, 1.0, 0.0, 0.0); // Vermelho
    sleep_ms(REACTION_GAMEOVER_MS);
    clear_matrix();
}