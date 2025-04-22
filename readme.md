# Jogo de Setas

Este projeto implementa um jogo interativo de memória e reflexos em um microcontrolador **Raspberry Pi Pico**, utilizando:

- Matriz de LEDs **WS2812B 5x5**
- Display OLED **SSD1306**
- **Joystick analógico**
- **Buzzers**
- **LED RGB**

O jogador deve memorizar e reproduzir sequências aleatórias de setas (cima, baixo, esquerda, direita) exibidas na matriz, com dificuldade crescente conforme as rodadas avançam.

O objetivo é criar uma experiência divertida e desafiadora, com feedback visual e sonoro — ideal para aprendizado em **sistemas embarcados** e **desenvolvimento de jogos**.

## Estrutura do Projeto

```
arrow_game/
├── Arrow_Game.c          # Código principal do projeto
├── CMakeLists.txt        # Arquivo de configuração para compilação com Pico SDK
├── inc/                  # Arquivos auxiliares (bibliotecas para display, fontes, frames)
└── pio_matrix.pio        # Código PIO para controle da matriz de LEDs WS2812B
```

## Especificações do Projeto

### Periféricos Utilizados
- **Matriz de LEDs WS2812B 5x5**: Exibe setas e reações (sucesso, erro, game over).
- **Display OLED SSD1306 128x64**: Mostra nível, rodada, posição do joystick e tela de game over.
- **Joystick Analógico**: Controla a entrada do jogador (eixos X/Y) com botão integrado.
- **Botão de Confirmação**: Valida as entradas durante a fase de resposta.
- **2 Buzzers Passivos**: Emitirão sons ao início da fase de entrada.
- **LED RGB**: Indica número de vidas (verde: 3, amarelo: 2, vermelho: 1).

### Recursos do MCU (RP2040)
- **PIO (Programmable I/O)**: Comunicação com a matriz de LEDs WS2812B.
- **I2C**: Comunicação com o display OLED SSD1306.
- **ADC (Analog-to-Digital Converter)**: Leitura dos eixos do joystick analógico.
- **PWM**: Controle dos buzzers e do LED RGB.
- **GPIO**: Gerenciamento de botões e sinais digitais.

## Materiais Necessários

- Raspberry Pi Pico ou Pico W
- Cabo micro-USB para USB-A
- Protoboard e fios jumper
- Computador para compilação e upload do código

## Softwares Utilizados

- **Visual Studio Code** (recomendado)
- **Pico SDK** (biblioteca oficial para RP2040)
- **ARM GCC** (compilador C)
- **CMake** (gerenciamento de build)

## Como Utilizar

### Configurar o Hardware

| Componente        | Pino no Pico   |
|-------------------|----------------|
| Matriz WS2812B     | GP7             |
| Display OLED (SDA) | GP14            |
| Display OLED (SCL) | GP15            |
| Joystick (X)       | GP27 (ADC1)     |
| Joystick (Y)       | GP26 (ADC0)     |
| Botão do Joystick  | GP6             |
| Buzzer 1           | GP21            |
| Buzzer 2           | GP10            |
| LED RGB (Vermelho) | GP13            |
| LED RGB (Verde)    | GP11            |
| LED RGB (Azul)     | GP12            |

### Compilar e Carregar o Código

```bash
git clone <URL_DO_REPOSITORIO>
cd arrow_game
mkdir build
cd build
cmake ..
make
```

- Copie o arquivo `.uf2` gerado para o Pico via modo **bootloader**.

## Como Jogar

### Fase Inicial
- Conecte o Pico ao computador via USB.
- Pressione o botão do joystick para iniciar o jogo.

### Fase de Exibição
- A matriz de LEDs mostrará uma sequência de setas (cima, baixo, esquerda, direita).
- Cada seta é exibida por **1000 a 2000 ms**, com pausas de **200 ms**.

### Fase de Entrada
- Movimente o joystick para escolher a direção correta.
- Pressione o botão para confirmar.
- Você terá **6 a 10 segundos** para repetir toda a sequência.

### Feedback Visual e Sonoro
- **Acerto**: Ícone verde por **2–3.5 segundos**.
- **Erro/Timeout**: Ícone vermelho + perda de uma vida.
- **Game Over**: Mensagem no display OLED + matriz vermelha por **4 segundos**.

### Indicadores

| Indicador            | Função                             |
|----------------------|------------------------------------|
| Matriz de LEDs        | Exibe setas e reações              |
| Display OLED          | Mostra nível, rodada e status      |
| LED RGB               | Indica vidas (verde, amarelo, vermelho) |
| Buzzers               | Sinalizam início da fase de entrada |

## Vídeo Demonstrativo

[Assista ao vídeo demonstrativo](https://drive.google.com/file/d/12Lp5miLpn3lDZJ-NJguo7utmLh1KyGb-/view?usp=sharing)

## Autor

Desenvolvido por **Elmer Carvalho**  
[GitHub - Perfil](https://github.com/Elmer-Carvalho)

## Licença

Este projeto está licenciado sob a licença MIT.  
Consulte o arquivo [LICENSE](LICENSE) para mais informações.