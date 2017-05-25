/**
  空气质量检测仪 beta 0.1.1 v
  Created by TsuiXh
  time: 2017/4/21 13:50
*/

/* Libraries */
#include <dht11.h>
#include <SPI.h>
#include <TFT.h>
#include "Timer.h"

/* 定义引脚部分 */

//彩色屏幕针脚 (MOSI -- pin11 SCLK -- pin13 as default)
#define cs 10
#define dc 9
#define rst 8

//PM2.5传感器驱动脚和数据脚
#define PIN_DATA_OUT A0     //连接空气质量传感器模拟量输出的IO口
#define PIN_LED_VCC 2     //空气质量传感器中为内部LED供电

//温湿度传感器数据脚
#define dhtDataPin 3

//甲醛传感器数据脚
#define msDataPin 1

/* 常量定义 */
const int DELAY_BEFORE_SAMPLING = 280;  //采样前等待时间
const int DELAY_AFTER_SAMPLING = 40;  //采样后等待时间
const int DELAY_LED_OFF = 9680;     //间隔时间

/* 创建实例 */
TFT screen = TFT(cs, dc, rst);  //创建TFT实例
dht11 DHT11;  //创建dht实例
Timer t;  //定时器实例

/* 全局变量 */
char temp[6]; //温度
char humidity[6];//湿度
char dust[6]; //灰尘浓度
char ch4[6];  //甲醛浓度
char dew[6];  //露点温度
char suggest[8];  //空气质量

//用于暂存各项数据的临时变量
float tTemp = 0;
float tHumidity = 0;
double tDustCon = 0;
double tCh4 = 0;
double tDew = 0;
int tAqi = 0;

boolean isCh4Ready = false;

void setup() {
  Serial.begin(9600);
  Serial.println("Air detector start !");
  dustSensorSetup();
  tftSetup();
  dataInit();
  t.after(1000*60*5, ch4Ready); //设置5分钟后开始从甲醛传感器中读取数据
}

void loop() {
  //更新屏幕数据
  tftUpdate();
  t.update();
  //临时变量
  String tempVal = String("");

  /*
    获取温湿度数据
  */
  int chk = DHT11.read(dhtDataPin);
  if (chk == DHTLIB_OK) {
    //获取温度
    tTemp = (float)DHT11.temperature;
    //获取湿度
    tHumidity = (float)DHT11.humidity;
    //获取露点温度
    tDew = dewPointFast(tTemp, tHumidity);
  }

  /*
    获取灰尘传感器数据
  */
  //获取灰尘浓度
  tDustCon = getDustDensity(getOutputV());
  //计算空气质量指数
  tAqi = getAQI(tDustCon);

  /*
    获取甲醛传感器的读数（因为要预热3-5分钟，这里设置为3分钟）
  */
  if (isCh4Ready)
  {
    tCh4 = getFormaldehydeConcentration(msDataPin);
    Serial.print("CH4 concentration(ppm): ");
    Serial.println(tCh4);
  }
  //屏幕闪烁
  delay(500);
  
  //擦除屏幕数据
  tftErase();

  //更新显示数据
  tempVal = String(tTemp);
  tempVal.toCharArray(temp, 6);

  tempVal = String(tHumidity, 6);
  tempVal.toCharArray(humidity, 6);

  tempVal = String(tDew);
  tempVal.toCharArray(dew, 6);

  tempVal = String(tDustCon);
  tempVal.toCharArray(dust, 6);
  tempVal = getGradeInfo(tAqi);
  tempVal.toCharArray(suggest, 8);

  if (isCh4Ready) {
    tempVal = String(tCh4);
    tempVal.toCharArray(ch4, 6);
  }
}

/* custom functions */

/* dht传感器函数 */
//摄氏度转华氏度
double fahrenheit(double celsius) {
  return 1.8 * celsius + 32;
} 
//摄氏度转化为开氏温度
double kelvin(double celsius) {
  return celsius + 237.15;
}
//露点（在此温度时，空气饱和并产生露珠）
double dewPoint(double celsius, double humidity) {
  double A0 = 373.15 / (273.15 + celsius);
  double SUM = -7.90298 * (A0 - 1);
  SUM += 5.02808 * log10(A0);
  SUM += -1.3816e-7 * (pow(10, (11.344 * (1 - 1/A0))) - 1);
  SUM += 8.1328e-3 * (pow(10, (-3.49479) * (A0 - 1)) - 1);
  SUM += log10(1013.246);
  double VP = pow(10, SUM - 3) * humidity;
  double T = log(VP / 0.61078); //temp var
  return (241.88 * T) / (17.558 - T);
}
//快速计算露点：（5倍dewPoint）
double dewPointFast(double celsius, double humidity) {
  double a = 17.271;
  double b = 237.7;
  double temp = (a * celsius) / (b + celsius) + log(humidity / 100);
  double Td = (b * temp) / (a - temp);
  return Td; 
}

/* 灰尘传感器函数 */
//读取输出电压
double getOutputV() {
  digitalWrite(PIN_LED_VCC, LOW);
  delayMicroseconds(DELAY_BEFORE_SAMPLING);
  double analogOutput = analogRead(PIN_DATA_OUT);
  delayMicroseconds(DELAY_AFTER_SAMPLING);
  digitalWrite(PIN_LED_VCC, HIGH);
  delayMicroseconds(DELAY_LED_OFF);
  //Genuino 模拟量读取值的范围为0-1023,一下划算为0-5V
  double outputV = analogOutput / 1024 * 5;
  return outputV;
}
//根据输出电压计算灰尘密度
double getDustDensity(double outputV) {
  //输出电压和灰尘密度换算公式：ug/m3 = 200*(Vout) - 200 + offset_value
  double ugm3 = outputV * 200 - 200 + 181.7;
 
  //去除检测不到的范围
  if (ugm3 < 0) {
    ugm3 = 0;
  }
  return ugm3;
}
//根据灰尘密度计算AQI（环境空气质量指数）
double getAQI(double ugm3) {
  double aqiL = 0;
  double aqiH = 0;
  double bpL = 0;
  double bpH = 0;
  double aqi = 0;
  //根据pm2.5和aqi对应关系分别计算aqi
  if (ugm3 >= 0 && ugm3 <= 35) {
    aqiL = 0;
    aqiH = 50;
    bpL = 0;
    bpH = 35;
  } else if (ugm3 > 35 && ugm3 <= 75) {
    aqiL = 50;
    aqiH = 100;
    bpL = 35;
    bpH = 75;
  } else if (ugm3 > 75 && ugm3 <= 115) {
    aqiL = 100;
    aqiH = 150;
    bpL = 75;
    bpH = 115;
  } else if (ugm3 > 115 && ugm3 <= 150) {
    aqiL = 150;
    aqiH = 200;
    bpL = 115;
    bpH = 150;
  } else if (ugm3 > 150 && ugm3 <= 250) {
    aqiL = 200;
    aqiH = 300;
    bpL = 150;
    bpH = 250;
  } else if (ugm3 > 250 && ugm3 <= 350) {
    aqiL = 300;
    aqiH = 400;
    bpL = 250;
    bpH = 350;
  } else if (ugm3 > 350) {
    aqiL = 400;
    aqiH = 500;
    bpL = 350;
    bpH = 500;
  }
  
  aqi = (aqiH - aqiL) / (bpH - bpL) * (ugm3 - bpL) + aqiL;
  return aqi;
}
//根据AQI获取级别描述
String getGradeInfo(double aqi) {
  String gradeInfo;
  if (aqi > 0 && aqi <= 50) {
    gradeInfo = String("Perfect");
  } else if (aqi > 50 && aqi <= 100) {
    gradeInfo = String("Good");
  } else if (aqi > 100 && aqi <= 150) {
    gradeInfo = String("Mild");
  } else if (aqi > 150 && aqi <= 200) {
    gradeInfo = String("Medium");
  } else if (aqi > 200 && aqi <= 300) {
    gradeInfo = String("Heavily");
  } else if (aqi > 300 && aqi <= 500) {
    gradeInfo = String("Alert!");
  } else {
    gradeInfo = String("Terrible");
  }
  return gradeInfo;
}
//灰尘传感器初始化函数
void dustSensorSetup() {
  pinMode(PIN_DATA_OUT, INPUT);
  pinMode(PIN_LED_VCC, OUTPUT);
}

/* TFT 屏幕函数 */
//初始化显示信息
void tftSetup() {
  screen.begin();
  screen.background(0, 0, 0);
  screen.stroke(0, 130, 255);
  screen.rect(2,2,158,126);
  screen.stroke(255,255,255);
  screen.setTextSize(1);
  screen.text("Temp(oC):",5,15);
  screen.text("Humidity(%):", 5,35);
  screen.text("PM2.5(ug/m3):", 5, 55);
  screen.text("CH4(ppm):", 5, 75);
  screen.text("DewPoint(oC):", 5, 95);
  screen.text("AirQuality:", 5, 115); 
}
//更新屏幕内容
void tftUpdate() {
  screen.stroke(0, 130, 255);
  screen.setTextSize(2);
  //温度
  screen.text(temp, 85, 10);
  //湿度
  screen.text(humidity, 85, 30);
  //灰尘浓度
  screen.text(dust, 85, 50);
  //甲醛
  screen.text(ch4, 85, 70);
  //dew
  screen.text(dew, 85, 90);
  //suggest
  screen.text(suggest, 85, 110);
}
//擦除显示内容为下次内容更新做准备
void tftErase() {
  screen.stroke(0, 0, 0);
  screen.text(temp, 85, 10);
  screen.text(humidity, 85, 30);
  screen.text(dust, 85, 50);
  screen.text(ch4, 85, 70);
  screen.text(dew, 85, 90);
  screen.text(suggest, 85, 110);
}

/* 第一次未获得数据时的数据初始化 */
void dataInit() {
  String val = String("unknow");
  val.toCharArray(temp, 6);
  val.toCharArray(humidity, 6);
  val.toCharArray(dust, 6);
  val.toCharArray(dew, 6);
  val.toCharArray(suggest, 8);
  val = String("wait");
  val.toCharArray(ch4, 6);
}

/* 甲醛传感器函数 */
//预热结束后将状态变化为Ready状态
void ch4Ready() {
  isCh4Ready = true;
}
//读取数据
double getFormaldehydeConcentration(int sensorPin) {
  //wait before inquery
  delay(5);
  //读取模拟量输入
  int val = analogRead(sensorPin);
  Serial.print("Output Voltage (V):");
  Serial.println(val);
  //转换电压
  double voltage = val * (5 / 1023);
  //根据电压计算甲醛浓度
  double logTemp = (-2.631) + 1.528 * voltage + (-0.125) * voltage * voltage;
  //返回浓度值
  double ppm = pow(10, logTemp);
  Serial.print("ppm:");
  Serial.println(ppm);
  return ppm;
}
