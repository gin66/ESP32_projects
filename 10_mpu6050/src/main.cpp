#include "template.h"
#include <Wire.h>
#include <Kalman.h>

using namespace std;

TwoWire i2c(0);
#define MPU_addr ((uint16_t)0x68)

//#define RESTRICT_PITCH // Comment out to restrict roll to ±90deg instead - please read: http://www.freescale.com/files/sensors/doc/app_note/AN3461.pdf

Kalman kalmanX; // Create the Kalman instances
Kalman kalmanY;

/* IMU Data */
double accX, accY, accZ;
double gyroX, gyroY, gyroZ;
int16_t tempRaw;

double gyroXangle, gyroYangle; // Angle calculate using the gyro only
double compAngleX, compAngleY; // Calculated angle using a complementary filter
double kalAngleX, kalAngleY; // Calculated angle using a Kalman filter

uint32_t timer;
uint8_t i2cData[14]; // Buffer for I2C data

// can be used as parameter to tpl_command_setup
// void execute(enum Command command) {}

// can be used as parameter to tpl_websocket_setup
// void add_ws_info(DynamicJsonDocument* myObject) {}

//---------------------------------------------------
void setup() {
  tpl_system_setup(0);  // no deep sleep

  Serial.begin(115200);
  Serial.setDebugOutput(false);
 
  // Wait OTA
  tpl_wifi_setup(true, true, (gpio_num_t)255/*tpl_ledPin*/);
  tpl_webserver_setup();
  tpl_websocket_setup(NULL);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);

#ifdef IS_ESP32CAM
  uint8_t fail_cnt = 0;
#ifdef BOTtoken
  tpl_camera_setup(&fail_cnt, FRAMESIZE_QVGA);
#else
  tpl_camera_setup(&fail_cnt, FRAMESIZE_VGA);
#endif
#endif

#ifdef BOTtoken
  tpl_telegram_setup(BOTtoken, CHAT_ID);
#endif

#define SDA 22 // DATA
#define SCL 21 //CLK

  i2c.begin(SDA, SCL, 100000);
  pinMode(SDA, INPUT_PULLUP);
  pinMode(SCL, INPUT_PULLUP);

  i2c.beginTransmission(MPU_addr);
  i2c.write(0x6B);  // PWR_MGMT_1 register
  i2c.write(0);     // set to zero (wakes up the MPU-6050)
  i2c.endTransmission(true);

  Serial.println("Setup done.");
}

int16_t AcX,AcY,AcZ,Tmp,GyX,GyY,GyZ;

void mpu_read(){
	i2c.beginTransmission(MPU_addr);
	i2c.write(0x3B);  // starting with register 0x3B (ACCEL_XOUT_H)
	i2c.endTransmission(false);
	i2c.requestFrom(MPU_addr,(uint8_t)14,true);  // request a total of 14 registers
	AcX=i2c.read()<<8|i2c.read();  // 0x3B (ACCEL_XOUT_H) & 0x3C (ACCEL_XOUT_L)
	AcY=i2c.read()<<8|i2c.read();  // 0x3D (ACCEL_YOUT_H) & 0x3E (ACCEL_YOUT_L)
	AcZ=i2c.read()<<8|i2c.read();  // 0x3F (ACCEL_ZOUT_H) & 0x40 (ACCEL_ZOUT_L)
	Tmp=i2c.read()<<8|i2c.read();  // 0x41 (TEMP_OUT_H) & 0x42 (TEMP_OUT_L)
	GyX=i2c.read()<<8|i2c.read();  // 0x43 (GYRO_XOUT_H) & 0x44 (GYRO_XOUT_L)
	GyY=i2c.read()<<8|i2c.read();  // 0x45 (GYRO_YOUT_H) & 0x46 (GYRO_YOUT_L)
	GyZ=i2c.read()<<8|i2c.read();  // 0x47 (GYRO_ZOUT_H) & 0x48 (GYRO_ZOUT_L)

	float Ac = sqrt(1.0*AcX*AcX + 1.0*AcY*AcY + 1.0*AcZ*AcZ);
	float Gy = sqrt(1.0*GyX*GyX + 1.0*GyY*GyY + 1.0*GyZ*GyZ);

	char buf[255];
	sprintf(buf, "Acceleration: %6d %6d %6d => %6.1f, Giro: %6d %6d %6d => %6.1f, Temp: %d", AcX, AcY, AcZ, Ac, GyX, GyY, GyZ, Gy, (Tmp+12420)/340);
	Serial.println(buf);
}

void loop() {
  mpu_read();
  const TickType_t xDelay = 50 / portTICK_PERIOD_MS;
  vTaskDelay(xDelay);
}
