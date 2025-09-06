#include "can.h"
#include "mcp2515.h"

#include <SPI.h>

#define TEST_MODE false
// Шим доступно на пинах 3, 5, 6, 9, 10, 11 в случае с ардуино на atmega328p, но 10 и 11 пины заняты подключением mcp2515
#define FRONT_LEFT 3
#define FRONT_RIGHT 6
#define REAR_LEFT 5
#define REAR_RIGHT 9

#define PWM_STEP 5 // Яркость это значение от 0 до 255, шаг изменения яркости это за сколько мс изменяется яркость на 1

struct can_frame canMsg;
MCP2515 mcp2515(10); // Нужно указать к какому пину подключен пин CS от MCP2515

int redBrightness = 90; // Яркость фоновой подсветки на габаритах

// Двери - { Передняя левая, Передняя правая, Задняя левая, Задняя правая }
int closeDoorBrightness = 0;
bool parkingLightEnabled = false;

int doorBrightness[] = {closeDoorBrightness, closeDoorBrightness, closeDoorBrightness, closeDoorBrightness};
bool doorState[] = {0, 0, 0, 0};
int doorPin[] = {FRONT_LEFT, FRONT_RIGHT, REAR_LEFT, REAR_RIGHT};

unsigned long lastChangeBrightness;

int notUsedPins[] = {2,4,7,8,14,15,16,17,18,19,20,21};

void setup()
{
#if TEST_MODE
    Serial.begin(115200);
#endif
    SPI.begin();

    mcp2515.reset();
    mcp2515.setBitrate(CAN_100KBPS, MCP_8MHZ);
    // Устанваливаем фильтры. Копипаста с моим ID.
    // https://pro-diod.ru/download/datasheets/mcp2515/can_pic18cxx8.pdf
    // 22 страница рассказывает о работе маски и фильтра.
    // Фильтрация применяется только к битам заданным в 1 в маске.
    // Если бит в маске задан как 1 - он должен совпадать с битом фильтра, если бит в маске 0 - принимаются оба варианта бита независимо от маски.
    // Т.е. маска обозначает какие биты нужно фильтровать, и далее сообщение должно соответстввать этим битам какого-либо фильтра
    // Если хоть один фильтр не задать, то будут лезть ненужные пакеты.
    // По ссылке ниже почитать текст "Совпадение с фильтром"
    // http://microsin.net/adminstuff/hardware/mcp2515-stand-alone-can-controller-with-spi-interface.html
    // Ссылка выше про устройство буферов. У mcp2515 есть 3 буфера приема, из них фильтруются в два буфера сообщения.
    // Надо разобраться с текстом строки ниже, как сконфигурировать данный регистр. По идее по сиденью должно в приоритете улетать в 0 буфер сообщение, а по подсветке уже можно немного игнорить, т.к. пакет от сиденья один с нужным id, а по подсветке 10 пакетов в секунду.
    // Режим пролонгации буфера RXB0. Дополнительно регистр RXB0CTRL может быть сконфигурирован так, что если RXB0 содержит допустимое сообщение, и при этом было принято другое допустимое сообщение, то не произойдет ошибка переполнения, и новое сообщение просто будет перемещено в буфер RXB1, независимо от критерия допустимости, определенного для буфера RXB1.
    // Возможно можно настроить чтоб в буфер нулевой падали только сообщения сиденья, теоретически возможна фильтрация по первым байтам сообщения, строка ниже
    // Фильтрация по байтам данных. Когда принимаются стандартные фреймы данных (у которых 11-битные идентификаторы), MCP2515 автоматически накладывает 16 бит масок и фильтров, обычно связанных с расширенными идентификаторами, на первые 16 бит поля данных (байты данных 0 и 1). На рис. 4-4 показано, как маски и фильтры прикладываются для расширенных и стандартных фреймов данных.
    // Теоретически фильтр будет падать на 0x511A
    // Вот пример http://arduino.ru/forum/apparatnye-voprosy/arduino-uno-i-mcp2515-can?page=3#comment-427930
    // Вроде все должны быть восьмибайтные фильтры и маски
    // Переделал на вариант - принимаем в первый буфер только пакеты с первым байтом данных равным A - остальные не фильтруем. Должно лучше ловить срабатывание памяти
    // Переделано по примеру по ссылке выше. Но почему тогда у меня работает фильтр для 470 id, надо разобраться.
    // Судя по коду setFilter так и должно быть, как для id470, а вот то что в нулевом буфере не сработает т.к. в коде setFilter нужные буферы не устанавливаются
    mcp2515.setConfigMode();

    mcp2515.setFilterMask(MCP2515::MASK0, false, 0x7FF);
    mcp2515.setFilter(MCP2515::RXF0, false, 0x470);
    mcp2515.setFilter(MCP2515::RXF1, false, 0x470);

    mcp2515.setFilterMask(MCP2515::MASK1, false, 0x7FF);
    mcp2515.setFilter(MCP2515::RXF2, false, 0x470);
    mcp2515.setFilter(MCP2515::RXF3, false, 0x470);
    mcp2515.setFilter(MCP2515::RXF4, false, 0x470);
    mcp2515.setFilter(MCP2515::RXF5, false, 0x470);
    mcp2515.setNormalMode();


#if TEST_MODE
    Serial.println("------- CAN Read ----------");
    Serial.println("ID  DLC   DATA");
#endif

    pinMode(FRONT_LEFT, OUTPUT);
    pinMode(FRONT_RIGHT, OUTPUT);
    pinMode(REAR_LEFT, OUTPUT);
    pinMode(REAR_RIGHT, OUTPUT);

    // Неиспользуемый выводы конфигурируем как вход, подтянуты к земле через 10ком
    for (int notUsedPin : notUsedPins) { 
      pinMode(notUsedPin, INPUT);      
    }
    
    lastChangeBrightness = millis();
}

void loop()
{
    if (mcp2515.readMessage(&canMsg) == MCP2515::ERROR_OK)
    {
#if TEST_MODE
        // delay(1000);
        Serial.print(canMsg.can_id, HEX); // print ID
        Serial.print(" ");
        Serial.print(canMsg.can_dlc, HEX); // print DLC
        Serial.print(" ");
        Serial.println();
        for (int i = 0; i<8; i++)
        {
          Serial.print(canMsg.data[i], DEC);
          Serial.print(" "); 
        }
        Serial.println();
#endif

        // работа с яркостью подсветки дверей
        if (canMsg.can_id == 0x470)
        {
            // Первый байт состояние дверей, второй - света.
            checkDoors(canMsg.data[1], canMsg.data[2]);
        }

#if TEST_MODE
        Serial.println();
#endif
    }
    changeDoorsLigthState();
}

void changeDoorsLigthState()
{
    if (parkingLightEnabled)
    {
        closeDoorBrightness = redBrightness;
    }
    else
    {
        closeDoorBrightness = 0;
    }

    // Нужно настроить шаг изменения яркости так, чтобы яркость подсветки ног менялась синхронно с плафоном. Плафон разгорается в течении 1.5с после открытия двери.
    // Вроде настроено при PWM_STEP = 20, но возможно для повышения яркости белого без габаритов стоит задавать иное значение, т.к. там получается около 60 шагов, а не 255
    if (millis() - lastChangeBrightness > PWM_STEP)
    {
        byte brStep = 1;

        /*
  В цикле для каждой двери проверяем:
  1) если дверь закрыта, то меняем яркость до уровня требуемой при закрытой двери (зависит от состояния габаритных огней). 
  Если габариты выключены, то плавно уменьшаем яркость белого цвета, а потом гасим полностью @todo: сделать вольтметр и проверять напряжение, чтобы гасить правильно.
  2) если дверь открыта, то при включенных габаритах просто плавно повышаем яркость до 255, а при выключенных сразу ставим яркость на начало включения белого цвета
  и далее плавно повышаем до 255.
  */
      // 0 - дверь водителя, 1 - переднего пассажира, 2 - левая задняя, 3 - правая задняя
        for (byte i = 0; i <= 3; i++)
        {
            if (doorState[i] == 0)
            {
                if (parkingLightEnabled)
                {
                    int brightModifier = 1;

                    // Тут просто данные? они изначально из кан, число от 20 до ~90. Причем число менятся будт ов зависимости от напряжения в бортсети
                    // Модифицируем
                    int closeDoorBrightnessLocal = (closeDoorBrightness-20)*1.8; 
                    
                    // Делаем тусклее красную подсветку у водителя
                    if (i == 0)
                    {
                       closeDoorBrightnessLocal = closeDoorBrightnessLocal * 0.65;
                    }
                    // Делаем ярче красную подсветку у переднего пассажира
                    if (i == 1)
                    {
                       closeDoorBrightnessLocal = closeDoorBrightnessLocal * 1.5;
                    }

                    // Проверим, что число не больше 150, т.к. если больше - есть риск что загорится уже белая подсветка
                    if (closeDoorBrightnessLocal > 150)
                    {
                      closeDoorBrightnessLocal = 150;
                    }

                      #if TEST_MODE
                       /*
                        Serial.print(i);
                        Serial.print(" ");
                        Serial.print(closeDoorBrightnessLocal);
                        Serial.print(" | ");
                        */
                      #endif
                    
                    if (doorBrightness[i] > closeDoorBrightnessLocal)
                    {
                        doorBrightness[i] -= brStep;
                    }
                    if (doorBrightness[i] < closeDoorBrightnessLocal)
                    {
                        doorBrightness[i] += brStep;
                    }
                }
                else
                {
                    if (doorBrightness[i] > 0)
                    {
                        doorBrightness[i] -= brStep;
                    }
                }
            }
            else
            {
                if (doorBrightness[i] < 255)
                {
                    doorBrightness[i] += brStep;
                }
            }
            analogWrite(doorPin[i], doorBrightness[i]);
        }
        lastChangeBrightness = millis();
    }
}

void checkDoors(byte dataDoors, byte dataLights)
{
#if TEST_MODE
    Serial.println("dataDoors:");
    Serial.println(dataDoors);
    Serial.println("dataLights:");
    Serial.println(dataLights);
#endif
    if (!(dataDoors & 0b00001111))
    {
#if TEST_MODE
        Serial.println("Все двери закрыты");
#endif
        doorState[0] = 0;
        doorState[1] = 0;
        doorState[2] = 0;
        doorState[3] = 0;
    }
    else
    {
        if (dataDoors & (0b00000001))
        {
#if TEST_MODE
            Serial.println("0 открыли");
#endif
            doorState[0] = 1;
        }
        else
        {
            doorState[0] = 0;
        }

        if (dataDoors & (0b00000010))
        {
#if TEST_MODE
            Serial.println("1 открыли");
#endif
            doorState[1] = 1;
        }
        else
        {
            doorState[1] = 0;
        }

        if (dataDoors & (0b00000100))
        {
#if TEST_MODE
            Serial.println("2 открыли");
#endif
            doorState[2] = 1;
        }
        else
        {
            doorState[2] = 0;
        }

        if (dataDoors & (0b00001000))
        {
#if TEST_MODE
            Serial.println("3 открыли");
#endif
            doorState[3] = 1;
        }
        else
        {
            doorState[3] = 0;
        }
    }

    // Условие для поло без возможности регулировки яркости подсветки. 
    // if (dataLights & (0b01000000)), иначе по описанию ниже
    // Если яркость подсветки салона регулируется, то число меняется от примерно 20 до 80 на заведенной машине
    // И видел до 90 на заглушенной. Видимо BCM подстраивает значение в зависимости от выходного напряжения. 
    // Как я понял, в этом байте именно заданная яркость подсветки салона (кнопок и т.п.). При этом если подсветка выключена - тут 0 будет
    if (dataLights > 20)
    {
        parkingLightEnabled = true;

        // Это для Поло с регулировкой яркости подсветки, если регулятора нет - или оставить так и будет дефолтная яркость авто передаваемая в can (вроде 64), 
        // или просто удалить эту строку и будет 90
        redBrightness = dataLights; 
        
        #if TEST_MODE
        Serial.println("габариты включены");
        Serial.println(redBrightness);
        #endif
    }
    else
    {
        parkingLightEnabled = false;
    }
}
