#include "rgb_led.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "rgb_led";

/* Filets de sécurité si Kconfig absent */
#ifndef CONFIG_RGB_LED_PIN_R
#define CONFIG_RGB_LED_PIN_R 15
#endif
#ifndef CONFIG_RGB_LED_PIN_G
#define CONFIG_RGB_LED_PIN_G 16
#endif
#ifndef CONFIG_RGB_LED_PIN_B
#define CONFIG_RGB_LED_PIN_B 17
#endif
#ifndef CONFIG_RGB_LED_COMMON_ANODE
#define CONFIG_RGB_LED_COMMON_ANODE 1
#endif
#ifndef CONFIG_RGB_LED_PWM_FREQ_HZ
#define CONFIG_RGB_LED_PWM_FREQ_HZ 5000
#endif
#ifndef CONFIG_RGB_LED_PWM_RES_BITS
#define CONFIG_RGB_LED_PWM_RES_BITS 8
#endif

/* Param LEDC */
#define RGB_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define RGB_LEDC_TIMER     LEDC_TIMER_0
#define CH_R               LEDC_CHANNEL_0
#define CH_G               LEDC_CHANNEL_1
#define CH_B               LEDC_CHANNEL_2

static uint32_t max_duty(void) {
    return (1u << CONFIG_RGB_LED_PWM_RES_BITS) - 1u;
}

static inline uint32_t apply_polarity(uint32_t duty) {
#if CONFIG_RGB_LED_COMMON_ANODE
    return max_duty() - duty;
#else
    return duty;
#endif
}

static esp_err_t set_duty_channel(ledc_channel_t ch, uint32_t duty) {
    ESP_RETURN_ON_ERROR(ledc_set_duty(RGB_LEDC_MODE, ch, apply_polarity(duty)), TAG, "set_duty");
    return ledc_update_duty(RGB_LEDC_MODE, ch);
}

esp_err_t rgb_led_init(void)
{
    ESP_LOGI(TAG, "Init RGB: R=%d G=%d B=%d, anode=%d, %u Hz, %u bits",
             CONFIG_RGB_LED_PIN_R, CONFIG_RGB_LED_PIN_G, CONFIG_RGB_LED_PIN_B,
             (int)CONFIG_RGB_LED_COMMON_ANODE, CONFIG_RGB_LED_PWM_FREQ_HZ, CONFIG_RGB_LED_PWM_RES_BITS);

    ledc_timer_config_t tcfg = {
        .speed_mode       = RGB_LEDC_MODE,
        .duty_resolution  = CONFIG_RGB_LED_PWM_RES_BITS,
        .timer_num        = RGB_LEDC_TIMER,
        .freq_hz          = CONFIG_RGB_LED_PWM_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&tcfg), TAG, "timer");

    const int pins[3] = { CONFIG_RGB_LED_PIN_R, CONFIG_RGB_LED_PIN_G, CONFIG_RGB_LED_PIN_B };
    const ledc_channel_t ch[3] = { CH_R, CH_G, CH_B };

    for (int i = 0; i < 3; ++i) {
        ledc_channel_config_t ccfg = {
            .gpio_num   = pins[i],
            .speed_mode = RGB_LEDC_MODE,
            .channel    = ch[i],
            .intr_type  = LEDC_INTR_DISABLE,
            .timer_sel  = RGB_LEDC_TIMER,
            .duty       = apply_polarity(0),
            .hpoint     = 0
        };
        ESP_RETURN_ON_ERROR(ledc_channel_config(&ccfg), TAG, "chan");
    }
    return ESP_OK;
}

esp_err_t rgb_led_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t mr = (uint32_t)r * max_duty() / 255u;
    uint32_t mg = (uint32_t)g * max_duty() / 255u;
    uint32_t mb = (uint32_t)b * max_duty() / 255u;

    ESP_RETURN_ON_ERROR(set_duty_channel(CH_R, mr), TAG, "R");
    ESP_RETURN_ON_ERROR(set_duty_channel(CH_G, mg), TAG, "G");
    ESP_RETURN_ON_ERROR(set_duty_channel(CH_B, mb), TAG, "B");
    return ESP_OK;
}

/* Conversion HSV (0..359, 0..255, 0..255) -> RGB 0..255 */
static void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (s == 0) { *r = *g = *b = v; return; }
    uint16_t region = h / 60;
    uint16_t remainder = (h % 60) * 255 / 60;
    uint32_t p = (uint32_t)v * (255 - s) / 255;
    uint32_t q = (uint32_t)v * (255 - (s * remainder) / 255) / 255;
    uint32_t t = (uint32_t)v * (255 - (s * (255 - remainder)) / 255) / 255;

    switch (region % 6) {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        default:*r = v; *g = p; *b = q; break;
    }
}

esp_err_t rgb_led_set_hsv(uint16_t h, uint8_t s, uint8_t v)
{
    if (h > 359) h %= 360;
    uint8_t r,g,b;
    hsv_to_rgb(h,s,v,&r,&g,&b);
    return rgb_led_set_rgb(r,g,b);
}

void rgb_led_deinit(void)
{
    /* Optionnel : repasser les pins à 0 et désinitialiser */
    rgb_led_set_rgb(0,0,0);
    ledc_stop(RGB_LEDC_MODE, CH_R, 0);
    ledc_stop(RGB_LEDC_MODE, CH_G, 0);
    ledc_stop(RGB_LEDC_MODE, CH_B, 0);
}
