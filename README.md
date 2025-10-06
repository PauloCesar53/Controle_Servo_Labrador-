# Controle_Servo_Labrador
Repósitorio com código utilizado para controle de um servo motor SG90 pela SBC Labrador

## Descrição geral do Funcionamento do programa 
O projeto consiste em um sistema microcontrolado para o controle preciso da velocidade de agitação de um motor, projetado para ser utilizado em um reator químico de pequena escala. O sistema permite ao operador ajustar a velocidade de mistura de forma incremental, garantindo a homogeneidade dos reagentes e o controle sobre as taxas de reação, processos fundamentais em experimentos químicos.

## Descrição detalhada do Funcionamento do programa  na BitDogLab
O programa foi desenvolvido em linguagem C para ser executado em uma placa SBC Labrador com sistema Linux. Seu principal objetivo é controlar o movimento de um servomotor e, ao mesmo tempo, usar dois LEDs como indicadores visuais da posição angular desse motor.

O funcionamento pode ser dividido nos seguintes passos:

Inicialização: Ao ser executado, o programa primeiro prepara o hardware. Ele ativa o canal de controle PWM (Modulação por Largura de Pulso), que é a técnica usada para enviar sinais de controle ao servomotor. Em paralelo, ele configura duas portas GPIO (pinos de entrada/saída) como saídas para controlar os LEDs.

Movimento Contínuo do Servo: O programa entra em um laço infinito que cria um movimento de varredura contínuo para o servomotor:

Primeiro, ele move o eixo do motor suavemente do ângulo 0° até 180°.

Ao chegar em 180°, ele faz uma pequena pausa e, em seguida, move o eixo de volta de 180° para 0°.

Esse movimento não é instantâneo; ele acontece em pequenos incrementos, com um atraso mínimo entre cada passo, resultando em um deslocamento fluido e preciso.

Lógica dos LEDs Indicadores: Enquanto o servomotor se move, o programa verifica constantemente sua posição para acender o LED correspondente:

LED 1 acende sempre que o ângulo do servo está na primeira metade do percurso 

LED 2 acende quando o ângulo está no percurso oposto 
Isso fornece um feedback visual imediato sobre em qual o sentido do moviemnto.


