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
#include <little_fs_esp8266/little_fs.h>
#include <ArduinoOTA.h>

#define DHTPIN  5
#define DHTTYPE DHT11 
#define BMP_CS  4
#define BACKGRPUND_LIGHT 15

DHT dht(DHTPIN, DHTTYPE);//温湿度计模块
Adafruit_BMP280 bmp(BMP_CS);//气压计模块
TM1637 tm1637(0, 16);//数码管模块
U8G2_ST7567_JLX12864_F_4W_HW_SPI u8g2(U8G2_MIRROR, /* cs=*/ 1, /* dc=*/ 3, /* reset=*/ 2);//屏幕模块

WiFiClient wifi_client;
HTTPClient time_http_client;
HTTPClient weather_http_client;

AsyncWebServer server(80);

String ip_address;//连接后获得的IP地址

volatile float temprature;//温度
volatile float humidity;//湿度
volatile uint32_t pressure;//大气压

volatile bool compute_data_flag=false;//获取计算机信息的标志
volatile bool display_mode_flag=false;//显示数据类型的标志
volatile bool weather_switch_flag=true;//转换今天和明天的天气标志

//无阻塞延时的暂存时间变量
volatile uint64_t currentMillis;
volatile unsigned long previousMillis_sensor = 0;
volatile unsigned long previousMillis_time = 0;
volatile unsigned long previousMillis_colon = 0;
volatile unsigned long previousMillis_weather = 0;
volatile unsigned long previousMillis_refresh_monitor= 0;
volatile unsigned long previousMillis_switch_display= 0;

static const unsigned char PROGMEM temperature_bmp[] = {0x07,0x05,0xF7,0x08,0x08,0x08,0x08,0xF0};//摄氏度符号
static const unsigned char PROGMEM percent_bmp[] = {0x98,0x58,0x20,0x10,0x08,0x04,0x1A,0x19};//百分比符号
static const unsigned char PROGMEM pressure_bmp[] = {0x00,0x0F,0x09,0x79,0x5F,0x51,0xF1,0x01};//帕符号

typedef struct{//天气数据结构体
  String day_weather;
  String night_weather;
  String max_temperature;
  String min_temperature;
  String wind_speed;
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
  uint16_t CPU_Utilization;
  uint16_t Memory_Utilization;
  uint16_t GPU_Utilization;
  uint16_t Mother_board_Temp;
  uint16_t CPU_Temp;
  uint16_t GPU_Temp;
  uint16_t GPU_Fan;
  uint16_t CPU_Fan;
}COMPUTE_DATA;

typedef struct{//配置信息结构体
  String wifi_name;
  String wifi_password;
  uint8_t start_sleep_time;
  uint8_t end_sleep_time;
  String  city_code;
}CONFIG;

WEATHER weather[2];
DATE date;
COMPUTE_DATA compute_data;
CONFIG device_config;

const char home_page[] PROGMEM =R"rawliteral(
<!DOCTYPE html>
<html>
<head>
	<meta charset="utf-8" />
	<meta name="viewport" content="width=device-width,initial-scale=1.0,user-scalable=0">
	<title>WIFI_SETTING</title>
	<style>
		.div1{
			position: absolute;
			 top: 30%;
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
			<label for="">WiFi信息设置</label>
			<br />
			<input name="wifiName" type="text" placeholder="WIFI账号" class="input_div">
			<br />
			<input name="wifiPassword" type="password" placeholder="WIFI密码" class="input_div">
			<br /><br />
			<label for="">休眠时间设置</label>
			<br />
			<input name="startSleepTime" type="text" placeholder="开始休眠时间" class="input_div_2">
			<input name="endSleepTime" type="text" placeholder="停止休眠时间" class="input_div_2">
			<br /><br />
			<label for="">天气设置</label>
			<br />
			<input name="cityCode" type="text" placeholder="城市代码(身份证前6位)" class="input_div">
			<br /><br />
			<input type='submit' value='确认' class="submit_div">
		</form>
	</div>
</body>
<script type="text/javascript">
</script>
)rawliteral";

void u8g2_prepare();
void device_init();
String weekDay(uint16_t year,uint8_t month,uint8_t day);
WEATHER weather_data_parse(String weather_data,int i);
void get_date();
void get_sensor_data();
void get_weather();
void set_mode(bool mode);
void digital_tube_display();
void sensor_data_display();
void compute_data_display();
void create_ap();
uint8_t wifi_connect(String wifi_name,String wifi_password);
void sever_start();
void dns_server_start();
void write_config_txt(CONFIG config);
String read_config_txt();
CONFIG get_config(String config_str);
void notFound(AsyncWebServerRequest *request);
void get_compute_data(AsyncWebServerRequest *request);
void set_config(AsyncWebServerRequest *request);
void ota_init();
void wifi_station();

uint8_t network_station=0;//网络状态

void setup(){
  device_init();
  // LittleFS.begin();
  // Serial.begin(115200);
  // Serial.println(read_config_txt());
   
  String config_str=read_config_txt();
  if(String("nullptr").equals(config_str)){//读取不到配置文件
    create_ap();//开启热点
    sever_start();//开启服务器
    dns_server_start();//开启dns服务器
  }
  else{//读取到配置文件
    device_config=get_config(config_str);//获取配置信息
    uint8_t flag=wifi_connect(device_config.wifi_name,device_config.wifi_password);//连接wifi
    if(flag!=WL_CONNECTED){//连接失败
      deleteFile("/config.txt");
      ESP.restart();
    }
    
    ip_address=WiFi.localIP().toString();//ip地址

    sever_start();//开启服务器

    String time_url="http://quan.suning.com/getSysTime.do";// 苏宁授时网页
    String weather_url="http://restapi.amap.com/v3/weather/weatherInfo?city="+device_config.city_code+"&key=cf0df3bf85b9361ae9c661ad3af216a8&extensions=all";
    
    time_http_client.setTimeout(500);
    time_http_client.begin(wifi_client,time_url);
    weather_http_client.setTimeout(500);
    weather_http_client.begin(wifi_client,weather_url);

    get_date();//获取一次日期时间为下面的模式选择铺垫
    get_weather(); //获取一次天气
  }
}

void loop() {
  ArduinoOTA.handle();//ota轮询
  if(device_config.start_sleep_time<=date.hour.toInt()&&date.hour.toInt()<=device_config.end_sleep_time){//休眠
    set_mode(false);//休眠模式
    get_date();
    wifi_station();
    delay(5000);
  }
  else{//正常工作
    set_mode(true);//工作模式

    currentMillis = millis();

    if (currentMillis - previousMillis_sensor >= 500) {//传感器数据获取
      previousMillis_sensor = currentMillis;
      get_sensor_data();

      currentMillis = millis();
    }

    if (currentMillis - previousMillis_refresh_monitor >= 500) {//屏幕刷新
      previousMillis_refresh_monitor = currentMillis;
       
      if(display_mode_flag)compute_data_display();//电脑数据展示 
      else sensor_data_display();//传感器数据展示
      
      currentMillis = millis();
    }

    if (currentMillis - previousMillis_colon >= 800) {//数码管刷新
      previousMillis_colon = currentMillis;
      digital_tube_display();

      currentMillis = millis();
    }

    if (currentMillis - previousMillis_time >= 1500) {//日期和时间数据获取
      previousMillis_time = currentMillis;
      get_date();
      weather_switch_flag=!weather_switch_flag;//切换今天和明天的天气

      currentMillis = millis();
    }

    if(currentMillis - previousMillis_switch_display >= 5000){//每五秒钟切换一次显示模式（传感器数据或电脑数据
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

    if (currentMillis - previousMillis_weather >= 10000) {//天气数据获取
      previousMillis_weather = currentMillis;
      get_weather();
      
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
}

void device_init(){
  pinMode(BACKGRPUND_LIGHT,OUTPUT);

  dht.begin();
  bmp.begin();
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */

  tm1637.init();
  tm1637.setBrightness(5);
  u8g2_prepare();
  u8g2.begin();
  LittleFS.begin();
  ota_init();
}

String weekDay(uint16_t year,uint8_t month,uint8_t day){//通过年月日计算星期
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

WEATHER weather_data_parse(String weather_data,int i){//天气json数据解析
  DynamicJsonDocument weather_doc(1024);
  deserializeJson(weather_doc, weather_data);//反序列化json
  JsonObject obj = weather_doc.as<JsonObject>();
  WEATHER weather;
  JsonObject obj_temp=obj["forecasts"][0]["casts"][i];
  weather.day_weather=obj_temp["dayweather"].as<String>();
  weather.night_weather=obj_temp["nightweather"].as<String>();
  weather.max_temperature=obj_temp["daytemp"].as<String>();
  weather.min_temperature=obj_temp["nighttemp"].as<String>();
  weather.wind_speed=obj_temp["daypower"].as<String>();
  return weather;
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

void get_date(){//日期时间获取
  uint8_t httpCode = time_http_client.GET();
  if ((httpCode > 0)&&(httpCode == HTTP_CODE_OK) ) { 
    //{"sysTime2":"2021-12-07 21:07:31","sysTime1":"20211207210731"}
    DynamicJsonDocument date_doc(1024);
    deserializeJson(date_doc, time_http_client.getString());//反序列化json
    JsonObject obj = date_doc.as<JsonObject>();
    String date_str=obj["sysTime1"];
    if(!date_str.equals("null"))
    {
      date.year=date_str.substring(0,4);
      date.month=date_str.substring(4,6);
      date.day=date_str.substring(6,8);
      date.hour=date_str.substring(8,10);
      date.minte=date_str.substring(10,12);
      date.second=date_str.substring(12,14);
      date.week=weekDay(date.year.toInt(),date.month.toInt(),date.day.toInt());
    }
    if(network_station!=0)network_station=0;
  }
  else{
    ++network_station;
  }
}

void get_weather(){//天气数据获取
 uint8_t httpCode = weather_http_client.GET();
  if ((httpCode > 0)&&(httpCode == HTTP_CODE_OK) ) { 
    DynamicJsonDocument weather_doc(1024);
    String weather_data=weather_http_client.getString();
    weather[0]=weather_data_parse(weather_data,0);     
    weather[1]=weather_data_parse(weather_data,1);
  }
}

void set_mode(bool mode){//设备模式设定
  static bool work_mode=true;
  static bool sleep_mode=true;
  if(mode)
  {
    if(work_mode)//配置正常工作模式
    {
      digitalWrite(BACKGRPUND_LIGHT,HIGH);//屏幕背光设置
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
      digitalWrite(BACKGRPUND_LIGHT,LOW);//屏幕背光设置
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
 
void sensor_data_display(){//传感器数据显示
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
  u8g2.drawUTF8(65,38,String("温度:"+temp_weather.min_temperature+".."+temp_weather.max_temperature).c_str());
  u8g2.drawUTF8(65,50,("风速:"+temp_weather.wind_speed+"级").c_str());
  u8g2.sendBuffer();
  u8g2.setFont(u8g2_font_minicute_tr);
}

void compute_data_display(){//电脑数据显示
  u8g2.clearBuffer();
  u8g2.drawStr(0,0,("CPU TEMP--------"+String(compute_data.CPU_Temp)).c_str());
  u8g2.drawXBMP(110,0,8,8,temperature_bmp);
  u8g2.drawStr(0,8,("GPU TEMP--------"+String(compute_data.GPU_Temp)).c_str());
  u8g2.drawXBMP(110,8,8,8,temperature_bmp);
  u8g2.drawStr(0,16,("BOARD TEMP-----"+String(compute_data.Mother_board_Temp)).c_str());
  u8g2.drawXBMP(110,16,8,8,temperature_bmp);
  u8g2.drawStr(0,24,("CPU USE---------"+String(compute_data.CPU_Utilization)).c_str());
  u8g2.drawXBMP(104,24,8,8,percent_bmp);
  u8g2.drawStr(0,32,("GPU USE---------"+String(compute_data.GPU_Utilization)).c_str());
  u8g2.drawXBMP(106,32,8,8,percent_bmp);
  u8g2.drawStr(0,40,("MEMORY USE-----"+String(compute_data.Memory_Utilization)).c_str());
  u8g2.drawXBMP(108,40,8,8,percent_bmp);
  u8g2.drawStr(0,48,("CPU FNA---------"+String(compute_data.CPU_Fan)+"RPM").c_str());
  u8g2.drawStr(0,56,("GPU FAN---------"+String(compute_data.GPU_Fan)+"RPM").c_str());
  u8g2.sendBuffer();
  
}

void notFound(AsyncWebServerRequest *request){
  request->send_P(200, "text/html", home_page);
}

void get_compute_data(AsyncWebServerRequest *request){//电脑数据传输
  if(request->hasParam("CPUUtilization",true))
  {
    uint8_t paramNumber=request->params();
    AsyncWebParameter* param;
    // String param_name;
    uint16_t param_value;
    for(int i=0;i<paramNumber;i++)
    {
      param = request->getParam(i);
      // param_name=param->name();
      param_value=param->value().toInt();
      
      switch (i)
      {
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
    }
  
    CONFIG before_config=get_config(read_config_txt());//之前的配置

    CONFIG new_config;
    if(wifi_name_str.equals(""))new_config.wifi_name=before_config.wifi_name;
    else new_config.wifi_name=wifi_name_str;
    if(wifi_password_str.equals(""))new_config.wifi_password=before_config.wifi_password;
    else new_config.wifi_password=wifi_password_str;
    if(start_sleep_time_str.equals(""))new_config.start_sleep_time=before_config.start_sleep_time;
    else new_config.start_sleep_time=start_sleep_time_str.toInt();
    if(end_sleep_time_str.equals(""))new_config.end_sleep_time=before_config.end_sleep_time;
    else new_config.end_sleep_time=end_sleep_time_str.toInt();
    if(city_code_str.equals(""))new_config.city_code=before_config.city_code;
    else new_config.city_code=city_code_str;
    
    write_config_txt(new_config);//wifi连接成功,写入配置文件
    request->send(200, "text/plain", "success!");
    ESP.restart();//重启esp
  } 
}

void create_ap(){//创建热点
  IPAddress apIP(8,8,4,4);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("DESKTOP_DATA_STATION");
} 

uint8_t wifi_connect(String wifi_name,String wifi_password){//连接wifi
  //静态iP信息
  // IPAddress staticIP(192, 168, 0, 253);
  // IPAddress gateway(192, 168, 0, 1);
  // IPAddress subnet(255, 255, 255, 0);
  // IPAddress dns(192, 168, 0,1);

  WiFi.mode(WIFI_STA);
  //WiFi.config(staticIP, gateway, subnet, dns, dns);
  WiFi.setAutoReconnect(true);
  WiFi.begin(wifi_name, wifi_password);
  
  uint8_t disconnected=0;
  digitalWrite(BACKGRPUND_LIGHT,HIGH);//打开背光
  while (true)
  {
    ESP.wdtFeed();//feed watch door dog
    delay(500);
    uint8_t flag=WiFi.status();
    if(flag!=WL_CONNECTED)++disconnected;
    if(flag==WL_CONNECTED)return WL_CONNECTED;
    if(disconnected>=10)return WL_DISCONNECTED;//5秒后依旧未连接
    u8g2.clearBuffer();
    u8g2.drawStr(0,0,"WIFI CONNECTING.......");
    u8g2.sendBuffer();
  }
  digitalWrite(BACKGRPUND_LIGHT,LOW);//关闭背光 
}

void wifi_station(){
  if(!WiFi.isConnected())
  {
    ip_address="DISCONNECT";
    WiFi.reconnect();
  }
  else
  {
    ip_address=WiFi.localIP().toString();//ip地址
  }
   
  if(network_station>=5)//防止WiFi假死
  {
    network_station=0;
    WiFi.disconnect();
    delay(100);
    WiFi.begin(device_config.wifi_name,device_config.wifi_password);
    while(!WiFi.isConnected())
    {
      delay(200);
      ESP.wdtFeed();
    }
  }
}

void sever_start(){//开启服务器
  server.onNotFound(notFound);
  server.on("/uploadComputeData",HTTP_POST,get_compute_data);
  server.on("/setConfig",HTTP_POST,set_config);
  server.on("/ipLocation",HTTP_GET,[](AsyncWebServerRequest *request){request->send(200,"text/plain","success");});
  server.begin();
}

void dns_server_start(){//开启dns服务器
  IPAddress apIP(8,8,4,4);
  const byte DNS_PORT = 53;
  DNSServer dnsServer;
  dnsServer.start(DNS_PORT, "*", apIP);
  digitalWrite(BACKGRPUND_LIGHT,HIGH);//打开背光
  u8g2.clearBuffer();
  u8g2.drawStr(0,0,"WAIT CONFIGING.......");
  u8g2.sendBuffer();
  while (true) dnsServer.processNextRequest();
}

void write_config_txt(CONFIG config){ //写入配置信息到config.txt
  DynamicJsonDocument doc(1024);
  doc["wifi_name"] = config.wifi_name;
  doc["wifi_password"]= config.wifi_password;
  doc["start_sleep_time"]=config.start_sleep_time;
  doc["end_sleep_time"]=config.end_sleep_time;
  doc["city_code"]=config.city_code;
  String data;
  serializeJson(doc, data);
  writeFile("/config.txt", data.c_str());//创建一个新的文件并写入
}

String read_config_txt(){//读取配置信息
    return readFile("/config.txt");
}

CONFIG get_config(String config_str){//解析配置信息返回配置结构体
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, config_str);
  String wifi_name=doc["wifi_name"];
  String wifi_password=doc["wifi_password"];
  uint8_t start_sleep_time=doc["start_sleep_time"];
  uint8_t end_sleep_time=doc["end_sleep_time"];
  String city_code=doc["city_code"];
  CONFIG result;
  result.wifi_name=wifi_name;
  result.wifi_password=wifi_password;
  result.start_sleep_time=start_sleep_time;
  result.end_sleep_time=end_sleep_time;
  result.city_code=city_code;
  return result;
}

void ota_init(){
  ArduinoOTA.setHostname("DESKTOP_DATA_STATION_OTA");
  ArduinoOTA.onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";
    });
  ArduinoOTA.begin();
}