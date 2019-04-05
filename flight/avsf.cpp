#include "avsf.h"
#include "I2C.h"
#include "cl.h"

byte readBuffer(bool type, byte reg, byte len, uint8_t *buffer){
	byte address;
	if (type == GYROTYPE) {
		address = LSM9DS0_ADDRESS_G;
	} else {
		address = LSM9DS0_ADDRESS_XM;
	}
	I2c.read(address, reg, (byte)len);
	for (uint8_t i=0; i<len; i++) {
		buffer[i] = I2c.receive();
	}
	return len;
}

void write16(uint8_t reg, uint16_t val){
	I2c.write(INA219_ADDRESS, reg, (uint8_t)((val>>8) & 0xFF));
	I2c.write(INA219_ADDRESS, reg, (uint8_t)(   val   & 0xFF));
}

uint16_t read16(uint8_t reg){
	I2c.read(INA219_ADDRESS, reg, 2);
	uint16_t val;
	delay(1);
	val = ((I2c.receive()<<8) | I2c.receive());
	return val;
}

void write8(bool type, byte reg, byte value){
	byte address;
	if (type == GYROTYPE) {
		address = LSM9DS0_ADDRESS_G;
	} else {
		address = LSM9DS0_ADDRESS_XM;
	}
	I2c.write(address, reg, value);
}

byte read8(bool type, byte reg){
	uint8_t value;
	readBuffer(type, reg, 1, &value);
	return value;
}

int16_t flow_read(){
	I2c.read(FS2012_ADDRESS, 2);
	int16_t flow = (I2c.receive()<<8) | I2c.receive();
	return flow;
}

void avsf_init(){
	// TODO: review this function and the register writes, sensitivity ranges
	uint8_t v = 0;
	I2c.begin();
	I2c.timeOut(20); // set 20 millisecond timeout
	I2c.read(LSM9DS0_ADDRESS_XM, LSM9DS0_REGISTER_WHO_AM_I_XM, 1);
	if(I2c.receive() == LSM9DS0_XM_ID){
		// init
		I2c.write(LSM9DS0_ADDRESS_XM, LSM9DS0_REGISTER_CTRL_REG1_XM, 0b00110111);
		I2c.write(LSM9DS0_ADDRESS_XM, LSM9DS0_REGISTER_CTRL_REG5_XM, 0b01101000);
		I2c.write(LSM9DS0_ADDRESS_XM, LSM9DS0_REGISTER_CTRL_REG5_XM, 0b01101000);
		// stream
		I2c.write(LSM9DS0_ADDRESS_XM, LSM9DS0_REG0_XM, 0b01000000);
		I2c.write(LSM9DS0_ADDRESS_XM, LSM9DS0_FIFO_CTRL_REG_XM, 0b01000000);
		// range - accel
		I2c.read(LSM9DS0_ADDRESS_XM, LSM9DS0_REGISTER_CTRL_REG2_XM, 1);
		v = I2c.receive(); v &= ~(0b00111000); v |= ACCEL_RANGE;
		I2c.write(LSM9DS0_ADDRESS_XM, LSM9DS0_REGISTER_CTRL_REG2_XM, v);
		// range - mag
		I2c.read(LSM9DS0_ADDRESS_XM, LSM9DS0_REGISTER_CTRL_REG6_XM, 1);
		v = I2c.receive(); v &= ~(0b01100000); v |= MAG_GAIN;
		I2c.write(LSM9DS0_ADDRESS_XM, LSM9DS0_REGISTER_CTRL_REG6_XM, v);
	}

	I2c.read(LSM9DS0_ADDRESS_G, LSM9DS0_REGISTER_WHO_AM_I_G, 1);
	if(I2c.receive() == LSM9DS0_G_ID){
		// init
		I2c.write(LSM9DS0_ADDRESS_G, LSM9DS0_REGISTER_CTRL_REG1_G, 0b00001111);
		// stream
		I2c.write(LSM9DS0_ADDRESS_XM, LSM9DS0_REG5_G, 0b01000000);
		I2c.write(LSM9DS0_ADDRESS_XM, LSM9DS0_FIFO_CTRL_REG_G, 0b01000000);
		// range - gyro
		I2c.read(LSM9DS0_ADDRESS_XM, LSM9DS0_REGISTER_CTRL_REG4_G, 1);
		v = I2c.receive(); v &= ~(0b01100000); v |= GYRO_SCALE;
		I2c.write(LSM9DS0_ADDRESS_XM, LSM9DS0_REGISTER_CTRL_REG4_G, v);
	}

	// Set up sense
	I2c.write(INA219_ADDRESS, INA219_REG_CALIBRATION, INA219_CALVALUE_HI);
	I2c.write(INA219_ADDRESS, INA219_REG_CALIBRATION, INA219_CALVALUE_LO);
	I2c.write(INA219_ADDRESS, INA219_REG_CONFIG, INA219_CONFIGVALUE_HI);
	I2c.write(INA219_ADDRESS, INA219_REG_CONFIG, INA219_CONFIGVALUE_LO);
}

void avsf_read(DATA* d){
	float avg = 0.0;
	float x_avg = 0.0;
	// Unwrap and rewrap:
	// dereference d's AV array, and point at which one it wants
	// ==> all are polling at 12.5 hz or once every 0.08 sec;
	for(int av_DATAROW = 0; av_DATAROW < 16; av_DATAROW++){
		av_read((int16_t*) &(d->AV)[av_DATAROW]);
		avg += sqrt(sq((d->AV)[av_DATAROW][0]) + \
			sq((d->AV)[av_DATAROW][1]) + \
			sq((d->AV)[av_DATAROW][2]));
		x_avg += (d->AV[av_DATAROW][0]);
	}
	d->FLOW = flow_read();
	sense_read((int16_t*) d->SENSE);
	avg /= 16.0; x_avg /= 16.0;
	if(avg > AVG_HIGH_TRIGGER && x_avg > XAVG_HIGH_TRIGGER){
		bitWrite(d->FLAGS, FLAG_AVS_H, 0);
		bitWrite(d->FLAGS, FLAG_AVS_L, 1);

	}
	else if(abs(avg) < AVG_LOW_TRIGGER && abs(x_avg) < XAVG_LOW_TRIGGER){
		bitWrite(d->FLAGS, FLAG_AVS_H, 1);
		bitWrite(d->FLAGS, FLAG_AVS_L, 0);

	}
	else if(abs(avg) > AVG_HIGH_TRIGGER && x_avg < XAVG_NEG_TRIGGER){
		bitWrite(d->FLAGS, FLAG_AVS_H, 1);
		bitWrite(d->FLAGS, FLAG_AVS_L, 1);
	}
	return;
}

void sense_read(int16_t* buf){
	I2c.read(INA219_ADDRESS, INA219_REG_SHUNTVOLTAGE, 2);
	buf[0] = ((I2c.receive()<<8) | I2c.receive());
	I2c.read(INA219_ADDRESS, INA219_REG_BUSVOLTAGE, 2);
	buf[1] = (( ((I2c.receive()<<8) | I2c.receive()) >> 3) * 4);
	// Force something related to current sense
	I2c.write(INA219_ADDRESS, INA219_REG_CALIBRATION, INA219_CALVALUE_HI);
	I2c.write(INA219_ADDRESS, INA219_REG_CALIBRATION, INA219_CALVALUE_LO);
	delay(1);
	I2c.read(INA219_ADDRESS, INA219_REG_CURRENT, 2);
	buf[2] = ((I2c.receive()<<8) | I2c.receive());
	I2c.read(INA219_ADDRESS, INA219_REG_POWER, 2);
	buf[3] = ((I2c.receive()<<8) | I2c.receive());
}

void av_read(int16_t* V){
	uint8_t buffer[6];
	// Read 6, starting from X LSB
	I2c.read(LSM9DS0_ADDRESS_XM, LSM9DS0_VALUE_START_A, 6, buffer);

	V[0] = (buffer[1]<<8) | buffer[0];
	V[1] = (buffer[3]<<8) | buffer[2];
	V[2] = (buffer[5]<<8) | buffer[4];

	// Read the magnetometer
	I2c.read(LSM9DS0_ADDRESS_XM, LSM9DS0_VALUE_START_M, 6, buffer);

	V[3] = (buffer[1]<<8) | buffer[0];
	V[4] = (buffer[3]<<8) | buffer[2];
	V[5] = (buffer[5]<<8) | buffer[4];

	// Read gyro
	I2c.read(LSM9DS0_ADDRESS_G, LSM9DS0_VALUE_START_G, 6, buffer);

	V[6] = (buffer[1]<<8) | buffer[0];
	V[7] = (buffer[3]<<8) | buffer[2];
	V[8] = (buffer[5]<<8) | buffer[4];
}
