#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <gpiod.h>

#define PWM_SYSFS_FMT "/sys/class/pwm/pwmchip%d"
#define PWM_CHANNEL_PATH_FMT "/sys/class/pwm/pwmchip%d/pwm%d"
#define PATH_MAX_LEN 256

// --- Funções auxiliares para manipulação de arquivos (sysfs) ---

static volatile sig_atomic_t stop_flag = 0;
void on_sig(int s) { (void)s; stop_flag = 1; }

int write_file_str(const char *path, const char *s) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t n = write(fd, s, (size_t)strlen(s));
    close(fd);
    return (n == (ssize_t)strlen(s)) ? 0 : -1;
}

int path_exists(const char *path) {
    return (access(path, F_OK) == 0);
}

int ensure_export_pwm(int chip, int channel) {
    char ch_path[PATH_MAX_LEN];
    char buf[32];
    snprintf(ch_path, sizeof(ch_path), PWM_CHANNEL_PATH_FMT, chip, channel);
    if (path_exists(ch_path)) return 0;
    snprintf(ch_path, sizeof(ch_path), PWM_SYSFS_FMT "/export", chip);
    snprintf(buf, sizeof(buf), "%d", channel);
    if (write_file_str(ch_path, buf) < 0) { perror("export pwm"); return -1; }
    snprintf(ch_path, sizeof(ch_path), PWM_CHANNEL_PATH_FMT, chip, channel);
    for (int i = 0; i < 50; ++i) { if (path_exists(ch_path)) return 0; usleep(100000); }
    fprintf(stderr, "timeout esperando pwm aparecer em sysfs\n");
    return -1;
}

int pwm_set_period(int chip, int channel, unsigned int ns) {
    char p[PATH_MAX_LEN], v[64];
    snprintf(p, sizeof(p), PWM_CHANNEL_PATH_FMT "/period", chip, channel);
    snprintf(v, sizeof(v), "%u", ns);
    return write_file_str(p, v);
}

int pwm_set_duty(int chip, int channel, unsigned int ns) {
    char p[PATH_MAX_LEN], v[64];
    snprintf(p, sizeof(p), PWM_CHANNEL_PATH_FMT "/duty_cycle", chip, channel);
    snprintf(v, sizeof(v), "%u", ns);
    return write_file_str(p, v);
}

int pwm_enable(int chip, int channel, int en) {
    char p[PATH_MAX_LEN], v[8];
    snprintf(p, sizeof(p), PWM_CHANNEL_PATH_FMT "/enable", chip, channel);
    snprintf(v, sizeof(v), "%d", en);
    return write_file_str(p, v);
}

// --- Lógica de controle do servo de POSIÇÃO ---

/*
 * Calibração do Servo (ajuste se necessário):
 * A maioria dos servos SG90 opera com pulsos entre 0.5ms e 2.5ms.
 * 0.5ms  (~500000 ns)  -> 0 graus
 * 1.5ms  (~1500000 ns) -> 90 graus
 * 2.5ms  (~2500000 ns) -> 180 graus
 */
#define MIN_PULSE_NS 500000u
#define MAX_PULSE_NS 2500000u

/**
 * @brief Mapeia um ângulo (0-180) para a largura de pulso correspondente em nanossegundos.
 * @param angle O ângulo desejado, de 0.0 a 180.0.
 * @return A largura do pulso em nanossegundos.
 */
unsigned int angle_to_pulse_ns(double angle) {
    if (angle < 0.0) angle = 0.0;
    if (angle > 180.0) angle = 180.0;

    // Interpolação linear
    return (unsigned int)(MIN_PULSE_NS + (angle / 180.0) * (double)(MAX_PULSE_NS - MIN_PULSE_NS));
}


// --- CONFIGURAÇÕES PADRÃO (ajuste apenas se necessário) ---
#define PWM_CHIP_DEFAULT 0
#define PWM_CHANNEL_DEFAULT 0
#define GPIOCHIP_NAME_DEFAULT "gpiochip0"
#define LED0_OFFSET_DEFAULT 27 // LED 1
#define LED1_OFFSET_DEFAULT 22 // LED 2


int main(void) {
    int pwmchip = PWM_CHIP_DEFAULT;
    int pwmchan = PWM_CHANNEL_DEFAULT;
    const char *gpiochip_name = GPIOCHIP_NAME_DEFAULT;
    unsigned int led0_off = LED0_OFFSET_DEFAULT;
    unsigned int led1_off = LED1_OFFSET_DEFAULT;

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    // 1. Inicializa o módulo PWM
    if (ensure_export_pwm(pwmchip, pwmchan) < 0) {
        fprintf(stderr, "Falha ao exportar PWM. Verifique permissões.\n");
        return 1;
    }

    // 3. Define a frequência do PWM para ~50Hz (período de 20ms)
    const unsigned int period_ns = 20000000u;
    if (pwm_set_period(pwmchip, pwmchan, period_ns) < 0) perror("set period");
    
    if (pwm_set_duty(pwmchip, pwmchan, angle_to_pulse_ns(0)) < 0) perror("set initial duty");
    if (pwm_enable(pwmchip, pwmchan, 1) < 0) perror("enable pwm");
    
    // 2. Inicializa as GPIOs dos LEDs
    struct gpiod_chip *chip = gpiod_chip_open_by_name(gpiochip_name);
    if (!chip) {
        fprintf(stderr, "Erro: nao conseguiu abrir %s\n", gpiochip_name);
        perror("gpiod open");
        goto cleanup_pwm;
    }

    struct gpiod_line *led0 = gpiod_chip_get_line(chip, led0_off);
    struct gpiod_line *led1 = gpiod_chip_get_line(chip, led1_off);
    if (!led0 || !led1) {
        fprintf(stderr, "Erro ao obter linhas GPIO.\n");
        goto cleanup_chip;
    }

    if (gpiod_line_request_output(led0, "led0", 0) < 0) { perror("req out led0"); goto cleanup_chip; }
    if (gpiod_line_request_output(led1, "led1", 0) < 0) { perror("req out led1"); gpiod_line_release(led0); goto cleanup_chip; }

    printf("Iniciando varredura suave do servo. Pressione Ctrl+C para parar.\n");

    // --- PARÂMETROS DE SUAVIDADE E VELOCIDADE ---
    // Passo de cada incremento/decremento de ângulo. Menor = mais suave.
    const double ANGLE_STEP = 0.5; // meio grau por passo
    
    // Delay entre cada passo. Menor = mais rápido.
    // (15000us para 1 grau) -> (7500us para 0.5 grau) mantém a velocidade original.
    const useconds_t step_delay_us = 1000; 

    // 6. Loop principal
    while (!stop_flag) {

        // 4. Varre de 0 a 180 graus (incrementa)
        for (double angle = 0.0; angle <= 180.0; angle += ANGLE_STEP) {
            if (stop_flag) break;

            unsigned int duty_ns = angle_to_pulse_ns(angle);
            if (pwm_set_duty(pwmchip, pwmchan, duty_ns) < 0) {
                perror("set duty (0-180)");
            }

            // 7 & 8. Controle dos LEDs
            if (angle <= 90.0) {
                gpiod_line_set_value(led0, 1); // LED 1 ON
                gpiod_line_set_value(led1, 0); // LED 2 OFF
            } else {
                gpiod_line_set_value(led0, 0); // LED 1 OFF
                gpiod_line_set_value(led1, 1); // LED 2 ON
            }
            usleep(step_delay_us);
        }

        if (stop_flag) break;
        usleep(500000); // Pausa de 0.5s no final do curso

        // 5. Varre de 180 a 0 graus (decrementa)
        for (double angle = 180.0; angle >= 0.0; angle -= ANGLE_STEP) {
            if (stop_flag) break;

            unsigned int duty_ns = angle_to_pulse_ns(angle);
            if (pwm_set_duty(pwmchip, pwmchan, duty_ns) < 0) {
                perror("set duty (180-0)");
            }

            // 7 & 8. Controle dos LEDs
            if (angle <= 90.0) {
                gpiod_line_set_value(led0, 1); // LED 1 ON
                gpiod_line_set_value(led1, 0); // LED 2 OFF
            } else {
                gpiod_line_set_value(led0, 0); // LED 1 OFF
                gpiod_line_set_value(led1, 1); // LED 2 ON
            }
            usleep(step_delay_us);
        }
        
        if (stop_flag) break;
        usleep(500000); // Pausa de 0.5s no final do curso
    }

    // --- Rotina de Limpeza ---
    printf("\nEncerrando e limpando os recursos...\n");
    gpiod_line_set_value(led0, 0);
    gpiod_line_set_value(led1, 0);
    gpiod_line_release(led0);
    gpiod_line_release(led1);

cleanup_chip:
    if (chip) gpiod_chip_close(chip);

cleanup_pwm:
    pwm_enable(pwmchip, pwmchan, 0);
    
    printf("Programa encerrado.\n");
    return 0;
}
