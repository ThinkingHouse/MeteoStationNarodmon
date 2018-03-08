/******************************************************************************
 *                             ЗАГОЛОВОЧНЫЕ ФАЙЛЫ
 ******************************************************************************/
 #define TINY_GSM_MODEM_SIM800
#include <SoftwareSerial.h>       //Библиотека для работы с Программным UART (к которому подключен SIM800L)
#include <OneWire.h>              //Библиотека для работы с линией OneWire
#include <DallasTemperature.h>    //Библиотека для работы с ds18b20
#include <Adafruit_SleepyDog.h>   //Библиотека для реализации спящего режима
#include <TinyGsmClient.h>        //Библиотека для реализации GSM клиента
#include <PubSubClient.h>         //Библиотека для работы со сторонними mqtt брокерами
/******************************************************************************
 *                             ДАННЫЕ
 ******************************************************************************/
//ПОДКЛЮЧЕНИЕ
#define GSM_RX        2    //Сюда подключается TX SIM800L
#define GSM_TX        3    //Сюда подключается RX SIM800L
#define GSM_RESET    12    //Сюда подключается RESET SIM800L
#define GSM_DTR       8    //Сюда подключается DTR SIM800L для перевода в спящий режим
#define ONE_WIRE_BUS  4    //Сюда подключается датчик ds18b20

#define TEMPERATURE_PRECISION 9

//Экземпляры классов
SoftwareSerial    GSMport(GSM_RX, GSM_TX);    // Экземпляр для работы с Программным UART (к которому подключен SIM800L) 
OneWire           oneWire(ONE_WIRE_BUS);      // Экземпляр для работы с OneWire
DallasTemperature sensors(&oneWire);          // Экземпляр для работы с датчиком ds18b20

TinyGsm modem(GSMport);                       //Экземпляры классов для работы с модемом
TinyGsmClient client(modem);                  //клиентом
PubSubClient mqtt(client);                    //mqtt

//Включение/отключение сервисов для отправки
#define NARODMON_ON   1  //1-включить отправку на naromon, 0 - выключить
#define NARODMON_TYPE 0  //Способ отправки на narodmon 0 - HTTP, 1-MQTT
#define MQTT_ON       0  //1-Включить отправку на сторонний mqtt, 0 - выключить

//ДАННЫЕ
// APN data
String GPRS_APN           = "internet.beeline.ru";    // GPRS APN
String GPRS_LOGIN         = "beeline";                // GPRS login
String GPRS_PASSWORD      = "beeline";                // GPRS password

//Общие данные для всех сервисов
int sendig_interval       = 15 * 60;                  // Частота отправки данных в секундах

#if NARODMON_ON == 1
//Параметры для сервиса https://narodmon.ru
bool   narodmon_enable    = true;                     // Включить отправку на narodmon
String device_mac         = "ACED5CC43CF9";           // уникальный номер устройства (необходимо сгенерировать новый) например взять от своей сетевой карты MAC (12-18 символов)
int8_t protocol           = 0;                        // Протокол передачи данных 0 - HTTP, 1 - MQTT
String url                = "http://narodmon.ru/";    // Базовая ссылка на сайт
#if  NARODMON_TYPE == 1
int    mqtt_narodmon_port = 1883;                     // MQTT порт для отправки на narodmon.ru
String mqtt_narodmon_type = "MQIsdp";                 // тип протокола
String login_narodmon     = "andrey.alekseev.spb";    // Логин на сайте https://narodmon.ru
String password_narodmon  = "20277";               // Пароль на сайте https://narodmon.ru
String mqtt_narodmon_topic_connect = login_narodmon + "/narodmon.ru/status";  // Топик для коннекта
String mqtt_narodmon_topic_publish = login_narodmon + "/narodmon.ru/t0";  // Топик для отправки
#endif
#endif

#if MQTT_ON == 1
//Параметры для произвольного mqtt брокера
bool   mqtt_enable         = true;                     // Включить отправку на mqtt
String mqtt_url            = "http://narodmon.ru/";    // Базовая ссылка на сайт
int    mqtt_port           = 1883;                     // MQTT порт для отправки на narodmon.ru
String mqtt_type           = "MQIsdp";                 // тип протокола
String mqtt_login          = "andrey.alekseev.spb";    // Логин на сайте https://narodmon.ru
String mqtt_password       = "fWsxnXhE";               // Пароль на сайте https://narodmon.ru
String mqtt_topic_connect  = login_narodmon + "/narodmon.ru/status";  // Топик для коннекта
String mqtt_topic_publish  = login_narodmon + "/narodmon.ru/t0";  // Топик для отправки
#endif


DeviceAddress insideThermometer, outsideThermometer;    //Переменные для хранения адресов датчика

void setup() 
{
  //Настраиваем порт для отладки
  Serial.begin(9600);  //скорость порта
  
  //Стартуем работу датчика ds18b20
  sensors.begin();

  //Инициализация ds18b20
  ds18b20_init();
  
  //Настраиваем пин сброса SIM800 на выход
  pinMode(GSM_RESET, OUTPUT);

  //Настраиваем пин сброса SIM800 на выход
  pinMode(GSM_DTR, OUTPUT);
  gsm_sleep(false);

  Serial.println("GPRS test");

  //Настраиваем порт для GSM 
  GSMport.begin(9600);

  //Сбрасываем модуль
  gsm_reset();
  
  //Настраиваем GPRS
  gprs_init();
}

void loop() 
{
  //Пробуждаем модуль
  gsm_sleep(false);
  delay(2000);
  //Читаем то что отправил нам модем, если отправил
  //если GSM модуль что-то послал нам, то
  Serial.print("Получено от GSM:: ");
  Serial.println(ReadGSM());//печатаем в монитор порта пришедшую строку
  // Получаем температуру
  Serial.print("Requesting temperatures...");
  sensors.requestTemperatures();
  Serial.println("DONE");
  float temperature = printData(insideThermometer);

  //Проверяем работает ли модем
  bool modem_status = check_sim800l();
  Serial.print("modem_status=");
  Serial.println(String(modem_status));
  //Если не работает физически перезапускаем и переинициализируем
  if ( modem_status == false)
  {
    gsm_reset();
    gprs_init();
  }
#if NARODMON_ON == 1
  //Проверяем включена ли отправка
  if (NARODMON_ON == 1)
  {
    //Отправка данных на Narodmon
    narodmon_send(temperature);
  }
#endif
  
#if MQTT_ON == 1
  //Проверяем включена ли отправка
  if (MQTT_ON == 1)
  {
    //Отправка данных на mqtt
    mqtt_send(temperature);
  }
#endif

  //Читаем то что отправил нам модем, если отправил
  //если GSM модуль что-то послал нам, то
  Serial.print("Получено от GSM:: ");
  Serial.println(ReadGSM());//печатаем в монитор порта пришедшую строку

  gsm_sleep(true);
  arduino_sleep(sendig_interval);
}


void ds18b20_init()
{
  Serial.print("Locating devices...");
  Serial.print("Found ");
  Serial.print(sensors.getDeviceCount(), DEC);
  Serial.println(" devices.");
  Serial.print("Parasite power is: "); 
  if (sensors.isParasitePowerMode()) Serial.println("ON");
  else Serial.println("OFF");
 
  if (!sensors.getAddress(insideThermometer, 0)) Serial.println("Unable to find address for Device 0"); 
 
  // show the addresses we found on the bus
  Serial.print("Device 0 Address: ");
  printAddress(insideThermometer);
  Serial.println();
 
  Serial.print("Device 1 Address: ");
  printAddress(outsideThermometer);
  Serial.println();
 
  // set the resolution to 9 bit
  sensors.setResolution(insideThermometer, TEMPERATURE_PRECISION);
 
  Serial.print("Device 0 Resolution: ");
  Serial.print(sensors.getResolution(insideThermometer), DEC); 
  Serial.println();
}

/**
 * \brief Функция аппаратной перезагрузки модуля SIM800L
 */
void gsm_reset(void)
{
  //Выводим сообщение о перезагрузке модуля в порт
  Serial.println("Сброс Модуля SIM800L");

  //Начинаем Сброс
  // 1 - Прижимает сброс к нулю
  digitalWrite(GSM_RESET, LOW);

  // 2 - Ждем 
  delay(500);

  // 3 - возвращаем сброс в единицу
  digitalWrite(GSM_RESET, HIGH);

  // 4 - Даём модулую очухаться
  delay(10000);

  //если GSM модуль что-то послал нам, то печатаем в монитор порта пришедшую строку
  Serial.print("Получено от GSM:: ");
  Serial.println(ReadGSM());
}

/**
 * \brief Функция аппаратного перевода модуля SIM800L в сон.
 * \param mode - Включить/выключить режим
 *               true - включить
 *               false - выключить 
 */
void gsm_sleep(bool mode)
{
  //Включаем режим перевода в сон
  //Проверяем режим
  if (mode)
  {
    GSMport.println("AT+CFUN=0");
  }
  else
  {
    GSMport.println("AT+CFUN=1");
  }
  delay(500);
  Serial.print("Получено от GSM:: ");
  Serial.println(ReadGSM());  //показываем ответ от GSM модуля
  //ждем пока модуль очухается
  delay(1000);
}


/**
 * \brief Функция перевода Arduino в сон.
 * \param sleep_time - Сколько спать в секундах
 */
void arduino_sleep(int sleep_time)
{
  int cycle = 0;
  int sleepMS = 0;

  noInterrupts();
  while(cycle < sleep_time)
  {
    sleepMS += Watchdog.sleep(1000);
    cycle++;
  }
  
  interrupts();
  Serial.print("Общее время сна ");
  Serial.print(sleepMS, DEC);
  Serial.println(" milliseconds.");
}

/**
  *
  * \brief          Проверка доступности модуля SIM 800L
  * \return         bool
  * \retval         true  - Работает
  * \retval         false - Завис
  */
bool check_sim800l(void)
{
  bool result = true;
  char symbol;
  String check_str;
  
  GSMport.println("AT");
  delay(500);
  //Читаем ответ от SIM800 по символьно в стороку 
  while (GSMport.available()) 
  {  
    symbol = GSMport.read();
    check_str += String(symbol);
    delay(10);
  }
  Serial.print("Получено от GSM:: ");
  Serial.println(check_str);
  if (check_str.indexOf("OK") == -1)
  {
    result = false;
    Serial.println("SIM800 Завис");
  }
  return result;
}

/**
  *
  * \brief          Инициализация GPRS модуля SIM 800L
  */
void gprs_init() 
{  
  //Процедура начальной инициализации GSM модуля
  int d = 700;
  int ATsCount = 7;
  String ATs[] = {  //массив АТ команд
    "AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"",  //Установка настроек подключения
    "AT+SAPBR=3,1,\"APN\",\"" + GPRS_APN + "\"",
    "AT+SAPBR=3,1,\"USER\",\"" + GPRS_LOGIN + "\"",
    "AT+SAPBR=3,1,\"PWD\",\"" + GPRS_PASSWORD + "\"",
    "AT+SAPBR=1,1",  //Устанавливаем GPRS соединение
    "AT+HTTPINIT",  //Инициализация http сервиса
    "AT+HTTPPARA=\"CID\",1"  //Установка CID параметра для http сессии
  };
  int ATsDelays[] = {6, 1, 1, 1, 3, 3, 1}; //массив задержек
  Serial.println("GPRG init start");
  for (int i = 0; i < ATsCount; i++) 
  {
    Serial.println(ATs[i]);  //посылаем в монитор порта
    Serial.print("Получено от GSM:: ");
    GSMport.println(ATs[i]);  //посылаем в GSM модуль
    delay(d * ATsDelays[i]);
    Serial.println(ReadGSM());  //показываем ответ от GSM модуля
    delay(d);
  }
  Serial.println("GPRG init complete");
}

/**
 * \brief Функция отправки данных на Narodmon
 * \param data - температура
 */
void narodmon_send(float data) //Процедура отправки данных на сервер
{  
  String get_str;
  int d = 500; //Базовое число для задержек
  //Проверяем как отправлять данные
  switch(NARODMON_TYPE)
  {
    // HTTP protocol
    case 0:
      //отправка данных на сайт
      
      Serial.println("Send start");
      Serial.println("setup url");
      //Формируем строку для get запроса
      get_str = "AT+HTTPPARA=\"URL\",\"";
      get_str += url;
      get_str += "get?ID=";
      get_str += device_mac; 
      get_str += "&mac1=";
      get_str += String(data);
      get_str += String("\"");
      
      //Выводим ее для проверки
      Serial.print("TEST:: ");
      Serial.println(get_str);

      //Отправляем строку модулю
      GSMport.println(get_str);
      delay(d * 2);
      Serial.print("Получено от GSM:: ");
      Serial.println(ReadGSM());
      delay(d);
      
      // Ждем ответа
      Serial.println("GET url");
      GSMport.println("AT+HTTPACTION=0");
      delay(d * 2);
      Serial.print("Получено от GSM:: ");
      Serial.println(ReadGSM());
      delay(d);
      Serial.println("Send done");
      break;

    case 1:
    #if  NARODMON_TYPE == 1
      MQTT_CONNECT(url, mqtt_narodmon_port, MQTT_type.c_str(), device_mac.c_str(), login_narodmon.c_str(), password_narodmon.c_str(), mqtt_narodmon_topic_connect.c_str(), mqtt_narodmon_topic_publish.c_str(), data);
    #endif
    break;

    default:
    break;
  }
}

#if MQTT_ON == 1
/**
  *
  * \brief  Отправка данных на сторонний Mqtt сервер
  */
void mqtt_send(float data) //Процедура отправки данных на сервер
{  
  MQTT_CONNECT(mqtt_url, mqtt_port, mqtt_type.c_str(), device_mac.c_str(), mqtt_login.c_str(), mqtt_password.c_str(), mqtt_topic_connect.c_str(), mqtt_topic_publish.c_str(), data);
}
#endif

/**
  *
  * \brief  Чтение данных от модуля
  */
String ReadGSM() {  //функция чтения данных от GSM модуля
  char c;
  String v;
  while (GSMport.available()) {  //сохраняем входную строку в переменную v
    c = GSMport.read();
    v += char(c);
    delay(10);
  }
  return v;
}

/**
  *
  * \brief  Вывод адресов устройств датчиков температуры
  */
void printAddress(DeviceAddress deviceAddress)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    // zero pad the address if necessary
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}
 
/**
  *
  * \brief  Чтение температры
  */
float printTemperature(DeviceAddress deviceAddress)
{
  float tempC = sensors.getTempC(deviceAddress);
  Serial.print("Temp C: ");
  Serial.print(tempC);
  Serial.print(" Temp F: ");
  Serial.print(DallasTemperature::toFahrenheit(tempC));
  return tempC;
}
 
// function to print a device's resolution
void printResolution(DeviceAddress deviceAddress)
{
  Serial.print("Resolution: ");
  Serial.print(sensors.getResolution(deviceAddress));
  Serial.println();    
}
 
// main function to print information about a device
float printData(DeviceAddress deviceAddress)
{
  Serial.print("Device Address: ");
  printAddress(deviceAddress);
  Serial.print(" ");
  float tempC = printTemperature(deviceAddress);
  Serial.println();
  return tempC;
}

void MQTT_CONNECT(String MQTT_SERVER, int PORT, const char MQTT_type[15], const char MQTT_CID[15], const char MQTT_user[25], const char MQTT_pass[25], const char MQTT_topic_sub[55], const char MQTT_topic[55], float temperature) 
{

  //Говорим модулю что хотим отправить пакет по TCP
  GSMport.println("AT+CIPSTART=\"TCP\",\"" + MQTT_SERVER + "\",\"" + String(PORT) + "\"");
  delay(1000);

  //Формируем пакет
  GSMport.println("AT+CIPSEND"), delay (100);
     
  GSMport.write(0x10);                                                                     // заголовок пакета на установку соединения
  GSMport.write(strlen(MQTT_type)+strlen(MQTT_CID)+strlen(MQTT_user)+strlen(MQTT_pass)+12);
  GSMport.write((byte)0),GSMport.write(strlen(MQTT_type)),GSMport.write(MQTT_type);   // тип протокола
  GSMport.write(0x03), GSMport.write(0xC2),GSMport.write((byte)0),GSMport.write(0x3C); 
  GSMport.write((byte)0), GSMport.write(strlen(MQTT_CID)),  GSMport.write(MQTT_CID);  // MQTT  идентификатор устройства
  GSMport.write((byte)0), GSMport.write(strlen(MQTT_user)), GSMport.write(MQTT_user); // MQTT логин
  GSMport.write((byte)0), GSMport.write(strlen(MQTT_pass)), GSMport.write(MQTT_pass); // MQTT пароль

  //MQTT_SUB (MQTT_topic_sub);
  MQTT_PUB (MQTT_topic, String(temperature).c_str());// пакет публикации
  
  GSMport.write(0x1A);  //broker = true;    // символ завершения и маркер
}                                         
  

void  MQTT_PUB (const char MQTT_topic[15], const char MQTT_messege[15]) // пакет на публикацию
{          
  //Добавляем информацию о публикации
  GSMport.write(0x30), GSMport.write(strlen(MQTT_topic)+strlen(MQTT_messege)+2);
  GSMport.write((byte)0), GSMport.write(strlen(MQTT_topic)), GSMport.write(MQTT_topic); // топик
  GSMport.write(MQTT_messege);  // сообщение 
}   

void  MQTT_SUB (const char MQTT_topic[15]) // пакет подписки на топик
{                                       
  
  GSMport.write(0x82), GSMport.write(strlen(MQTT_topic)+5);                          // сумма пакета 
  GSMport.write((byte)0), GSMport.write(0x01), GSMport.write((byte)0);                // 0x00
  GSMport.write(strlen(MQTT_topic)), GSMport.write(MQTT_topic);                      // топик
  GSMport.write((byte)0);  
}
