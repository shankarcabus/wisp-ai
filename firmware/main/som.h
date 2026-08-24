#pragma once
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "ui.h"

/* O som da placa.
 *
 * ES8311 no I2C 0x18, I2S nos GPIOs 19/20/22/23, e o amplificador vivendo do
 * rail ALDO2 — não há GPIO de habilitação nesta placa, então quem "liga o
 * amplificador" é o bsp_amp_power().
 *
 * Recebe o barramento I2C JÁ ABERTO, como o pmic_start(): o codec divide o bus
 * com o PMIC, o touch, o IMU e o RTC, e abrir um segundo bus nos mesmos pinos é
 * como se ganha um dia de depuração. Chamar no BOOT, junto do pmic — o main.c
 * documenta, com medição, que abrir dispositivo I2C dentro da task de rede
 * trava a task inteira. */
esp_err_t som_init(i2c_master_bus_handle_t bus);

/* WISP_VOL_* -> volume do codec. Chamável a cada payload: igual ao que já vale
 * não faz nada. */
void som_volume(uint8_t degrau);

/* Enfileira UMA reprodução do som do estado. Não bloqueia — a escrita no I2S
 * roda em task própria, porque o laço do LVGL não pode esperar por ela.
 * Chamada durante uma reprodução é ignorada, e não empilhada: dois avisos
 * sobrepostos não informam o dobro. */
void som_tocar(wisp_state_t s);
