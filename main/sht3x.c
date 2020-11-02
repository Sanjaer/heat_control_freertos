/*  SHT Source

    MIT Licensed

    Copyright 2020 Pablo San José Burgos

    Permission is hereby granted, free of charge, to any person obtaining a copy 
    of this software and associated documentation files (the "Software"), 
    to deal in the Software without restriction, including without limitation the rights to 
    use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies 
    of the Software, and to permit persons to whom the Software is furnished to do so, 
    subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all copies 
    or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, 
    INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS 
    OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "sht3x.h"

/**
 * @brief temperature calculator from raw sensor data
 */
float convert_raw_to_celsius(uint8_t *data){

    return ((((data[0] * 256.0) + data[1]) * 175) / 65535.0) - 45;

}

/**
 * @brief humidity calculator from raw sensor data
 */
float convert_raw_to_humidity(uint8_t *data){

    return ((((data[3] * 256.0) + data[4]) * 100) / 65535.0);

}

/**
 * @brief temperature checksum calculator from raw sensor data
 */
bool check_raw_temp_checksum(uint8_t *data){

    // TODO
    return true;

}

/**
 * @brief humidity checksum calculator from raw sensor data
 */
bool check_raw_hum_checksum(uint8_t *data){

    // TODO
    return true;

}

/**
 * @brief i2c master initialization
 */
esp_err_t i2c_master_init(void){

    int i2c_master_port = I2C_EXAMPLE_MASTER_NUM;
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_EXAMPLE_MASTER_SDA_IO;
    conf.sda_pullup_en = 1;
    conf.scl_io_num = I2C_EXAMPLE_MASTER_SCL_IO;
    conf.scl_pullup_en = 1;
    conf.clk_stretch_tick = 300; // 300 ticks, Clock stretch is about 210us
    ESP_ERROR_CHECK(i2c_driver_install(i2c_master_port, conf.mode));
    ESP_ERROR_CHECK(i2c_param_config(i2c_master_port, &conf));

    return ESP_OK;

}

/**
 * @brief test code to read sht30
 *
 * 1. send read request
 * ____________________________________________________________________________________
 * | start | slave_addr + wr_bit + ack | command MSB + ack | command LSB + ack | stop |
 * --------|---------------------------|-------------------|-------------------|------|
 *
 * 2. read data
 * ___________________________________________________________________________________
 * | start | slave_addr + wr_bit + ack | read data_len byte + ack(last nack)  | stop |
 * --------|---------------------------|--------------------------------------|------|
 *
 * @param i2c_num I2C port number
 * @param command_msb command most significant byte
 * @param command_lsb command least significant byte
 * @param data data to read
 * @param data_len data length
 *
 * @return
 *     - ESP_OK Success
 *     - ESP_ERR_INVALID_ARG Parameter error
 *     - ESP_FAIL Sending command error, slave doesn't ACK the transfer.
 *     - ESP_ERR_INVALID_STATE I2C driver not installed or not in master mode.
 *     - ESP_ERR_TIMEOUT Operation timeout because the bus is busy.
 * 
 */
esp_err_t i2c_master_sht30_read(i2c_port_t i2c_num, uint8_t command_msb, uint8_t command_lsb, uint8_t *data, size_t data_len)
{
    int ret;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, SHT30_SENSOR_ADDR << 1 | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, command_msb, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, command_lsb, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);


    if (ret != ESP_OK) {
        ESP_LOGI("ERROR", "ret = %d\n", ret);
    }

    vTaskDelay(500 / portTICK_RATE_MS);

    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, SHT30_SENSOR_ADDR << 1 | READ_BIT, ACK_CHECK_EN);
    i2c_master_read(cmd, data, data_len, LAST_NACK_VAL);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);

    return ret;
}