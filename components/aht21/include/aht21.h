//
// Created by cch98 on 2021/11/4.
//

#ifndef TEMP_SENSOR_C3_AHT21_H
#define TEMP_SENSOR_C3_AHT21_H

#include "driver/i2c.h"

#define AHT21_ADDR                  0x38
#define I2C_MASTER_TX_BUF_DISABLE   0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0 /*!< I2C master doesn't need buffer */
#define ACK_CHECK_EN                0x01            /*!< I2C master will check ack from slave*/
#define ACK_VAL                     0x0                 /*!< I2C ack value */
#define NACK_VAL                    0x1                /*!< I2C nack value */


void test_init_aht21();

void aht21_read_data(float *temperature, float *humidity);


#endif //TEMP_SENSOR_C3_AHT21_H
