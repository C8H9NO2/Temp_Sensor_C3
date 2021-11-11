//
// Created by cch98 on 2021/11/4.
//

#include "include/aht21.h"
#include "esp_log.h"

static i2c_port_t I2C_PORT;

static const char *TAG = "I2C";

static uint8_t *data = new uint8_t[6];
static uint8_t *crc_byte = new uint8_t;

void init_i2c_driver(i2c_port_t i2c_port, int sda_gpio, int scl_gpio, uint32_t i2c_freq) {
    ESP_LOGI(TAG, "Init driver");
    i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
    i2c_config_t i2c_config = {
            .mode=I2C_MODE_MASTER,
            .sda_io_num=sda_gpio,
            .scl_io_num=scl_gpio,
            .sda_pullup_en=GPIO_PULLUP_ENABLE,
            .scl_pullup_en=GPIO_PULLUP_ENABLE,
    };
    i2c_config.master.clk_speed = i2c_freq;
    i2c_param_config(i2c_port, &i2c_config);
    I2C_PORT = i2c_port;
}

void init_aht21_dev() {
    ESP_LOGI(TAG, "Init Dev");
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, AHT21_ADDR << 1 | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, 0xBE, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, 0x08, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, 0x30, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_PORT, cmd, 100 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
}

void test_init_aht21() {
    init_i2c_driver(I2C_NUM_0, GPIO_NUM_1, GPIO_NUM_0, 1000 * 10);
    vTaskDelay(500 / portTICK_RATE_MS);
    init_aht21_dev();
}

void aht21_measure_cmd() {
    ESP_LOGI(TAG, "Measure");
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, AHT21_ADDR << 1 | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, 0xAC, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, 0x33, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, 0x00, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_PORT, cmd, 100 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);

}


void aht21_read_data(float *temperature, float *humidity) {
    ESP_LOGI(TAG, "Read");
    aht21_measure_cmd();
    vTaskDelay(500 / portTICK_RATE_MS);


    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, AHT21_ADDR << 1 | I2C_MASTER_READ, ACK_CHECK_EN);
    i2c_master_read(cmd, data, 6, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, crc_byte, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    i2c_master_cmd_begin(I2C_NUM_0, cmd, 100 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);

    uint32_t *temp_raw = new uint32_t();
    uint32_t *hum_raw = new uint32_t();
    *hum_raw = (*hum_raw | data[1]) << 8;
    *hum_raw = (*hum_raw | data[2]) << 8;
    *hum_raw = (*hum_raw | data[3]) >> 4;
    *temp_raw = (*temp_raw | data[3]) << 8;
    *temp_raw = (*temp_raw | data[4]) << 8;
    *temp_raw = (*temp_raw | data[5]);
    *temp_raw = *temp_raw & 0xfffff;

    *humidity = (float) (*hum_raw * 100 * 10 / 1024 / 1024); // NOLINT(bugprone-integer-division)
    *temperature = (float) (*temp_raw * 200 * 10 / 1024 / 1024 - 500);// NOLINT(bugprone-integer-division)
}