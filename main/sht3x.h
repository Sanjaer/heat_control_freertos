/*  SHT Header

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


#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_err.h"

#include "driver/i2c.h"


#define I2C_EXAMPLE_MASTER_SCL_IO           CONFIG_SCL_PIN   /*!< gpio number for I2C master clock */
#define I2C_EXAMPLE_MASTER_SDA_IO           CONFIG_SDA_PIN   /*!< gpio number for I2C master data  */
#define I2C_EXAMPLE_MASTER_NUM              I2C_NUM_0        /*!< I2C port number for master dev */
#define I2C_EXAMPLE_MASTER_TX_BUF_DISABLE   0                /*!< I2C master do not need buffer */
#define I2C_EXAMPLE_MASTER_RX_BUF_DISABLE   0                /*!< I2C master do not need buffer */

#define SHT30_SENSOR_ADDR                   0x45             /*!< slave address for SHT30 sensor */
#define SHT30_CMD_START_MSB                 0x2C             /*!< Command to set measure one shot */
#define SHT30_CMD_START_LSB                 0x06             /*!< Clock stretchin enabled, Repeatibility High */
#define DATA_MSG_SIZE                       6                /*!< Size of the message containig sesor data 6B */
#define WRITE_BIT                           I2C_MASTER_WRITE /*!< I2C master write */
#define READ_BIT                            I2C_MASTER_READ  /*!< I2C master read */
#define ACK_CHECK_EN                        0x1              /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS                       0x0              /*!< I2C master will not check ack from slave */
#define ACK_VAL                             0x0              /*!< I2C ack value */
#define NACK_VAL                            0x1              /*!< I2C nack value */
#define LAST_NACK_VAL                       0x2              /*!< I2C last_nack value */


struct SensorData{
    float temperature;
    float humidity;
};

float convert_raw_to_celsius(uint8_t *data);
float convert_raw_to_humidity(uint8_t *data);
bool check_raw_temp_checksum(uint8_t *data);
bool check_raw_hum_checksum(uint8_t *data);
esp_err_t i2c_master_init(void);
esp_err_t i2c_master_sht30_read(i2c_port_t i2c_num, uint8_t command_msb, uint8_t command_lsb, uint8_t *data, size_t data_len);