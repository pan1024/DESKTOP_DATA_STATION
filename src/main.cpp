#include <Arduino.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Adafruit_BMP280.h>
#include <TM1637.h>
#include <U8g2lib.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <FileSystem/FileSystem.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>

#define DHTPIN  5
#define DHTTYPE DHT11 
#define BMP_CS  4

#define SCREEN_CS 1
#define SCREEN_DC 3
#define SCREEN_RESET 2
#define SCREEN_BACKGROUND_LIGHT 15

#define DIGITAL_TUBE_CLK 0
#define DIGITAL_TUBE_DATA 16

DHT dht(DHTPIN, DHTTYPE);//温湿度计模块
Adafruit_BMP280 bmp(BMP_CS);//气压计模块
TM1637 tm1637(DIGITAL_TUBE_CLK, DIGITAL_TUBE_DATA);//数码管模块
U8G2_ST7567_JLX12864_F_4W_HW_SPI u8g2(U8G2_MIRROR, SCREEN_CS, SCREEN_DC, SCREEN_RESET);//屏幕模块

AsyncWebServer server(80);

String ip_address;//连接后获得的IP地址

float temprature;//温度
float humidity;//湿度
uint32_t pressure;//大气压


bool compute_data_flag=false;//获取计算机信息的标志
bool display_mode_flag=false;//显示数据类型的标志
bool weather_switch_flag=true;//转换今天和明天的天气标志

//无阻塞延时的暂存时间变量
unsigned long currentMillis;
unsigned long previousMillis_sensor = 0;
unsigned long previousMillis_time = 0;
unsigned long previousMillis_colon = 0;
unsigned long previousMillis_weather = 0;
unsigned long previousMillis_refresh_monitor= 0;
unsigned long previousMillis_switch_display= 0;

const unsigned char  temperature_bmp[] = {0x07,0x05,0xF7,0x08,0x08,0x08,0x08,0xF0};//摄氏度符号
const unsigned char  percent_bmp[] = {0x98,0x58,0x20,0x10,0x08,0x04,0x1A,0x19};//百分比符号
const unsigned char  pressure_bmp[] = {0x00,0x0F,0x09,0x79,0x5F,0x51,0xF1,0x01};//帕符号

typedef struct{//天气数据结构体
  String day_weather="未知";
  String night_weather="未知";
  String max_temperature="99";
  String min_temperature="99";
  String wind_speed="未知";
}WEATHER;

typedef struct{//日期时间结构体
  String year="9999";
  String month="99";
  String day="99";
  String hour="99";
  String minte="99";
  String second="99";
  String week="unknow";
}DATE;

typedef struct{//电脑数据结构体
  String CPU_Utilization;
  String Memory_Utilization;
  String GPU_Utilization;
  String Mother_board_Temp;
  String CPU_Temp;
  String GPU_Temp;
  String GPU_Fan;
  String CPU_Fan;
}COMPUTE_DATA;

typedef struct{//配置信息结构体
  String wifi_name;
  String wifi_password;
  uint8_t start_sleep_time;
  uint8_t end_sleep_time;
  String  city_code;
  String weather_api_flag;
  String weather_key_gd;
  String weather_key_xz;
}CONFIG;

WEATHER weather[2];
DATE date;
COMPUTE_DATA compute_data;
CONFIG device_config;
String wifi_list="";



void u8g2_prepare();
void device_init();
String weekDay(uint16_t year,uint8_t month,uint8_t day);
WEATHER weather_data_parse(String weather_data,int i,boolean flag);
void get_date();
void get_sensor_data();
void get_weather_gd();
void get_weather_xz();
void set_mode(bool mode);
void digital_tube_display();
void data_display();
void data_display_disconncted();
void computer_data_display();
void create_ap();
uint8_t wifi_connect(String wifi_name,String wifi_password);
void sever_start();
void dns_server_start();
void write_config_txt(CONFIG config);
String read_config_txt(String file_name);
CONFIG get_config(String wifi_config_str,String other_config_str);
void notFound(AsyncWebServerRequest *request);
void get_computer_data(AsyncWebServerRequest *request);
void set_config(AsyncWebServerRequest *request);                                             
void ota_init();
void wifi_station();
String* scan_wifi();
String select_scan_wifi(String* wifi_array);

uint8_t network_station_count=0;//网络状态临时计数
bool network_station=true;//网络状态

void setup(){
  
  
  device_init();
  
  digitalWrite(SCREEN_BACKGROUND_LIGHT,HIGH);

  String wifi_config_str=read_config_txt("wifi_config");
  String other_config_str=read_config_txt("other_config");
  device_config=get_config(wifi_config_str,other_config_str);//获取配置信息
 
  if(String("nullptr").equals(wifi_config_str)){//配置文件不存在时会返回nullptr
    create_ap();//开启热点
    wifi_list=select_scan_wifi(scan_wifi());
    sever_start();//开启服务器
    dns_server_start();//开启dns服务器
  }
  else{//读取到配置文件
     
    uint8_t flag=wifi_connect(device_config.wifi_name,device_config.wifi_password);//连接wifi
    if(flag!=WL_CONNECTED){//连接失败
      deleteFile(LittleFS,"/wifi_config.txt");//清除wifi信息
      network_station=false;
      //ESP.restart();
    }
    if(network_station){
      wifi_list=select_scan_wifi(scan_wifi());
      ip_address=WiFi.localIP().toString();//获取ip地址
      ota_init();
      sever_start();//开启服务器
      get_date();//获取一次日期时间为下面的模式选择铺垫
      if(device_config.weather_api_flag.equals("gd"))
      {
        get_weather_gd(); //获取一次天气
      }
      else if(device_config.weather_api_flag.equals("xz"))
      {
        get_weather_xz(); //获取一次天气
      }
    } else{
      
    tm1637.offMode();
    WiFi.mode(WIFI_OFF);
    server.end();
  }
   
    u8g2.clearBuffer();
    u8g2.drawStr(0,0,"LOADING SYSTEM.......");
    u8g2.sendBuffer();

    digitalWrite(SCREEN_BACKGROUND_LIGHT,LOW);
  }
 
  
}

void loop() {
  ArduinoOTA.handle();//ota轮询
  if(network_station){
    if(device_config.start_sleep_time<=date.hour.toInt()&&date.hour.toInt()<=device_config.end_sleep_time){//休眠
      set_mode(false);//休眠模式
      get_date();
      wifi_station();
      delay(5000);
    }
    else{//正常工作
      set_mode(true);//工作模式
      currentMillis = millis();
        if (currentMillis - previousMillis_refresh_monitor >= 500) {//屏幕刷新
          previousMillis_refresh_monitor = currentMillis;
          
          if(display_mode_flag)computer_data_display();//电脑数据展示 
          else 
          {
          get_sensor_data();
          data_display();//传感器数据展示
          }
          
          currentMillis = millis();
        }

        if (currentMillis - previousMillis_colon >= 800) {//数码管刷新
          previousMillis_colon = currentMillis;
          digital_tube_display();

          currentMillis = millis();
        }

        if(currentMillis - previousMillis_time >= 1000){//日期和时间数据获取
          previousMillis_time = currentMillis;
          get_date();
          weather_switch_flag=!weather_switch_flag;//切换今天和明天的天气

          currentMillis = millis();
        }

        if (currentMillis - previousMillis_switch_display >= 5000){//每五秒钟切换一次显示模式（传感器数据或电脑数据
          previousMillis_switch_display = currentMillis;
          if(compute_data_flag){//存在电脑数据传输则定时切换显示数据
            display_mode_flag=!display_mode_flag;
            compute_data_flag=false;
          }
          else{
            display_mode_flag=false;
          }
          
          wifi_station();//监测wifi状态

          currentMillis = millis();
        }

        if (currentMillis - previousMillis_weather >= 300000) {//天气数据获取
          previousMillis_weather = currentMillis;
          if(device_config.weather_api_flag.equals("gd"))
          {
            get_weather_gd(); //获取一次天气
          }
          else if(device_config.weather_api_flag.equals("xz"))
          {
            get_weather_xz(); //获取一次天气
          }
          
          currentMillis = millis();
        }
    }
  }
  else{
  set_mode(true);//工作模式
  static bool temp=true;
  if(temp){
    tm1637.clearScreen();
    tm1637.offMode();
    WiFi.mode(WIFI_OFF);
    server.end();
    temp=false;
  }
  currentMillis = millis();
    if (currentMillis - previousMillis_refresh_monitor >= 500) {//屏幕刷新
      previousMillis_refresh_monitor = currentMillis;
      get_sensor_data();
      data_display_disconncted();//传感器数据展示
      currentMillis = millis();
    }
  }
}

void u8g2_prepare(){//屏幕初始化
  u8g2.setFont(u8g2_font_minicute_tr);
  u8g2.setFontRefHeightExtendedText();
  u8g2.setDrawColor(1);
  u8g2.setFontPosTop();
  u8g2.setFontDirection(0);
  u8g2.setContrast(60);
}

void device_init(){
  pinMode(SCREEN_BACKGROUND_LIGHT,OUTPUT);

  dht.begin();
  bmp.begin();
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X16,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */

  tm1637.init();
  tm1637.setBrightness(5);
  tm1637.refresh();
  tm1637.clearScreen();
  u8g2.begin();
  u8g2_prepare();
  LittleFS.begin();
  
}

void get_sensor_data(){
  // sensors_event_t event;
  // dht.temperature().getEvent(&event);
  // if (!(isnan(event.temperature)))  
  temprature=dht.readTemperature();
  // dht.humidity().getEvent(&event);
  // if (!(isnan(event.relative_humidity))) 
  humidity=dht.readHumidity();
  // if (bmp.takeForcedMeasurement()) 
  pressure=bmp.readPressure();
}    

String weekDay(uint16_t year,uint8_t month,uint8_t day){//通过年月日计算星期
  uint8_t week;
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
      case 0:return "Sunday" ;
      case 1:return "Monday" ;
      case 2:return "Tuesday" ;
      case 3:return "Wednesday" ;
      case 4:return "Thursday" ;
      case 5:return "Friday" ;
      case 6:return "Saturday" ;
  }
  return "ERROR";
}

void get_date(){//日期时间获取
  String date_url="http://quan.suning.com/getSysTime.do";// 苏宁授时网页
  WiFiClient wifi_client;
  HTTPClient date_http_client;
  date_http_client.setTimeout(500);
  date_http_client.begin(wifi_client,date_url);

  static String previous_day="0";
  uint8_t httpCode = date_http_client.GET();
  if ((httpCode > 0)&&(httpCode == HTTP_CODE_OK) ) { 
    //{"sysTime2":"2021-12-07 21:07:31","sysTime1":"20211207210731"}
    DynamicJsonDocument date_doc(1024);
    deserializeJson(date_doc, date_http_client.getString());//反序列化json
    date_http_client.end();
    JsonObject obj = date_doc.as<JsonObject>();
    String date_str=obj["sysTime1"];
    if(!date_str.equals("null"))
    {
      date.hour=date_str.substring(8,10);
      date.minte=date_str.substring(10,12);
      date.second=date_str.substring(12,14);
      if(!previous_day.equals(date_str.substring(6,8)))//只有日期发生变化才重新获取年月日星期
      {
        date.year=date_str.substring(0,4);
        date.month=date_str.substring(4,6);
        date.day=date_str.substring(6,8);
        previous_day=date.day;
        date.week=weekDay(date.year.toInt(),date.month.toInt(),date.day.toInt());
      }
    }
    if(network_station_count!=0)network_station_count=0;
  }
  else{
    ++network_station_count;
  }
  
}

void get_weather_gd(){//天气数据获取（使用高德
String weather_url="http://restapi.amap.com/v3/weather/weatherInfo?city="+device_config.city_code+"&key="+device_config.weather_key_gd+"&extensions=all";
WiFiClient wifi_client;
HTTPClient weather_http_client;
weather_http_client.setTimeout(500);
weather_http_client.begin(wifi_client,weather_url);

 uint8_t httpCode = weather_http_client.GET();
  if ((httpCode > 0)&&(httpCode == HTTP_CODE_OK) ) { 
    DynamicJsonDocument weather_doc(1024);
    String weather_data=weather_http_client.getString();
    weather_http_client.end();
    weather[0]=weather_data_parse(weather_data,0,true);     
    weather[1]=weather_data_parse(weather_data,1,true);
  }
}

void get_weather_xz(){//天气数据获取（使用心知 
  WiFiClient weather_client;  
  weather_client.setTimeout(500);
  // String reqRes = "/v3/weather/daily.json?key=SA7viYHe_aFgd8u06&location=WTG7R0CSBHZ9&language=zh-Hans&unit=c&start=0&day=0" ;
  // 建立http请求信息
  // String httpRequest = String("GET ") + reqRes + " HTTP/1.1\r\n" + "Host: api.seniverse.com"+ "\r\n" +"Connection: close\r\n\r\n";
  
  String httpRequest = String("GET ") +"/v3/weather/daily.json?key="+device_config.weather_key_xz+"&location="+device_config.city_code+"&language=zh-Hans&unit=c&start=0&day=0 HTTP/1.1\r\nHost: api.seniverse.com\r\nConnection: close\r\n\r\n";
  if (weather_client.connect("api.seniverse.com", 80)){
    // 向服务器发送http请求信息
    weather_client.print(httpRequest);
    // 使用find跳过HTTP响应头
    String weather_data;
    if (weather_client.find("\r\n\r\n")) weather_data = weather_client.readStringUntil('\n');
    weather_client.stop(); //stop this request
     //利用ArduinoJson库解析心知天气响应信息
    weather[0]=weather_data_parse(weather_data,0,false);     
    weather[1]=weather_data_parse(weather_data,1,false);
 }
}

WEATHER weather_data_parse(String weather_data,int i,boolean flag){//天气json数据解析
  DynamicJsonDocument weather_doc(1024);
  deserializeJson(weather_doc, weather_data);//反序列化json
  JsonObject obj = weather_doc.as<JsonObject>();
  JsonObject obj_temp;
  WEATHER weather;
  if(flag)
  {
    obj_temp=obj["forecasts"][0]["casts"][i];
    weather.day_weather=obj_temp["dayweather"].as<String>();
    weather.night_weather=obj_temp["nightweather"].as<String>();
    weather.max_temperature=obj_temp["daytemp"].as<String>();
    weather.min_temperature=obj_temp["nighttemp"].as<String>();
    weather.wind_speed=obj_temp["daypower"].as<String>();
  }
  else{
    obj_temp=obj["results"][0]["daily"][i];
    weather.day_weather=obj_temp["text_day"].as<String>();
    weather.night_weather=obj_temp["text_night"].as<String>();
    weather.max_temperature=obj_temp["high"].as<String>();
    weather.min_temperature=obj_temp["low"].as<String>();
    weather.wind_speed=obj_temp["wind_scale"].as<String>();
  }
  
  return weather;
}

void set_mode(bool mode){//设备模式设定
  static bool work_mode=true;
  static bool sleep_mode=true;
  if(mode)
  {
    if(work_mode)//配置正常工作模式
    {
      digitalWrite(SCREEN_BACKGROUND_LIGHT,HIGH);//打开屏幕背光
      u8g2.sleepOff();
      tm1637.onMode();
      bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
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
      digitalWrite(SCREEN_BACKGROUND_LIGHT,LOW);//关闭屏幕背光
      tm1637.offMode();//关闭数码
      u8g2.sleepOn();//关闭屏幕
      bmp.setSampling(bmp.MODE_SLEEP);
      sleep_mode=false;
      work_mode=true;
    }
  }

}

void digital_tube_display(){//数码管显示
  tm1637.refresh();
  tm1637.switchColon();
  //显示时间
  tm1637.display(date.hour+date.minte);  
}

void data_display(){//数据显示
  u8g2.setFont(u8g2_font_minicute_tr);
  //屏幕显示数据
  u8g2.clearBuffer();
  //设置边框
  u8g2.drawLine(0,0,0,63);
  u8g2.drawLine(127,0,127,63);
  u8g2.drawLine(63,0,63,63);
  u8g2.drawLine(0,21,63,21);
  u8g2.drawLine(0,50,63,50);
  //设置上下动态边框
  static uint8_t dynamic_line_ordinate=0;//动态框横坐标
  u8g2.drawLine(0,63,dynamic_line_ordinate,63);
  u8g2.drawLine(127-dynamic_line_ordinate,63,127,63);
  u8g2.drawLine(0,0,dynamic_line_ordinate,0);
  u8g2.drawLine(127-dynamic_line_ordinate,0,127,0);
  ++dynamic_line_ordinate;
  if(dynamic_line_ordinate>64) dynamic_line_ordinate=0;

  //显示日期
  u8g2.drawStr(2,2,(date.year+"-"+date.month+"-"+date.day).c_str());
  //显示星期
  u8g2.drawStr(2,10,date.week.c_str());
  //显示温湿度等传感器数据
  u8g2.drawStr(1,22,("TEMP:"+String(temprature)).c_str());
  u8g2.drawXBMP(54,22,8,8,temperature_bmp);
  u8g2.drawStr(1,31,("HUMI:"+String(humidity)).c_str());
  u8g2.drawXBMP(54,31,8,8,percent_bmp);
  if (pressure >= 101325) u8g2.drawStr(1,40,("HIGH:"+String(pressure- 101325)).c_str());
  else u8g2.drawStr(1,40,("LOW:"+String(101325-pressure)).c_str());
  u8g2.drawXBMP(54,40,8,8,pressure_bmp);
  //显示ip地址
  u8g2.setDrawColor(2);
  u8g2.drawStr(2,52,ip_address.c_str());
  u8g2.drawBox(2,52,60,10);
  u8g2.setDrawColor(1);
  //显示天气信息
  WEATHER temp_weather;
  String temp_date;
  if(weather_switch_flag)
  {
    temp_weather=weather[0];
    temp_date="今天";
  }
  else{
    temp_weather=weather[1];
    temp_date="明天";
  }

  u8g2.setFont(u8g2_font_wqy12_t_gb2312a);
  u8g2.drawUTF8(65,1,("-- "+temp_date+" --").c_str());
  u8g2.drawUTF8(65,14,("日:"+temp_weather.day_weather).c_str());
  u8g2.drawUTF8(65,26,("夜:"+temp_weather.night_weather).c_str());
  u8g2.drawUTF8(65,38,("温度:"+temp_weather.min_temperature+".."+temp_weather.max_temperature).c_str());
  u8g2.drawUTF8(65,50,("风速:"+temp_weather.wind_speed+"级").c_str());

  u8g2.sendBuffer();
}

void data_display_disconncted()
{
  
  //屏幕显示数据
  u8g2.clearBuffer();
  //设置边框
  u8g2.drawLine(0,0,0,63);
  u8g2.drawLine(127,0,127,63);
  //u8g2.drawLine(63,0,63,63);
  //u8g2.drawLine(0,21,63,21);
  //u8g2.drawLine(0,50,63,50);
  //设置上下动态边框
  static uint8_t dynamic_line_ordinate=0;//动态框横坐标
  u8g2.drawLine(0,63,dynamic_line_ordinate,63);
  u8g2.drawLine(127-dynamic_line_ordinate,63,127,63);
  u8g2.drawLine(0,0,dynamic_line_ordinate,0);
  u8g2.drawLine(127-dynamic_line_ordinate,0,127,0);
  ++dynamic_line_ordinate;
  if(dynamic_line_ordinate>64) dynamic_line_ordinate=0;

  static uint8_t dynamic_line_ordinate_2=0;//动态框横坐标
  ++dynamic_line_ordinate_2;
  if(dynamic_line_ordinate_2>126) dynamic_line_ordinate_2=0;
  u8g2.drawBox(127-dynamic_line_ordinate_2,15,dynamic_line_ordinate_2,3);
  u8g2.drawBox(1,31,dynamic_line_ordinate_2,3);
  u8g2.drawBox(127-dynamic_line_ordinate_2,48,dynamic_line_ordinate_2,3);



  //显示温湿度等传感器数据
  u8g2.setFont(u8g2_font_wqy12_t_gb2312a);
  u8g2.drawUTF8(30,2,("温度:"+String(temprature)).c_str());
  u8g2.drawXBMP(93,4,8,8,temperature_bmp);

  u8g2.drawUTF8(30,18,("湿度:"+String(humidity)).c_str());
  u8g2.drawXBMP(93,20,8,8,percent_bmp);

  if (pressure >= 101325) u8g2.drawUTF8(25,34,("气压:↑"+String(pressure- 101325)).c_str());
  else u8g2.drawUTF8(25,34,("气压:↓"+String(101325-pressure)).c_str());
  u8g2.drawXBMP(94,37,8,8,pressure_bmp);


  u8g2.setFont(u8g2_font_minicute_tr);
  //显示ip地址
  u8g2.setDrawColor(2);
  u8g2.drawStr(5,52,"NETWORK DISCONNECTED");
  u8g2.drawBox(2,52,124,10);
  u8g2.setDrawColor(1);
  //显示天气信息
  u8g2.sendBuffer();
}

void computer_data_display(){//电脑数据显示
  u8g2.setFont(u8g2_font_minicute_tr);
  u8g2.clearBuffer();
  u8g2.drawStr(0,0,("CPU TEMP--------"+compute_data.CPU_Temp).c_str());
  u8g2.drawXBMP(110,0,8,8,temperature_bmp);
  u8g2.drawStr(0,8,("GPU TEMP--------"+compute_data.GPU_Temp).c_str());
  u8g2.drawXBMP(110,8,8,8,temperature_bmp);
  u8g2.drawStr(0,16,("BOARD TEMP-----"+compute_data.Mother_board_Temp).c_str());
  u8g2.drawXBMP(110,16,8,8,temperature_bmp);
  u8g2.drawStr(0,24,("CPU USE---------"+compute_data.CPU_Utilization).c_str());
  u8g2.drawXBMP(104,24,8,8,percent_bmp);
  u8g2.drawStr(0,32,("GPU USE---------"+compute_data.GPU_Utilization).c_str());
  u8g2.drawXBMP(106,32,8,8,percent_bmp);
  u8g2.drawStr(0,40,("MEMORY USE-----"+compute_data.Memory_Utilization).c_str());
  u8g2.drawXBMP(108,40,8,8,percent_bmp);
  u8g2.drawStr(0,48,("CPU FAN---------"+compute_data.CPU_Fan+"RPM").c_str());
  u8g2.drawStr(0,56,("GPU FAN---------"+compute_data.GPU_Fan+"RPM").c_str());
  u8g2.sendBuffer();
  
}

void notFound(AsyncWebServerRequest *request){
  String weather_api_flag;
  if(device_config.weather_api_flag.equals("gd"))weather_api_flag="高德";
  else if(device_config.weather_api_flag.equals("xz"))weather_api_flag="心知";
  String home_page =R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width,initial-scale=1.0,user-scalable=0">
    <title>WIFI_SETTING</title>
    <style>
      .div1{
        position: absolute;
        top: 50%;
        left: 50%;
        transform: translate(-50%,-50%);
          display: table-cell;
        /*垂直居中 */
        vertical-align: middle;
        /*水平居中*/
        text-align: center;
      }
      .submit_div{
        background-color: blue;
        color: white;
        border-radius: 8px;
        width: 300px;
        height: 35px;
      }
      .input_div
      {
        border: 1px solid #0000FF;
        border-radius: 20px;
        width: 300px;
        height: 35px;
        font-size: 20px;
      }
      .input_div_2
      {
        border: 1px solid #0000FF;
        border-radius: 20px;
        width: 145px;
        height: 35px;
        font-size: 20px;
      }
    </style>
  </head>
  <body>
    <div class="div1">
      <h1>信息设置</h1>
      <form name="wifiset" onsubmit="return validateForm()" action="/setConfig" method="post" target="myframe">
        <br /><br /><br /><br /><br />
        <label for="">WiFi信息设置</label>
        <br />)rawliteral"+wifi_list+
        R"rawliteral(
        <br />
        <input name="wifiPassword" type="password" placeholder="WIFI密码" class="input_div">
        <br /><br />
        <label for="">休眠时间设置</label>
        <br />
        <input name="startSleepTime" type="text" placeholder="开始休眠时间" class="input_div_2">
        <input name="endSleepTime" type="text" placeholder="停止休眠时间" class="input_div_2">
        <br /><br />
        <label for="">天气设置</label><br />
		<input type="radio" name="weatherApiFlag" value="gd" checked='checked'>高德
		<input type="radio" name="weatherApiFlag" value="xz" >心知
        <br />
		<input name="cityCode" type="text" placeholder="城市代码" class="input_div">
		<br />
		<label for="">城市代码：高德为身份证前6位,心知为城市拼音</label><br />
		<br />
		<input name="weatherKeyGd" type="text" placeholder="高德API_KEY" class="input_div">
		<br />
		<input name="weatherKeyXz" type="text" placeholder="心知API_KEY" class="input_div">
		<br />
        <input type='submit' value='确认' class="submit_div">
      </form>
        <h3>当前连接WiFi：)rawliteral"+device_config.wifi_name+
        R"rawliteral(</h3>
        <h3>当前设备IP：)rawliteral"+ip_address+
        R"rawliteral(</h3>
        <h3>开始休眠时间：)rawliteral"+device_config.start_sleep_time+
        R"rawliteral(</h3>
        <h3>停止休眠时间：)rawliteral"+device_config.end_sleep_time+
        R"rawliteral(</h3>
        <h3>当前使用：)rawliteral"+weather_api_flag+
        R"rawliteral(</h3>
        <h3>当前城市代码：)rawliteral"+device_config.city_code+
        R"rawliteral(</h3>
    </div>
  </body>
  <script type="text/javascript">
  </script>
  )rawliteral";

  request->send(200, "text/html", home_page);
}

void get_computer_data(AsyncWebServerRequest *request){//电脑数据传输
  if(request->hasParam("CPUUtilization",true))
  {
    uint8_t paramNumber=request->params();
    AsyncWebParameter* param;
    // String param_name;
    String param_value;
    for(int i=0;i<paramNumber;i++)
    {
      param = request->getParam(i);
      // param_name=param->name();
      param_value=param->value();
      
      switch (i){
        case 0:
          compute_data.CPU_Utilization=param_value;
          break;
        case 1:
          compute_data.Memory_Utilization=param_value;
          break;
        case 2:
          compute_data.GPU_Utilization=param_value;
          break;
        case 3:
          compute_data.Mother_board_Temp=param_value;
          break;
        case 4:
          compute_data.CPU_Fan=param_value;
          break;
        case 5:
          compute_data.CPU_Temp=param_value;
          break;
        case 6:
          compute_data.GPU_Temp=param_value;
          break;
        case 7:
          compute_data.GPU_Fan=param_value;
          break;
      }
      // if(param_name.equals("CPUUtilization"))compute_data.CPU_Utilization=param_value;
      // else if(param_name.equals("MemoryUtilization"))compute_data.Memory_Utilization=param_value;
      // else if(param_name.equals("GPUUtilization"))compute_data.GPU_Utilization=param_value;
      // else if(param_name.equals("Motherboard"))compute_data.Mother_board_Temp=param_value;
      // else if(param_name.equals("CPU")) compute_data.CPU_Fan=param_value;
      // else if(param_name.equals("CPUDiode"))compute_data.CPU_Temp=param_value;
      // else if(param_name.equals("GPU"))compute_data.GPU_Temp=param_value;
      // else if(param_name.equals("GPUDiode"))compute_data.GPU_Fan=param_value;
    }
    if(!compute_data_flag)compute_data_flag=true;
    request->send(200);
  }
}

void set_config(AsyncWebServerRequest *request){//配置信息网页
  if(request->hasParam("wifiName",true)){
    uint8_t param_number=request->params();
    AsyncWebParameter* param;
    String param_name;
    String param_value;

    String wifi_name_str;
    String wifi_password_str;
    String start_sleep_time_str;
    String end_sleep_time_str;
    String city_code_str;
    String weather_api_flag_str;
    String weather_key_gd_str;
    String weather_key_xz_str;

    for(int i=0;i<param_number;i++)
    {
      param=request->getParam(i);
      param_name=param->name();
      param_value=param->value();
      
      if(param_name.equals("wifiName"))wifi_name_str=param_value;
      else if(param_name.equals("wifiPassword"))wifi_password_str=param_value;
      else if(param_name.equals("startSleepTime"))start_sleep_time_str=param_value;
      else if(param_name.equals("endSleepTime"))end_sleep_time_str=param_value;
      else if(param_name.equals("cityCode"))city_code_str=param_value;
      else if(param_name.equals("weatherApiFlag"))weather_api_flag_str=param_value;
      else if(param_name.equals("weatherKeyGd"))weather_key_gd_str=param_value;
      else if(param_name.equals("weatherKeyXz"))weather_key_xz_str=param_value;
    }
  
    CONFIG config=get_config(read_config_txt("wifi_config"),read_config_txt("other_config"));//之前的配置
    
    if(!wifi_name_str.equals(""))config.wifi_name=wifi_name_str;
    if(!wifi_password_str.equals(""))config.wifi_password=wifi_password_str;
    if(!start_sleep_time_str.equals(""))config.start_sleep_time=start_sleep_time_str.toInt();
    if(!end_sleep_time_str.equals(""))config.end_sleep_time=end_sleep_time_str.toInt();
    if(!city_code_str.equals(""))config.city_code=city_code_str;
    if(!weather_api_flag_str.equals(""))config.weather_api_flag=weather_api_flag_str;
    if(!weather_key_gd_str.equals(""))config.weather_key_gd=weather_key_gd_str;
    if(!weather_key_xz_str.equals(""))config.weather_key_xz=weather_key_xz_str;
    write_config_txt(config);//wifi连接成功,写入配置文件
    request->send(200, "text/plain", "success!");
    ESP.restart();//重启esp
  } 
}

void sever_start(){//开启服务器
  server.onNotFound(notFound);
  server.on("/uploadComputeData",HTTP_POST,get_computer_data);
  server.on("/setConfig",HTTP_POST,set_config);
  server.on("/ipAddress",HTTP_GET,[](AsyncWebServerRequest *request){request->send(200,"text/plain","success");});
  server.begin();
}

void create_ap(){//创建热点
  IPAddress apIP(8,8,4,4);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("DESKTOP_CLOCK");
} 

uint8_t wifi_connect(String wifi_name,String wifi_password){//连接wifi
  //静态iP信息
  WiFi.mode(WIFI_STA);
  //WiFi.config(staticIP, gateway, subnet, dns, dns);
  WiFi.setAutoReconnect(true);
  WiFi.begin(wifi_name, wifi_password);
  uint8_t disconnected=0;
  while (true)
  {
    //ESP.wdtFeed();//feed watch door dog
    uint8_t flag=WiFi.status();
    if(flag==WL_CONNECTED)return WL_CONNECTED;
    else ++disconnected;
    if(disconnected>=20)return WL_DISCONNECTED;//10秒后依旧未连接
    u8g2.clearBuffer();
    u8g2.drawStr(0,0,"WIFI CONNECTING.......");
    u8g2.sendBuffer();
    delay(500);
  }
}

void wifi_station(){
   if(network_station_count>=10)
   {
    ip_address="DISCONNECT";
      // WiFi.mode(WIFI_STA);
      // WiFi.setAutoReconnect(true);
      // WiFi.begin(device_config.wifi_name,device_config.wifi_password);
      WiFi.reconnect();
      uint8_t flag=0;
      while(!WiFi.isConnected())
      {
        ++flag;
        if(flag>20) 
        {
          network_station=false;
          break;
        }

        delay(50);
      }
    }
   else{
    ip_address=WiFi.localIP().toString();//ip地址
   }
}

void dns_server_start(){//开启dns服务器
  IPAddress apIP(8,8,4,4);
  const byte DNS_PORT = 53;
  DNSServer dnsServer;
  dnsServer.start(DNS_PORT, "*", apIP);
  digitalWrite(SCREEN_BACKGROUND_LIGHT,HIGH);//打开背光
  u8g2.clearBuffer();
  u8g2.drawStr(0,0,"Wait for configuration");
  u8g2.drawStr(0,20,"Connect to the ap:");
  u8g2.drawStr(0,30,"DESKTOP_CLOCK");
  u8g2.sendBuffer();
  while (true) dnsServer.processNextRequest();
}

void write_config_txt(CONFIG config){ //写入配置信息到config.txt
  DynamicJsonDocument wifi_doc(1024);
  wifi_doc["wifi_name"] = config.wifi_name;
  wifi_doc["wifi_password"]= config.wifi_password;
  String wifi_data;
  serializeJson(wifi_doc, wifi_data);
  writeFile(LittleFS,"/wifi_config.txt", wifi_data.c_str());//创建一个新的文件并写入

  DynamicJsonDocument other_doc(1024);
  other_doc["start_sleep_time"]=config.start_sleep_time;
  other_doc["end_sleep_time"]=config.end_sleep_time;
  other_doc["city_code"]=config.city_code;
  other_doc["weather_api_flag"]=config.weather_api_flag;
  other_doc["weather_key_gd"]=config.weather_key_gd;
  other_doc["weather_key_xz"]=config.weather_key_xz;
  String other_data;
  serializeJson(other_doc, other_data);
  writeFile(LittleFS,"/other_config.txt", other_data.c_str());//创建一个新的文件并写入
}

String read_config_txt(String file_name){//读取配置信息
  String real_name="/"+file_name+".txt";
    return readFile(LittleFS,real_name.c_str());
}

CONFIG get_config(String wifi_config_str,String other_config_str){//解析配置信息返回配置结构体
  DynamicJsonDocument wifi_doc(1024);
  deserializeJson(wifi_doc, wifi_config_str);

  DynamicJsonDocument other_doc(1024);
  deserializeJson(other_doc, other_config_str);

  CONFIG result;
  result.wifi_name=wifi_doc["wifi_name"].as<String>();
  result.wifi_password=wifi_doc["wifi_password"].as<String>();
  result.start_sleep_time=other_doc["start_sleep_time"];
  result.end_sleep_time=other_doc["end_sleep_time"];
  result.city_code=other_doc["city_code"].as<String>();
  result.weather_api_flag=other_doc["weather_api_flag"].as<String>();
  result.weather_key_gd=other_doc["weather_key_gd"].as<String>();
  result.weather_key_xz=other_doc["weather_key_xz"].as<String>();
  return result;
}

String* scan_wifi()//扫描wifi
{
    int wifi_number = WiFi.scanNetworks();
    if (wifi_number == 0) {
        return new String{"null"};
    } else {
      String* result=new String[wifi_number+1];
      result[0]=String(wifi_number);
      for (int i = 0; i < wifi_number; ++i) {
        result[i+1]=WiFi.SSID(i);
      } 
      WiFi.scanDelete();
      return result;
    }
}

String select_scan_wifi(String* wifi_array) 
{
  String* wifi_list=wifi_array;
  uint8_t number=wifi_list[0].toInt();
  String result="<select name=\"wifiName\" class=\"input_div\">";
  for(int i=0;i<number;i++)
  {
    result+=" <option value =\""+wifi_list[i+1]+"\">"+wifi_list[i+1]+"</option>";
  }
  result+="</select>";
  return result;
}

void ota_init(){
  ArduinoOTA.setHostname("DESKTOP_DATA_STATION_OTA");
  ArduinoOTA.onStart([](){});
  ArduinoOTA.begin();
}