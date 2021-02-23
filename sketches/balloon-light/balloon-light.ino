
#include "driver/adc.h" 
#include "esp_adc_cal.h"
#include <WiFi.h>
#include <HTTPClient.h>


#define usb_enable 23

esp_adc_cal_characteristics_t adcChar;
TaskHandle_t th;
hw_timer_t * timer = NULL;//タイマー初期化
WiFiServer server(80);

struct Level{
  const uint8_t PIN;
};
Level led1 = {25};
Level led2 = {26};
Level led3 = {27};
Level led4 = {14};
Level led5 = {12};

struct Button{
  const uint8_t PIN;
  bool pressed;
};
Button sw1 = {18, false};
Button sw2 = {17, false};
Button sw3 = {16, false};
Button sw4 = {4, false};

struct PwmOut{
  const uint8_t PIN;
  const uint8_t ch;
  const uint8_t duty_bit;
  uint8_t duty;
  uint8_t frq;
};
PwmOut spk = {A14, 1, 13, 50, 5000};
PwmOut fan = {A13, 2, 8, 0, 12000};
PwmOut light = {A5, 4, 8, 0, 200};



/////////////////グローバル変数/////////////
int led_count = 0;
int light_level = 1;
int fan_level = 1;
bool usb_out = false;
bool power = false;
bool charge_fin_flag = false;

//const char* ssid     = "EC56235BAAEC-2G";
//const char* password = "55401106904901";
const char* ssid = "Airport211_s";
const char* pass = "himitsunopass";
const String host = "https://script.google.com/macros/s/AKfycbxpsnkoJ7iYkUW2Kf8yTPlFuHGJ59_vjR7x9YnaWPp_z_DkMCg/exec";



/////////////////ユーザー定義関数///////////

//レベルメータの点灯形式//
void led_level(int i){
  switch(i){
    case 0:
      digitalWrite(led1.PIN,LOW);
      digitalWrite(led2.PIN,LOW);
      digitalWrite(led3.PIN,LOW);
      digitalWrite(led4.PIN,LOW);
      digitalWrite(led5.PIN,LOW);
      break;
    case 1:
      digitalWrite(led1.PIN,HIGH);
      break;
    case 2:
      digitalWrite(led1.PIN,HIGH);
      digitalWrite(led2.PIN,HIGH);
      break;
    case 3:
      digitalWrite(led1.PIN,HIGH);
      digitalWrite(led2.PIN,HIGH);
      digitalWrite(led3.PIN,HIGH); 
      break;
    case 4:
      digitalWrite(led1.PIN,HIGH);
      digitalWrite(led2.PIN,HIGH);
      digitalWrite(led3.PIN,HIGH);
      digitalWrite(led4.PIN,HIGH);
      break;
    case 5:
      digitalWrite(led1.PIN,HIGH);
      digitalWrite(led2.PIN,HIGH);
      digitalWrite(led3.PIN,HIGH);
      digitalWrite(led4.PIN,HIGH);
      digitalWrite(led5.PIN,HIGH);
      break;
    default:
      break;
  }
}

//充電中のレベルメータ表示//
void charging_level(int i){
  if(i > led_count){
    led_level(led_count + 1);
    led_count++;
  }else{
    led_level(0);
    led_count = 0;
  }
}

//ブザー音//
void beep(int i){
  int frq=0;
  switch(i){
    case 1:
      frq = 262;
      break;
    case 2:
      frq = 294;
      break;
    case 3:
      frq = 330;
      break;
    case 4:
      frq = 349;
      break;
    case 5:
      frq = 392;
      break;                
    default:
      frq = 3075;
      break;
  }
 
  ledcWriteTone(spk.ch, frq) ;
  delay(100);
  ledcWriteTone(spk.ch, 0) ;
}

//スイッチ検出割り込み関数//
void sw_intr(void* arg){
  Button* s = static_cast<Button*>(arg);
  s->pressed = true;
}

void timer_inter(){
  BaseType_t taskWoken;
  xTaskNotifyFromISR(th, 0, eIncrement, &taskWoken);
}

//電圧監視//
void voltage(){
  uint32_t battery_v;
  uint32_t charging_v;
  float val;
  const float vol_conv = 0.0046435;
  esp_adc_cal_get_voltage(ADC_CHANNEL_6, &adcChar, &battery_v);
  esp_adc_cal_get_voltage(ADC_CHANNEL_0, &adcChar, &charging_v); 
  val = battery_v*vol_conv;
  Serial.println("Voltage: " + String(val) + "[V]"+ "," + String(charging_v));

  if(charging_v < 2700){
    ///バッテリー駆動
    charge_fin_flag = false;
    if(val > 12.5){
      led_level(5);
    }else if(val > 12){
      led_level(4);
    }else if(val > 11.5){
      led_level(3);
    }else if(val > 11){
      led_level(2);
    }else{
      led_level(1);
    }
  }else if(!charge_fin_flag){
    ///充電駆動
    if(val > 12.7){
      charge_fin_flag = true;
    }else if(val > 12.5){
      charging_level(5);
    }else if(val > 12){
      charging_level(4);
    }else if(val > 11.5){
      charging_level(3);
    }else if(val > 11){
      charging_level(2);
    }else{
      charging_level(1);
    }
  }else{
    led_level(5);
  }
}

//Wi-Fi接続//
void wifi_connect(){
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void responseHTTP (WiFiClient client) {
  boolean currentLineIsBlank = true;
  float flow = fan_level * 51;
  float brightness = light_level * 51;
  
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      Serial.write(c);
      if (c == '\n' && currentLineIsBlank) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html");
        client.println("Connection: close");
        client.println();
        client.println("<!DOCTYPE HTML>");
        client.println("<html>");
        client.println("<h1>ESP32 is running</h1>");
        client.println("power: ");
        if(power){
          client.print("ON");
        }else{
          client.print("OFF");
        }
        client.print("<p>brightness: "); client.print(brightness); client.print(" *C, ");
        client.print("flow: "); client.print(flow); client.print(" %</p>");
        client.println("</html>");
        break;
      }
      if (c == '\n') {
        currentLineIsBlank = true;
      } else if (c != '\r') {
        currentLineIsBlank = false;
      }
    }
  }
}

void record() {
  HTTPClient http;
  float brightness = light_level * 51;
  float flow = fan_level * 51;
  http.begin(host + "?brightness=" + brightness + "&flow=" + flow);
  int status_code = http.GET();
  Serial.printf("get request: status code = %d\r\n", status_code);
  http.end();
}


//////////////core0タスク/////////////////
void task1(void *pvParameters){
  while(1){
    delay(500);
    
    ////lightレベル変更スイッチ
    if(sw1.pressed){
      light_level++;
      if(light_level > 5)light_level = 1;
      beep(light_level);
      light.duty = light_level * 51;
      
      sw1.pressed = false;
    }
    ////fanレベル変更スイッチ
    if(sw2.pressed){
      fan_level++;
      if(fan_level > 5)fan_level = 1;
      beep(fan_level);
      fan.duty = fan_level * 51;
      
      sw2.pressed = false;
    }
    ////powerオンオフスイッチ
    if(sw3.pressed){
      power = !power;
      beep(6);
      
      sw3.pressed = false;
    }
    ////usbオンオフスイッチ
    if(sw4.pressed){
      usb_out = !usb_out;
      
      sw4.pressed = false;
    }  
  
    voltage();///電圧計測、レベル表示
  
    ///USB出力オンオフ
    if(!usb_out){
      digitalWrite(usb_enable, LOW);
    }else{
      digitalWrite(usb_enable, HIGH);
    }
  
    ///powerオンオフ
    if(power){
      ledcWrite(light.ch, light.duty);
      ledcWrite(fan.ch, fan.duty);
    }else{
      ledcWrite(light.ch, 0);
      ledcWrite(fan.ch, 0);
    }
  }
}


/////////////////setup///////////////////
void setup() {
  Serial.begin(115200);
  wifi_connect();
  
  xTaskCreatePinnedToCore(
           task1,
           "task",
           8192,
           NULL,
           1,
           &th,
           0
  );
 
  adc_power_on();
  adc_gpio_init(ADC_UNIT_1, ADC_CHANNEL_6);
  adc_gpio_init(ADC_UNIT_1, ADC_CHANNEL_0);
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
  adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adcChar);
  
  pinMode(led1.PIN, OUTPUT);
  pinMode(led2.PIN, OUTPUT);
  pinMode(led3.PIN, OUTPUT);
  pinMode(led4.PIN, OUTPUT);
  pinMode(led5.PIN, OUTPUT);
  pinMode(sw1.PIN, INPUT_PULLUP);
  pinMode(sw2.PIN, INPUT_PULLUP);
  pinMode(sw3.PIN, INPUT_PULLUP);
  pinMode(sw4.PIN, INPUT_PULLUP);
  pinMode(usb_enable, OUTPUT);
  
  pinMode(fan.PIN, OUTPUT);
  pinMode(light.PIN, OUTPUT);
  pinMode(spk.PIN, OUTPUT);

  attachInterruptArg( sw1.PIN, sw_intr, &sw1, FALLING);
  attachInterruptArg( sw2.PIN, sw_intr, &sw2, FALLING);
  attachInterruptArg( sw3.PIN, sw_intr, &sw3, FALLING);
  attachInterruptArg( sw4.PIN, sw_intr, &sw4, FALLING);
  
  timer = timerBegin(0, 80, true); // 1us
  timerAttachInterrupt(timer, &timer_inter, true);
  timerAlarmWrite(timer, 1e7, true);
  timerAlarmEnable(timer);
  
  ledcSetup(spk.ch, spk.frq, spk.duty_bit);  
  ledcSetup(fan.ch, fan.frq, fan.duty_bit); 
  ledcSetup(light.ch, light.frq, light.duty_bit); 
  ledcAttachPin(spk.PIN, spk.ch);
  ledcAttachPin(fan.PIN, fan.ch);
  ledcAttachPin(light.PIN, light.ch);
  
  digitalWrite(usb_enable, LOW);
}




/////////////////メイン///////////////////
void loop() {
  WiFiClient client = server.available();
  if (client) {
    Serial.println("client connected");
    responseHTTP(client);
    record();
    delay(1);
    client.stop();
    Serial.println("client disconnected");
  }
  delay(2000);
}
