// main.c
// contains the entrypoint
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"

#include "u8g2.h"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "ws2812.h"

#include "device_config.h"
#include "app.h"

#include "display.h"
#include "gui.h"
#include "receiver.h"
#include "input.h"

static const char* TAG = "main";
char str_buf[32];

static app_state_t app_state;

static ws2812_t status_led;
static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc1_cali_handle;
static splash_draw_config_t splash_config;



void setup_spi_bus(gpio_num_t sclk_pin, gpio_num_t mosi_pin, gpio_num_t miso_pin)
{
     spi_bus_config_t buscfg = {
        .sclk_io_num = sclk_pin,
        .mosi_io_num = mosi_pin,
        .miso_io_num = miso_pin,
        .quadwp_io_num = -1, // not QSPI
        .quadhd_io_num = -1, // not QSPI
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO)); // Enable the DMA feature
}


// initialise adc1 and 
void setup_adc()
{
    // init adc unit
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));


    #if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        // calibrate the adc unit
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .atten = APP_ADC_ATTEN,
            .bitwidth = APP_ADC_BITWIDTH,
        };
        ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc1_cali_handle));

    #endif

    #if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        // calibrate the adc unit
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .atten = APP_ADC_ATTEN,
            .bitwidth = APP_ADC_BITWIDTH,
        };
        ESP_ERROR_CHECK(adc_cali_create_scheme_line_fitting(&cali_config, &adc1_cali_handle));
    #endif

    // setup the adc on the needed channel (pin)
    adc_oneshot_chan_cfg_t config = {
        .atten = APP_ADC_ATTEN,
        .bitwidth = APP_ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, APP_RSSI_ADC_CHANNEL, &config));
    
}


// click handler
void app_click_handler(button_direction_t btn_dir, int* freq)
{
    switch (btn_dir)
    {
        case BTN_LEFT:
            // gui_print_string(&app_state.u8g2, "left");
            *freq -= 10;
            // request_receiver_rssi(*freq); 
            ESP_LOGI(TAG, "%i", *freq);
        break;
        case BTN_UP:
            request_receiver_sweep(5650, 5, 64);
            ESP_LOGI(TAG, "up");
        break;
        case BTN_RIGHT:
            *freq += 10;
            // request_receiver_rssi(*freq); 
            ESP_LOGI(TAG, "%i", *freq);
        break;
        case BTN_DOWN:
            ESP_LOGI(TAG, "down");
        break;
        case BTN_MIDDLE:
            ESP_LOGI(TAG, "middle");
        break;
    }
}


// all the operations for startup
// initialises and creates the needed tasks
esp_err_t app_load()
{
    app_state.current_screen = SCREEN_NONE;
    
    // setup the adc
    setup_adc();
    
    // setup the common spi bus
    setup_spi_bus(APP_SCLK_PIN, APP_MOSI_PIN, APP_MISO_PIN);

    // setup the display
    spi_display_pins_t display_pins = {
        .clk = APP_SCLK_PIN, 
        .mosi = APP_MOSI_PIN, 
        .miso = APP_MISO_PIN,
        .cs = APP_DISPLAY_CS_PIN,
        .dc = APP_DISPLAY_DC_PIN,
        .rst = APP_DISPLAY_RST_PIN
    };
    ESP_ERROR_CHECK(setup_display(&app_state.u8g2, APP_DISPLAY_DRIVER, APP_DISPLAY_DEFAULT_ROTATION, display_pins));

    init_display(&app_state.u8g2);

    // display the splash screen
    splash_config.u8g2 = &app_state.u8g2;
    splash_config.delay_ms = 2000;

    xTaskCreate(gui_draw_splashes_task, "gui_splashes_task", 2048, &splash_config, 10, NULL);

    // setup the receiver module
    spi_receiver_pins_t rx_pins = {
        .clk = APP_SCLK_PIN, 
        .mosi = APP_MOSI_PIN, 
        .cs = APP_RX_CS_PIN
    };
    setup_receiver(rx_pins, adc1_handle, APP_RSSI_ADC_CHANNEL, adc1_cali_handle);

    // setup the lua environment
    app_state.L = luaL_newstate();


    // setup the button event interrupts
    input_pins_t input_pins = {
        .left = APP_LEFT_BTN_PIN,
        .up = APP_UP_BTN_PIN,
        .right = APP_RIGHT_BTN_PIN,
        .down = APP_DOWN_BTN_PIN,
        .middle = APP_MIDDLE_BTN_PIN,
    };
    setup_button_input_pins(input_pins);

    return ESP_OK;
}


// main app task
void app_main(void)
{
    status_led = create_ws2812(GPIO_NUM_10);
    set_color_ws2812(&status_led, WS2812_DIM_BLUE);
    ESP_ERROR_CHECK(app_load());
    set_color_ws2812(&status_led, WS2812_DIM_GREEN);

    

    // variables for the program
    button_direction_t curr_click_dir;
    rssi_reading_t rssi_reading;
    int readings[64];

    int curr_freq = 5800;

    for(;;)
    {
        // check for the status of input button queue
        if (receive_input_event_queue(&curr_click_dir, 16)) {
            // process a new input event
            app_click_handler(curr_click_dir, &curr_freq);
            // ESP_LOGI(TAG, "inpt");
        }
        // check if any readings are available from the receiver
        if (receive_rssi_queue(&rssi_reading, 16)) {
            // write the reading into the correct place in readings
            int index = (rssi_reading.freq - 5650)/5;
            readings[index] = rssi_reading.rssi;

            // gui_draw_bars(&app_state.u8g2, 0, 0, 2, 64, 100, readings, 64);
            gui_update_bar(&app_state.u8g2, 0, 0, 2, 64, 20, readings[index], index);

            // ESP_LOGI(TAG, "recv");
            // char string[64] = "";
            // strcat(string, itoa(rssi_reading.freq, str_buf, 10));
            // strcat(string, ": ");
            // strcat(string, itoa(rssi_reading.rssi, str_buf, 10));

            // gui_print_string(&app_state.u8g2, string);
            // ESP_LOGI(TAG, "reading: %i %i", rssi_reading.freq, rssi_reading.rssi);
        }
    }
}