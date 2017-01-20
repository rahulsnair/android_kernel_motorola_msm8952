/*
 * Copyright (C) 2010-2014 Motorola Mobility LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/input-polldev.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/switch.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>

#include <linux/stml0xx.h>

long stml0xx_misc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	static int lowpower_mode = 1;
	int err = 0;
	struct stml0xx_data *ps_stml0xx = stml0xx_misc_data;
	unsigned int addr, duration, algo_idx;
	unsigned short delay;
	unsigned int data_size = 0;
	unsigned char buf[SPI_RX_PAYLOAD_LEN];
	unsigned char len;
	unsigned int handle;
	struct timespec current_time;
	bool cmd_handled;
	unsigned int read_write;
	unsigned int loop_count;
	unsigned char gyro_buf[STML0XX_GYRO_CAL_SIZE];

	if (!stml0xx_misc_data)
		stml0xx_misc_data = file->private_data;

	/* Commands to respond immediately without waiting for the mutex lock */
	cmd_handled = true;
	switch (cmd) {
	case STML0XX_IOCTL_GET_BOOTED:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_GET_BOOTED");
		buf[0] = stml0xx_g_booted;
		if (copy_to_user(argp, buf, 1))
			err = -EFAULT;
		else
			err = 0;
		break;
	case STML0XX_IOCTL_GET_VERNAME:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_GET_VERNAME");
		if (copy_to_user(argp, &(ps_stml0xx->pdata->fw_version),
				 FW_VERSION_SIZE))
			err = -EFAULT;
		else
			err = 0;
		break;
	case STML0XX_IOCTL_GET_VERSION:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_GET_VERSION");
		if (!stml0xx_g_booted)
			err = -EBUSY;
		else
			cmd_handled = false;
		break;
	default:
		/* Continue to the next switch statement */
		cmd_handled = false;
		break;
	}

	if (cmd_handled)
		goto EXIT;

	/* Wait for mutex lock */
	wake_lock(&ps_stml0xx->wakelock);
	mutex_lock(&ps_stml0xx->lock);
	stml0xx_wake(ps_stml0xx);

	/* Commands accepted in any mode */
	switch (cmd) {
	case STML0XX_IOCTL_BOOTLOADERMODE:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_BOOTLOADERMODE");
		err = switch_stml0xx_mode(BOOTMODE);
		break;
	case STML0XX_IOCTL_NORMALMODE:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_NORMALMODE");
		err = switch_stml0xx_mode(NORMALMODE);
		break;
	case STML0XX_IOCTL_MASSERASE:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_MASSERASE");
		err = stml0xx_boot_flash_erase();
		break;
	case STML0XX_IOCTL_SETSTARTADDR:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_SETSTARTADDR");
		if (copy_from_user(&addr, argp, sizeof(addr))) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Copy start address returned error");
			err = -EFAULT;
			break;
		}
		stml0xx_misc_data->current_addr = addr;
		err = 0;
		break;
	case STML0XX_IOCTL_SET_FACTORY_MODE:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_SET_FACTORY_MODE");
		err = switch_stml0xx_mode(FACTORYMODE);
		break;
	case STML0XX_IOCTL_TEST_BOOTMODE:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_TEST_BOOTMODE");
		err = switch_stml0xx_mode(BOOTMODE);
		break;
	case STML0XX_IOCTL_SET_DEBUG:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_SET_DEBUG");
		err = 0;
		break;
	case STML0XX_IOCTL_GET_VERSION:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_GET_VERSION");
		if (stml0xx_g_booted)
			err = stml0xx_get_version(ps_stml0xx);
		else
			err = -EBUSY;
		break;
	case STML0XX_IOCTL_SET_ACC_DELAY:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_SET_ACC_DELAY");
		if (copy_from_user(&delay, argp, sizeof(delay))) {
			dev_dbg(&stml0xx_misc_data->spi->dev,
				"Copy acc delay returned error");
			err = -EFAULT;
			break;
		}
		stml0xx_g_acc_delay = delay;
		if (stml0xx_g_booted) {
			buf[0] = delay;
			err = stml0xx_spi_send_write_reg(ACCEL_UPDATE_RATE,
							buf, 1);
		}
		break;
	case STML0XX_IOCTL_SET_ACC2_DELAY:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_SET_ACC2_DELAY");
		if (copy_from_user(&delay, argp, sizeof(delay))) {
			dev_dbg(&stml0xx_misc_data->spi->dev,
				"Copy acc2 delay returned error");
			err = -EFAULT;
			break;
		}
		stml0xx_g_acc2_delay = delay;
		if (stml0xx_g_booted) {
			buf[0] = delay;
			err = stml0xx_spi_send_write_reg(ACCEL2_UPDATE_RATE,
							buf, 1);
		}
		break;
	case STML0XX_IOCTL_SET_GYRO_DELAY:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_SET_GYRO_DELAY");
		delay = 0;
		if (copy_from_user(&delay, argp, sizeof(delay))) {
			dev_dbg(&stml0xx_misc_data->spi->dev,
				"Copy gyro delay returned error");
			err = -EFAULT;
			break;
		}
		stml0xx_g_gyro_delay = delay;
		if (stml0xx_g_booted) {
			buf[0] = delay;
			err = stml0xx_spi_send_write_reg(GYRO_UPDATE_RATE,
							buf, 1);
		}
		break;
	case STML0XX_IOCTL_SET_SENSORS:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_SET_SENSORS");
		if (copy_from_user(buf, argp, 3 * sizeof(unsigned char))) {
			dev_dbg(&stml0xx_misc_data->spi->dev,
				"Copy set sensors returned error");
			err = -EFAULT;
			break;
		}
		stml0xx_g_nonwake_sensor_state = (buf[2] << 16)
			| (buf[1] << 8) | buf[0];
		if (stml0xx_g_booted)
			err = stml0xx_spi_send_write_reg(NONWAKESENSOR_CONFIG,
							 buf, 3);
		dev_dbg(&stml0xx_misc_data->spi->dev, "Sensor enable = 0x%lx",
			stml0xx_g_nonwake_sensor_state);
		break;
	case STML0XX_IOCTL_GET_SENSORS:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_GET_SENSORS");
		if (stml0xx_g_booted) {
			err = stml0xx_spi_send_read_reg(NONWAKESENSOR_CONFIG,
							buf, 3);
			if (err < 0) {
				dev_err(&stml0xx_misc_data->spi->dev,
					"Reading get sensors failed");
				break;
			}
		} else {
			buf[0] = stml0xx_g_nonwake_sensor_state & 0xFF;
			buf[1] = (stml0xx_g_nonwake_sensor_state >> 8) & 0xFF;
			buf[2] = (stml0xx_g_nonwake_sensor_state >> 16) & 0xFF;
		}
		if (copy_to_user(argp, buf, 3 * sizeof(unsigned char)))
			err = -EFAULT;
		break;
	case STML0XX_IOCTL_SET_WAKESENSORS:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_SET_WAKESENSORS");
		if (copy_from_user(buf, argp, 3 * sizeof(unsigned char))) {
			dev_dbg(&stml0xx_misc_data->spi->dev,
				"Copy set sensors returned error");
			err = -EFAULT;
			break;
		}
		stml0xx_g_wake_sensor_state = (buf[2] << 16)
			|(buf[1] << 8) | buf[0];
		if (stml0xx_g_booted)
			err = stml0xx_spi_send_write_reg(WAKESENSOR_CONFIG,
							 buf, 3);
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"Sensor enable = 0x%06lX", stml0xx_g_wake_sensor_state);
		break;
	case STML0XX_IOCTL_GET_WAKESENSORS:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_GET_WAKESENSORS");
		if (stml0xx_g_booted) {
			err = stml0xx_spi_send_read_reg(WAKESENSOR_CONFIG,
							 buf, 3);
			if (err < 0) {
				dev_err(&stml0xx_misc_data->spi->dev,
					"Reading get sensors failed");
				break;
			}
		} else {
			buf[0] = stml0xx_g_wake_sensor_state & 0xFF;
			buf[1] = (stml0xx_g_wake_sensor_state >> 8) & 0xFF;
			buf[2] = (stml0xx_g_wake_sensor_state >> 16) & 0xFF;
		}
		if (copy_to_user(argp, buf, 3 * sizeof(unsigned char)))
			err = -EFAULT;
		break;
	case STML0XX_IOCTL_SET_ALGOS:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_SET_ALGOS");
		if (copy_from_user(buf, argp, 2 * sizeof(unsigned char))) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Copy set algos returned error");
			err = -EFAULT;
			break;
		}
		stml0xx_g_algo_state = (buf[1] << 8) | buf[0];
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"Set algos config: 0x%x", stml0xx_g_algo_state);
		if (stml0xx_g_booted)
			err = stml0xx_spi_send_write_reg(ALGO_CONFIG,
							buf, 2);
		break;
	case STML0XX_IOCTL_GET_ALGOS:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_GET_ALGOS");
		if (stml0xx_g_booted) {
			err = stml0xx_spi_send_read_reg(ALGO_CONFIG,
							buf, 2);
			if (err < 0) {
				dev_err(&stml0xx_misc_data->spi->dev,
					"Reading get algos failed");
				break;
			}
		} else {
			buf[0] = stml0xx_g_algo_state & 0xFF;
			buf[1] = (stml0xx_g_algo_state >> 8) & 0xFF;
		}
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"Get algos config: 0x%x", (buf[1] << 8) | buf[0]);
		if (copy_to_user(argp, buf, 2 * sizeof(unsigned char)))
			err = -EFAULT;
		break;
	case STML0XX_IOCTL_SET_ALS_DELAY:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_SET_ALS_DELAY");
		if (copy_from_user(&delay, argp, sizeof(delay))) {
			dev_dbg(&stml0xx_misc_data->spi->dev,
				"Copy als delay returned error");
			err = -EFAULT;
			break;
		}
		stml0xx_g_als_delay = delay;
		if (stml0xx_g_booted) {
			buf[0] = delay >> 8;
			buf[1] = delay & 0xFF;
			err = stml0xx_spi_send_write_reg(ALS_UPDATE_RATE,
							buf, 2);
		}
		break;
	case STML0XX_IOCTL_SET_MOTION_DUR:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_SET_MOTION_DUR");
		if (copy_from_user(&duration, argp, sizeof(duration))) {
			dev_dbg(&stml0xx_misc_data->spi->dev,
				"Copy set motion dur returned error");
			err = -EFAULT;
			break;
		}
		buf[0] = duration & 0xFF;
		stml0xx_g_motion_dur = buf[0];
		if (stml0xx_g_booted)
			err = stml0xx_spi_send_write_reg(MOTION_DUR,
							buf, 1);
		break;
	case STML0XX_IOCTL_SET_ZRMOTION_DUR:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_SET_ZRMOTION_DUR");
		if (copy_from_user(&duration, argp, sizeof(duration))) {
			dev_dbg(&stml0xx_misc_data->spi->dev,
				"Copy zmotion dur returned error");
			err = -EFAULT;
			break;
		}
		buf[0] = duration & 0xFF;
		stml0xx_g_zmotion_dur = buf[0];
		if (stml0xx_g_booted)
			err = stml0xx_spi_send_write_reg(ZMOTION_DUR,
							 buf, 1);
		break;
	case STML0XX_IOCTL_GET_DOCK_STATUS:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_GET_DOCK_STATUS");
		if (stml0xx_g_booted)
			err = stml0xx_spi_send_read_reg(DOCKED_DATA,
							buf, 1);
		else
			buf[0] = 0;
		if (copy_to_user(argp, buf, sizeof(unsigned char)))
			err = -EFAULT;
		break;
	case STML0XX_IOCTL_TEST_READ:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_TEST_READ");
		if (stml0xx_g_booted) {
			err = stml0xx_spi_read(buf, 1);
			/* stml0xx_spi_read will return num of bytes
				read or error */
			if (err > 0)
				err = buf[0];
		}
		break;
	case STML0XX_IOCTL_TEST_WRITE:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_TEST_WRITE");
		if (!stml0xx_g_booted) {
			err = -EBUSY;
			break;
		}
		if (copy_from_user(buf, argp, sizeof(unsigned char))) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Copy test write returned error");
			err = -EFAULT;
			break;
		}
		err = stml0xx_spi_write(buf, 1);
		break;
	case STML0XX_IOCTL_SET_ALGO_REQ:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_SET_ALGO_REQ");
		/* copy algo index */
		if (copy_from_user(buf, argp, 2 * sizeof(unsigned char))) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Set algo req copy bytes returned error");
			err = -EFAULT;
			break;
		}
		algo_idx = (buf[1] << 8) | buf[0];
		/* copy length */
		if (copy_from_user(&len, argp + 2 * sizeof(unsigned char),
				   sizeof(unsigned char))) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Set algo req copy byte returned error");
			err = -EFAULT;
			break;
		}
		/* algo req register */
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"Set algo req, algo idx: %d, len: %u", algo_idx, len);
		if (algo_idx < STML0XX_NUM_ALGOS) {
			buf[0] = stml0xx_algo_info[algo_idx].req_register;
			dev_dbg(&stml0xx_misc_data->spi->dev, "Register: 0x%x",
				stml0xx_algo_info[algo_idx].req_register);
		} else {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Set algo req invalid arg");
			err = -EFAULT;
			break;
		}
		if (len > ALGO_RQST_DATA_SIZE) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Set algo req invalid size arg");
			err = -EFAULT;
			break;
		}
		if (copy_from_user(buf, argp + 2 * sizeof(unsigned char)
				   + sizeof(len), len)) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Set algo req copy req info returned error");
			err = -EFAULT;
			break;
		}
		stml0xx_g_algo_requst[algo_idx].size = len;
		memcpy(stml0xx_g_algo_requst[algo_idx].data, buf, len);
		if (stml0xx_g_booted)
			err = stml0xx_spi_send_write_reg(stml0xx_algo_info
					       [algo_idx].req_register, buf,
					       len);
		break;
	case STML0XX_IOCTL_GET_ALGO_EVT:
		dev_dbg(&stml0xx_misc_data->spi->dev, "STML0XX_IOCTL_GET_ALGO_EVT");
		if (!stml0xx_g_booted) {
			err = -EFAULT;
			break;
		}
		/* copy algo index */
		if (copy_from_user(buf, argp, 2 * sizeof(unsigned char))) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Get algo evt copy bytes returned error");
			err = -EFAULT;
			break;
		}
		algo_idx = (buf[1] << 8) | buf[0];
		/* algo evt register */
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"Get algo evt, algo idx: %d", algo_idx);
		if (algo_idx < STML0XX_NUM_ALGOS) {
			dev_dbg(&stml0xx_misc_data->spi->dev, "Register: 0x%x",
				stml0xx_algo_info[algo_idx].evt_register);
		} else {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Get algo evt invalid arg");
			err = -EFAULT;
			break;
		}
		err =
		    stml0xx_spi_send_read_reg(stml0xx_algo_info
					      [algo_idx].evt_register, buf,
					      stml0xx_algo_info
					      [algo_idx].evt_size);
		if (err < 0) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Get algo evt failed");
			break;
		}
		if (copy_to_user(argp + 2 * sizeof(unsigned char),
				 buf, stml0xx_algo_info[algo_idx].evt_size))
			err = -EFAULT;
		break;
	case STML0XX_IOCTL_WRITE_REG:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_WRITE_REG");
		if (!stml0xx_g_booted) {
			err = -EFAULT;
			break;
		}
		/* copy addr and size */
		if (copy_from_user(buf, argp, 4)) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Write Reg, copy bytes returned error");
			err = -EFAULT;
			break;
		}
		addr = (buf[0] << 8) | buf[1];
		data_size = (buf[2] << 8) | buf[3];

		/* fail if the write size is too large */
		if (data_size > SPI_TX_PAYLOAD_LEN) {
			err = -EFAULT;
			dev_err(&stml0xx_misc_data->spi->dev,
				"Write Reg, data_size %d > %d",
				data_size,
				SPI_TX_PAYLOAD_LEN);
			break;
		}

		/* copy in the data */
		if (copy_from_user(buf, argp + 4, data_size)) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Write Reg copy from user returned error");
			err = -EFAULT;
			break;
		}

		err = stml0xx_spi_send_write_reg(addr, buf, data_size);
		if (err < 0) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Write Reg unable to write to direct reg %d",
				err);
		}
		break;
	case STML0XX_IOCTL_READ_REG:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_READ_REG");
		if (!stml0xx_g_booted) {
			err = -EFAULT;
			break;
		}
		/* copy addr and size */
		if (copy_from_user(buf, argp, 4)) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Read Reg, copy bytes returned error");
			err = -EFAULT;
			break;
		}
		addr = (buf[0] << 8) | buf[1];
		data_size = (buf[2] << 8) | buf[3];

		if (data_size > SPI_RX_PAYLOAD_LEN) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Read Reg, data_size %d > %d",
				data_size,
				SPI_RX_PAYLOAD_LEN);
			err = -EFAULT;
			break;
		}

		/* setup the address */
		err = stml0xx_spi_send_read_reg(addr, buf, data_size);
		if (err < 0) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Read Reg, unable to read from direct reg %d",
				err);
			break;
		}

		if (copy_to_user(argp, buf, data_size)) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Read Reg error copying to user");
			err = -EFAULT;
			break;
		}
		break;
	case STML0XX_IOCTL_PASSTHROUGH:
		dev_dbg(&stml0xx_misc_data->spi->dev,
				"STML0XX_IOCTL_PASSTHROUGH");
		if (!stml0xx_g_booted) {
			err = -EFAULT;
			break;
		}

		memset(buf, 0, STML0XX_PASSTHROUGH_SIZE);
		/* copy bus, addr, reg, R/W and size */
		if (copy_from_user(buf, argp, 6)) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Passthrough, copy info error");
			err = -EFAULT;
			break;
		}
		read_write = buf[4];
		data_size = buf[5];

		if (read_write == 0) { /* Read operation */
			if (data_size > (STML0XX_PASSTHROUGH_SIZE - 1)) {
				dev_err(&stml0xx_misc_data->spi->dev,
					"Passthrough, data size too large %d",
					data_size);
				err = -EFAULT;
				break;
			}
		} else if (read_write == 1) { /* Write operation */
			if (data_size > (STML0XX_PASSTHROUGH_SIZE - 6)) {
				dev_err(&stml0xx_misc_data->spi->dev,
					"Passthrough, data size too large %d",
					data_size);
				err = -EFAULT;
				break;
			}

			/* copy in the data to write */
			if (copy_from_user(&buf[6], argp + 6, data_size)) {
				dev_err(&stml0xx_misc_data->spi->dev,
					"Passthrough, copy data error");
				err = -EFAULT;
				break;
			}
		} else {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Passthrough, invalid r/w %d", read_write);
			err = -EFAULT;
			break;
		}

		/* Write the command to the sensor hub */
		err = stml0xx_spi_send_write_reg(I2C_PASSTHROUGH_COMMAND, buf,
			STML0XX_PASSTHROUGH_SIZE);
		if (err < 0) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Passthrough, write error %d",
				err);
			err = -EFAULT;
			break;
		}

		/* Read the response from  the sensor hub */
		loop_count = 0;
		buf[0] = 0;
		do {
			err = stml0xx_spi_send_read_reg(
				I2C_PASSTHROUGH_RESPONSE,
				buf, STML0XX_PASSTHROUGH_SIZE);
			if (err < 0) {
				dev_err(&stml0xx_misc_data->spi->dev,
					"Passthrough, read error %d",
					err);
				err = -EFAULT;
				break;
			}
			loop_count++;
			if (loop_count == 10)
				break;
		} while (buf[0] == 0);
		if (buf[0] != 1) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Passthrough, read failed %d", buf[0]);
			err = -EFAULT;
			break;
		}
		err = 0;

		if (read_write == 0) {
			/* Return the hub register data read */
			if (copy_to_user(argp, &buf[1], data_size)) {
				dev_err(&stml0xx_misc_data->spi->dev,
					"Passthrough, copy back error");
				err = -EFAULT;
				break;
			}
		}

		break;
	case STML0XX_IOCTL_SET_LOWPOWER_MODE:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"STML0XX_IOCTL_SET_LOWPOWER_MODE");
		if (!stml0xx_g_booted) {
			err = -EBUSY;
			break;
		}
		if (copy_from_user(buf, argp, 1)) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Copy size from user returned error");
			err = -EFAULT;
			break;
		}

		err = 0;
		if (buf[0] != 0 && lowpower_mode == 0) {
			stml0xx_set_lowpower_mode(LOWPOWER_ENABLED,
					RESET_ALLOWED);

			/* allow sensorhub to sleep */
			stml0xx_sleep(ps_stml0xx);
			lowpower_mode = buf[0];
		} else if (buf[0] == 0 && lowpower_mode == 1) {
			stml0xx_set_lowpower_mode(LOWPOWER_DISABLED,
					RESET_ALLOWED);

			/* keep sensorhub awake */
			stml0xx_wake(ps_stml0xx);
			lowpower_mode = buf[0];
		}
		break;
	case STML0XX_IOCTL_SET_FLUSH:
		dev_dbg(&stml0xx_misc_data->spi->dev,
				"STML0XX_IOCTL_SET_FLUSH");
		if (!stml0xx_g_booted)
			break;
		if (copy_from_user(&handle, argp, sizeof(unsigned int))) {
			dev_err(&stml0xx_misc_data->spi->dev,
				"Copy flush handle returned error\n");
			err = -EFAULT;
			break;
		}
		handle = cpu_to_be32(handle);
		get_monotonic_boottime(&current_time);
		stml0xx_as_data_buffer_write(ps_stml0xx, DT_FLUSH,
				(char *)&handle, 4, 0,
				ts_to_ns(current_time));
		break;
	case STML0XX_IOCTL_GET_GYRO_CAL:
		dev_dbg(&stml0xx_misc_data->spi->dev,
				"STML0XX_IOCTL_GET_GYRO_CAL");
		if (stml0xx_g_booted) {
			err = stml0xx_spi_send_read_reg(GYRO_CAL,
					gyro_buf, STML0XX_GYRO_CAL_FIRST);
			if (err < 0) {
				dev_err(&stml0xx_misc_data->spi->dev,
					"Reading get gyro cal failed\n");
				break;
			}
			err = stml0xx_spi_send_read_reg(GYRO_CAL_2,
					gyro_buf + STML0XX_GYRO_CAL_FIRST,
					STML0XX_GYRO_CAL_SECOND);
			if (err < 0) {
				dev_err(&stml0xx_misc_data->spi->dev,
					"Reading get gyro cal failed\n");
				break;
			}
			memcpy(stml0xx_g_gyro_cal, gyro_buf,
					STML0XX_GYRO_CAL_SIZE);
		}
		if (copy_to_user(argp, stml0xx_g_gyro_cal,
					STML0XX_GYRO_CAL_SIZE))
			err = -EFAULT;
		break;
	case STML0XX_IOCTL_SET_GYRO_CAL:
		dev_dbg(&stml0xx_misc_data->spi->dev,
				"STML0XX_IOCTL_SET_GYRO_CAL");
		if (copy_from_user(gyro_buf, argp, STML0XX_GYRO_CAL_SIZE)) {
			dev_err(&stml0xx_misc_data->spi->dev,
					"Copy set gyro cal returned error\n");
			err = -EFAULT;
			break;
		}
		memcpy(stml0xx_g_gyro_cal, gyro_buf,
				STML0XX_GYRO_CAL_SIZE);
		if (stml0xx_g_booted) {
			err = stml0xx_spi_send_write_reg(GYRO_CAL,
					gyro_buf, STML0XX_GYRO_CAL_FIRST);
			if (err < 0) {
				dev_err(&stml0xx_misc_data->spi->dev,
					"Writing set gyro cal failed\n");
				break;
			}
			err = stml0xx_spi_send_write_reg(GYRO_CAL_2,
					gyro_buf + STML0XX_GYRO_CAL_FIRST,
					STML0XX_GYRO_CAL_SECOND);
			if (err < 0) {
				dev_err(&stml0xx_misc_data->spi->dev,
					"Writing set gyro cal failed\n");
			}
		}
		break;
	default:
		dev_dbg(&stml0xx_misc_data->spi->dev,
			"Invalid IOCTL [%d]", cmd);
		break;
	}

	stml0xx_sleep(ps_stml0xx);
	mutex_unlock(&ps_stml0xx->lock);
	wake_unlock(&ps_stml0xx->wakelock);
EXIT:
	return err;
}
