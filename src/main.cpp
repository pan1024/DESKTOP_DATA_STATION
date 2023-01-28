#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Adafruit_BMP280.h>
#include <TM1637.h>
#include <U8g2lib.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>



#define DHTPIN  5
#define DHTTYPE DHT11 
#define BMP_CS  4
#define BACKGRPUND_LIGHT 15

DHT_Unified dht(DHTPIN, DHTTYPE);//温湿度计模块
Adafruit_BMP280 bmp(BMP_CS);//气压计模块
TM1637 tm1637(0, 16);//数码管模块
U8G2_ST7567_JLX12864_F_4W_HW_SPI u8g2(U8G2_MIRROR, /* cs=*/ 1, /* dc=*/ 3, /* reset=*/ 2);//屏幕模块

const char* ssid = "see you again_1"; //配置WiFi名称
const char* password = "102696++pan"; //配置WiFi密码

WiFiClient client;
HTTPClient http;
AsyncWebServer server(80);

//静态iP信息
IPAddress staticIP(192, 168, 0, 253);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(192, 168, 0,1);

String ip_address;//连接后获得的IP地址

DynamicJsonDocument date_doc(1024);
DynamicJsonDocument weather_doc(1024);

float temprature;//温度
float humidity;//湿度
float pressure;//大气压

bool data_flag=false;
bool display_flag=false;

String time_url="http://quan.suning.com/getSysTime.do";// 苏宁授时网页
String date_data;//日期json数据
String weather_data;//天气json字符串数据

 const char *host = "api.seniverse.com";//心知天气服务器
// const char *privateKey = "SA7viYHe_aFgd8u06";//私钥
// const char *city = "WTG7R0CSBHZ9";//城市码
// const char *language = "en";//语言

String weather_url = "/v3/weather/daily.json?key=SA7viYHe_aFgd8u06&location=WTG7R0CSBHZ9&language=en&unit=c&start=0&day=4";//完成拼接的请求url


typedef struct{//天气数据结构体
  String text_day;
  String text_night;
  String high;
  String low;
  String wind_speed;
}WEATHER;

typedef struct{//日期时间结构体
  String year;
  String month;
  String day;
  String hour="25";
  String minte;
  String second;

}DATE;

typedef struct{
  uint16_t CPU_Utilization;
  uint16_t Memory_Utilization;
  uint16_t GPU_Utilization;
  uint16_t Mother_board_Temp;
  uint16_t CPU_Temp;
  uint16_t GPU_Temp;
  uint16_t GPU_Fan;
  uint16_t CPU_Fan;
  
}COMPUTE_DATA;




WEATHER weather[2];
DATE date;
COMPUTE_DATA compute_data;

//无阻塞延时的暂存时间变量
unsigned long previousMillis_sensor = 0;
unsigned long previousMillis_time = 0;
unsigned long previousMillis_colon = 0;
unsigned long previousMillis_weather = 0;
unsigned long previousMillis_refresh_monitor= 0;
unsigned long previousMillis_switch_display= 0;

static const unsigned char PROGMEM temperature_bmp[] = {0x07,0x05,0xF7,0x08,0x08,0x08,0x08,0xF0};//摄氏度符号
static const unsigned char PROGMEM humidity_bmp[] = {0x98,0x58,0x20,0x10,0x08,0x04,0x1A,0x19};//百分比符号
static const unsigned char PROGMEM pressure_bmp[] = {0x00,0x0F,0x09,0x79,0x5F,0x51,0xF1,0x01};//帕符号


uint8_t line_ordinate=0;//动态框横坐标

void u8g2_prepare(void);
String weekDay(int year,uint8_t month,uint8_t day);
WEATHER weather_data_parse(String weather_data,int i);
void get_sensor_data();
void get_date();
void get_weather();
void set_mode(bool mode);
void digital_tube_display();
void display();
void data_display();
void notFound(AsyncWebServerRequest *request);
void get_compute_data(AsyncWebServerRequest *request);

void setup() {
  pinMode(BACKGRPUND_LIGHT,OUTPUT);
 
  dht.begin();
  bmp.begin();
  bmp.setSampling(Adafruit_BMP280::MODE_FORCED,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */

  tm1637.init();
  tm1637.setBrightness(5);

  u8g2.begin();

  u8g2_prepare();

  //静态IP配置
  WiFi.config(staticIP, gateway, subnet, dns, dns);
  //wifi模式_STA 为连接 AP为热点
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
  u8g2.clearBuffer();
  u8g2.drawStr(0,0,"WIFI CONNECTING.......");
  u8g2.sendBuffer();
  digitalWrite(BACKGRPUND_LIGHT,HIGH);
  delay(100);
  }
  digitalWrite(BACKGRPUND_LIGHT,LOW);

  ip_address=WiFi.localIP().toString();//ip地址

  http.setTimeout(500);
  http.begin(client,time_url);

  server.on("/",HTTP_GET,get_compute_data);
  server.onNotFound(notFound);
  server.begin();


  get_date();//获取一次日期时间为下面的模式选择铺垫
}
void loop() {
  if(0<=date.hour.toInt()&&date.hour.toInt()<=5)//休眠
  { 
    set_mode(false);//休眠模式
    get_date();
    delay(5000);
  }
  else{//正常工作
    unsigned long currentMillis = millis();

    set_mode(true);//工作模式
    
    if (currentMillis - previousMillis_sensor >= 400) {//传感器数据获取
      previousMillis_sensor = currentMillis;
      get_sensor_data();
    }
    else if (currentMillis - previousMillis_time >= 1800) {//日期和时间数据获取
      previousMillis_time = currentMillis;
      get_date();
    }
    else if (currentMillis - previousMillis_weather >= 6000) {//天气数据获取
      previousMillis_weather = currentMillis;
      get_weather();
    }
    else if (currentMillis - previousMillis_colon >= 850) {//数码管刷新
      previousMillis_colon = currentMillis;
      digital_tube_display();
    }
    else if((data_flag)&&(currentMillis - previousMillis_switch_display >= 5000)){//数码管刷新
      previousMillis_switch_display = currentMillis;

      display_flag=!display_flag;
      data_flag=false;
    }
    else if (currentMillis - previousMillis_refresh_monitor >= 300) {//屏幕刷新
      previousMillis_refresh_monitor = currentMillis;

      if(display_flag)
      {
          data_display(); 
      }
      else
      {
          display();
      }
    }
  }
}

void u8g2_prepare(void) {//屏幕初始化
  u8g2.setFont(u8g2_font_minicute_tr);
  u8g2.setFontRefHeightExtendedText();
  u8g2.setDrawColor(1);
  u8g2.setFontPosTop();
  u8g2.setFontDirection(0);
}

String weekDay(int year,uint8_t month,uint8_t day){//通过年月日计算星期
      uint8_t week;
      String weekday="";
      year=year%100;
      if(month>=3)
      {
          week=year+(year/4)-35+(26*(month+1)/10)+day-1;
      }
      else
      {
          month=month+12;
          year=year-1;
          week=year+(year/4)-35+(26*(month+1)/10)+day-1;
      }
      switch(week%7)
      {
          case 0:weekday="Sunday" ;break;
          case 1:weekday="Monday" ;break;
          case 2:weekday="Tuesday" ;break;
          case 3:weekday="Wednesday" ;break;
          case 4:weekday="Thursday" ;break;
          case 5:weekday="Friday" ;break;
          case 6:weekday="Saturday" ;break;
      }
      return weekday;
  }

WEATHER weather_data_parse(String weather_data,int i)//天气json数据解析
  {
    deserializeJson(weather_doc, weather_data);//反序列化json
    JsonObject obj = weather_doc.as<JsonObject>();
    WEATHER weather;
    weather.text_day=obj["results"][0]["daily"][i]["text_day"].as<String>();
    weather.text_night=obj["results"][0]["daily"][i]["text_night"].as<String>();
    weather.high=obj["results"][0]["daily"][i]["high"].as<String>();
    weather.low=obj["results"][0]["daily"][i]["low"].as<String>();
    weather.wind_speed=obj["results"][0]["daily"][i]["wind_speed"].as<String>();
    return weather;
  }

void get_sensor_data()
{
  sensors_event_t event;
      dht.temperature().getEvent(&event);
      if (!(isnan(event.temperature)))  temprature=event.temperature;
      dht.humidity().getEvent(&event);
      if (!(isnan(event.relative_humidity))) humidity=event.relative_humidity;
      if (bmp.takeForcedMeasurement()) pressure=bmp.readPressure();
}    

void get_date(){//日期时间获取
  uint8_t httpCode = http.GET();
  if ((httpCode > 0)&&(httpCode == HTTP_CODE_OK) ) { 
    //"sysTime2":"2021-12-07 21:07:31","sysTime1":"2021 12 07 210731"}
    deserializeJson(date_doc, http.getString());//反序列化json
    JsonObject obj = date_doc.as<JsonObject>();
    
    date.year=String(obj["sysTime1"]+"").substring(0,4);
    date.month=String(obj["sysTime1"]+"").substring(4,6);
    date.day=String(obj["sysTime1"]+"").substring(6,8);
    date.hour=String(obj["sysTime1"]+"").substring(8,10);
    date.minte=String(obj["sysTime1"]+"").substring(10,12);
    date.second=String(obj["sysTime1"]+"").substring(12,14);
  }
}

void get_weather()//天气数据获取
{
  client.connect(host, 80);
  client.print(String("GET ") + weather_url + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Connection: close\r\n\r\n");
  char endOfHeaders[] = "\r\n\r\n";
  client.find(endOfHeaders);
  weather_data=client.readStringUntil('\n');
  client.stop();
  if(!(weather_data_parse(weather_data,0).text_day.equals("null")))
  {
    weather[0]=weather_data_parse(weather_data,0);
    weather[1]=weather_data_parse(weather_data,1);
  }
}

void set_mode(bool mode)
{
  static bool work_mode=true;
  static bool sleep_mode=true;
  if(mode)
  {
    if(work_mode)//配置正常工作模式
    {
      digitalWrite(BACKGRPUND_LIGHT,HIGH);//屏幕背光设置
      u8g2.sleepOff();
      tm1637.onMode();
      bmp.setSampling(Adafruit_BMP280::MODE_FORCED,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */
      work_mode=false;
      sleep_mode=true;
    }
  }
  else
  {
    if(sleep_mode)//配置休眠模式
    {
      digitalWrite(BACKGRPUND_LIGHT,LOW);//屏幕背光设置
      tm1637.offMode();//关闭数码
      u8g2.sleepOn();//关闭屏幕
      bmp.setSampling(bmp.MODE_SLEEP);
      sleep_mode=false;
      work_mode=true;
    }
  }

}

void digital_tube_display()//数码管显示
{
  tm1637.refresh();
  tm1637.switchColon();
  //显示时间
  tm1637.display(date.hour+date.minte);  
}

void display()//屏幕显示
{
  //屏幕显示数据
  u8g2.clearBuffer();
  //设置边框
  u8g2.drawLine(0,0,0,63);
  u8g2.drawLine(127,0,127,63);
  u8g2.drawLine(63,0,63,127);
  u8g2.drawLine(0,21,63,21);
  u8g2.drawLine(0,50,63,50);
  //设置上下动态边框
  u8g2.drawLine(0,63,line_ordinate,63);
  u8g2.drawLine(127-line_ordinate,63,127,63);
  u8g2.drawLine(0,0,line_ordinate,0);
  u8g2.drawLine(127-line_ordinate,0,127,0);
  line_ordinate+=1;
  if(line_ordinate>64) line_ordinate=0;
  //显示日期
  u8g2.drawStr(2,2,(date.year+"-"+date.month+"-"+date.day).c_str());
  //显示星期
  u8g2.drawStr(2,10,(weekDay(date.year.toInt(),date.month.toInt(),date.day.toInt())).c_str());
  //显示温湿度等传感器数据
  u8g2.drawStr(1,22,("TEMP:"+String(temprature)).c_str());
  u8g2.drawXBMP(54,22,8,8,temperature_bmp);
  u8g2.drawStr(1,31,("HUMI:"+String(humidity)).c_str());
  u8g2.drawXBMP(54,31,8,8,humidity_bmp);
  if (pressure >= 101325) u8g2.drawStr(1,40,("HIGH:"+String((int)pressure- 101325)).c_str());
  else u8g2.drawStr(1,40,("LOW:"+String(101325-(int)pressure)).c_str());
  u8g2.drawXBMP(54,40,8,8,pressure_bmp);
  //显示ip地址
  u8g2.setDrawColor(2);
  u8g2.drawStr(2,52,ip_address.c_str());
  u8g2.drawBox(2,52,60,10);
  u8g2.setDrawColor(1);
  //显示天气信息
  u8g2.setFont(u8g2_font_tinyunicode_tf);
  u8g2.drawStr(65,0,"-----today-----");
  u8g2.drawStr(65,6,("D:"+weather[0].text_day).c_str());
  u8g2.drawStr(65,12,("N:"+weather[0].text_night).c_str());
  u8g2.drawStr(65,18,("temp:"+weather[0].low+" ::::: "+weather[0].high).c_str());
  u8g2.drawStr(65,24,("wind: "+String(weather[0].wind_speed.toFloat()/3.6)+"m/s").c_str());
  u8g2.drawStr(65,30,"---tomorrow-----");
  u8g2.drawStr(65,36,("D:"+weather[1].text_day).c_str());
  u8g2.drawStr(65,42,("N:"+weather[1].text_night).c_str());
  u8g2.drawStr(65,48,("temp:"+weather[1].low+" ::::: "+weather[1].high).c_str());
  u8g2.drawStr(65,54,("wind: "+String(weather[1].wind_speed.toFloat()/3.6)+"m/s").c_str());
  u8g2.sendBuffer();
  u8g2.setFont(u8g2_font_minicute_tr);
}

void data_display()
{
  u8g2.clearBuffer();
  u8g2.drawStr(0,0,("CPU TEMP--------"+String(compute_data.CPU_Temp)).c_str());
  u8g2.drawXBMP(100,0,8,8,temperature_bmp);
  u8g2.drawStr(0,8,("GPU TEMP--------"+String(compute_data.GPU_Temp)).c_str());
  u8g2.drawXBMP(100,8,8,8,temperature_bmp);
  u8g2.drawStr(0,16,("BOARD-----------"+String(compute_data.Mother_board_Temp)).c_str());
  u8g2.drawXBMP(100,16,8,8,temperature_bmp);
  u8g2.drawStr(0,24,("CPU USE---------"+String(compute_data.CPU_Utilization)).c_str());
  u8g2.drawXBMP(100,24,8,8,humidity_bmp);
  u8g2.drawStr(0,32,("GPU USE---------"+String(compute_data.GPU_Utilization)).c_str());
  u8g2.drawXBMP(100,32,8,8,humidity_bmp);
  u8g2.drawStr(0,40,("MEMORY USE-----"+String(compute_data.Memory_Utilization)).c_str());
  u8g2.drawXBMP(100,40,8,8,humidity_bmp);
  u8g2.drawStr(0,48,("CPU FNA---------"+String(compute_data.CPU_Fan)+"RPM").c_str());
  u8g2.drawStr(0,56,("GPU FAN---------"+String(compute_data.GPU_Fan)+"RPM").c_str());
  u8g2.sendBuffer();
  
}

void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}

void get_compute_data(AsyncWebServerRequest *request) {
  if(request!=nullptr)
  {
    uint8_t paramNumber=request->params();
    AsyncWebParameter* param;
    String param_name;
    uint16_t param_value;
    for(int i=0;i<paramNumber;i++)
    {
      param = request->getParam(i);
      if(param!=nullptr)
      {
        param_name=param->name();
        param_value=param->value().toInt();

        if(param_name.equals("CPUUtilization"))compute_data.CPU_Utilization=param_value;
        else if(param_name.equals("MemoryUtilization"))compute_data.Memory_Utilization=param_value;
        else if(param_name.equals("GPUUtilization"))compute_data.GPU_Utilization=param_value;
        else if(param_name.equals("Motherboard"))compute_data.Mother_board_Temp=param_value;
        else if(param_name.equals("CPU"))compute_data.CPU_Fan=param_value;
        else if(param_name.equals("CPUDiode"))compute_data.CPU_Temp=param_value;
        else if(param_name.equals("GPU"))compute_data.GPU_Fan=param_value;
        else if(param_name.equals("GPUDiode"))compute_data.GPU_Temp=param_value;
      }
    }
    data_flag=true;
    request->send(200, "text/plain", "success!");
  }
  
  
}