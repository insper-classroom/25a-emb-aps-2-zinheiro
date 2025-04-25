# Controle Customizado para Shell Shockers

Este repositório contém o firmware em C para Raspberry Pi Pico e o script em Python que, juntos, implementam um controle físico para o jogo Shell Shockers.


1. Jogo

Shell Shockers é um jogo de tiro em primeira pessoa que roda em navegador, onde cada jogador controla um ovo armado. O jogo utiliza movimentação de mira (mouse) e comandos de teclado (WASD, espaço, R, etc.). Nosso objetivo é substituir mouse e teclado por um controle físico dedicado.


2. Ideia do Controle

- Utilizar um Raspberry Pi Pico como microcontrolador.

- Ler dois joysticks analógicos (eixos X/Y) multiplexados por um CD4051, totalizando 4 entradas ADC.

- Ler 5 botões de ação: mira, atirar, pular, recarregar e trocar de arma.

- Botão extra de liga/desliga do envio de comandos.

- Indicador de estado via LED.

- Feedback sonoro de tiro via buzzer.

- Envio dos comandos ao PC via UART.

- Interpreter em Python lê a serial e converte em movimento de mouse e cliques com PyAutoGUI.


3. Inputs e Outputs

   - Input:
     1. Joystick X/Y (2)                                      ---------------> ADC via MUX
     2. Botoões: mira, atirar, pular, recarregar, trocar arma ---------------> GPIO 16-20
     3. Botão liga/desliga                                    ---------------> GPIO 14
    
  - Output:
    1. LED                                                    ---------------> GPIO 2
    2. Buzzer                                                 ---------------> GPIO 15
   

4. Protocolo Utilizado



5. Diadrama de blocos


6. Imagem do controle











