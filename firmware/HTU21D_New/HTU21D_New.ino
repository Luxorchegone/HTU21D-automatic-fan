/***************************************************************************************************/
/*Программа написана для управления вентилятором ванной комнаты.
Управление вкл/выкл вентилятор - уставка влажности + гистерезис. 
Для сборки схемы необходим датчик HTU21, LCD дисплей 160Х (в моем случае 1601, но можно любой другой 
из этой серии), пара кнопок, транзистор для управления самим вентилятором или готовый релейный 
модуль и конечно же Arduino UNO, Nano или Mini Pro.*/
/***************************************************************************************************/
#include <Wire.h>
#include <HTU21D.h>
#include <LiquidCrystal.h>
#include "GyverButton.h"
#include <EEPROM.h>
#define INIT_ADDR 20   //Некий адрес в EEPROM, для механизма записи первоначального значения в EEPROM
#define INIT_KEY 40.0  //Уставка влажности при первом запуске

HTU21D myHTU21D(HTU21D_RES_RH11_TEMP11);  // Что бы не нагружать дачтик, устанавливаем 11 битное разрешение на влажность и температуру

const int rs = 3, en = 2, d4 = 6, d5 = 7, d6 = 8, d7 = 9;  //вписываем свои пины при необходимости
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);
uint8_t humidity_icon[8]{ 0x04, 0x0E, 0x0E, 0x1F, 0x1F, 0x1F, 0x0E, 0x00 };
uint8_t temperature_icon[8]{ 0x04, 0x0A, 0x0A, 0x0E, 0x0E, 0x1F, 0x1F, 0x0E };
uint8_t celsius_icon[8]{ 0x18, 0x18, 0x06, 0x09, 0x08, 0x09, 0x06, 0x00 };

GButton button_inc(5, HIGH_PULL, NORM_OPEN);  //Обе кнопки нормально открытые, посажены на землю и 5 пин (кнопка +),
GButton button_dec(4, HIGH_PULL, NORM_OPEN);  //землю и 4 пин (кнопка -).

uint32_t homescreen_update_timing;
uint32_t setpoint_setting_timing;
boolean homescreen_template_flag = false;
boolean setpoint_setting_flag = false;
float current_temp;
float current_humidity;
float humidity_setpoint;
float hysteresis_setpoint = 2.0;  //При необходимости поменять на нужное число, не забывая, что оно вещественное. Т.е. если требуемый гистерезис 2, то нужно писать 2.0

void setup() {
  pinMode(13, OUTPUT);  //При необходимости поменять на необходимый пин

  myHTU21D.begin();

  lcd.begin(16, 2);
  lcd.createChar(0, humidity_icon);
  lcd.createChar(1, temperature_icon);
  lcd.createChar(2, celsius_icon);
  lcd.setCursor(0, 0);
  lcd.print("   Loadi");
  lcd.setCursor(0, 1);
  lcd.print("ng...");
  if (EEPROM.read(INIT_ADDR) != INIT_KEY) {  // Проверяем содержит ли EEPROM по адресу INIT_ADDR значение INIT_KEY,
    EEPROM.write(INIT_ADDR, INIT_KEY);       // если не содержит, то записываем
    EEPROM.put(0, INIT_KEY);                 // в адрес 0 пишем INIT_KEY. Теперь при первом запуске устройства будет предустановлена уставка INIT_KEY
  }
  EEPROM.get(0, humidity_setpoint);  // Считываем в переменную humidity_setpoint данные из 0 адреса EEPROM. Если включение не первое, то будет считано
}  // последнее установленное значение. Если же включение первое то значение будет = INIT_KEY

void loop() {
  button_inc.tick();
  button_dec.tick();
  if (button_inc.isPress()) {  // Нажатие кнопки +
    setpointSetting(true);
  }
  if (button_dec.isPress()) {  // Нажатие кнопки -
    setpointSetting(false);
  }
  if (setpoint_setting_flag) {                        // Если отрисован экран настроек, начинаем проверять.
    if (millis() - setpoint_setting_timing > 5000) {  // Если прошло >5сек, убираем экран настроек. Если за это время было повторное нажатие кнопок + или -, то setpoint_setting_timing "обнуляется" и отсчет начинается заново.
      setpoint_setting_flag = false;                  // Убираем флаг экрана настроек
      EEPROM.put(0, humidity_setpoint);               // Обновляем данные в EEPROM
      homescreenTemplate();                           // Вызываем функцию отрисовки домашнего экрана
      homescreenUpdateData();                         // Вызываем функцию вывода текущих показаний с датчика на экран
    }
  }

  if (millis() - homescreen_update_timing > 12000) {  // Каждые 12 секунд считываем показания с датчика. Автор библиотеки рекомендует опрашивать датчик не чаще 10-18 секунд
    homescreen_update_timing = millis();
    current_temp = myHTU21D.readTemperature();
    current_humidity = myHTU21D.readHumidity();
    homescreenTemplate();                                                         // Вызываем функцию отрисовки домашнего экрана
    homescreenUpdateData();                                                       // Вызываем функцию вывода текущих показаний с датчика на экран
    hysteresisFan(humidity_setpoint, hysteresis_setpoint, current_humidity, 13);  // Вызываем функцию гистерезиса, которая будет активировать пин, на котором у нас висит вентилятор.
  }
}

void homescreenTemplate() {
  if (!homescreen_template_flag && !setpoint_setting_flag) {  // Если не активен экран настроек и не активен домашний экран, то выводим на экран шаблон домашнего экрана
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.write('\0');
    lcd.setCursor(6, 0);
    lcd.print("%");
    lcd.setCursor(1, 1);
    lcd.write('\1');
    lcd.setCursor(7, 1);
    lcd.write('\2');
    homescreen_template_flag = !homescreen_template_flag;  // Домашний экран отрисован, ставим флаг -домашний экран активен
  }
}

void homescreenUpdateData() {
  if (!setpoint_setting_flag && homescreen_template_flag) {  // Если отрисован домашний экран и не активен экран настроек, то отрисовываем значения с датчика
    lcd.setCursor(1, 0);
    lcd.print(current_humidity);
    lcd.setCursor(2, 1);
    lcd.print(current_temp);
  }
}

void setpointSetting(boolean sign) {
  if (homescreen_template_flag && !setpoint_setting_flag) {  // Если домашний экран активен и не активен экран настроек
    homescreen_template_flag = !homescreen_template_flag;    // Убираем домашний экран
    setpoint_setting_flag = !setpoint_setting_flag;          // Активируем экран настроек и начинаем отрисовывать экран настроек и текущую уставку
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Setpoint");
    lcd.setCursor(0, 1);
    lcd.print(":");
    lcd.setCursor(1, 1);
    lcd.print(humidity_setpoint);
    setpoint_setting_timing = millis();  // "Обнуляем" отсчет времени
  } else {
    if (!homescreen_template_flag && setpoint_setting_flag) {  // Если домашний экран погашен и активен экран настроек, начинаем прибавлять уставку
      if (sign) {
        humidity_setpoint += 0.5;
      } else {
        humidity_setpoint -= 0.5;
      }
      lcd.setCursor(1, 1);
      lcd.print(humidity_setpoint);
      setpoint_setting_timing = millis();  // "Обнуляем" отсчет времени
    }
  }
}
void hysteresisFan(float setPoint, float hysteresis, float inputValue, uint8_t pinOutput) {  // Функция простенького гистерезиса. Если inputValue > setPoint + hysteresis, то pinOutput ставим в HIGH,
  if (inputValue > (setPoint + hysteresis)) {                                                // если inputValue < setPoint - hysteresis, то pinOutput ставим в LOW
    digitalWrite(pinOutput, HIGH);
  } else {
    if (inputValue < (setPoint - hysteresis)) {
      digitalWrite(pinOutput, LOW);
    }
  }
}
