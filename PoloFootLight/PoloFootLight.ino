#include <SPI.h>
#include <mcp2515.h>

#define TEST_MODE false
#define BEEPER_NEEDED false // Для бипера почти не тестилось, ибо не актуально стало для меня
// Шим доступно на пинах 3, 5, 6, 9, 10, 11 в случае с ардуино на atmega328p, но 10 и 11 пины заняты подключением mcp2515
#define BEEPER 9
#define FRONT_LEFT 3
#define FRONT_RIGHT 6
#define REAR_LEFT 5
#define REAR_RIGHT 9

#define PWM_STEP 13 // Яркость это значение от 0 до 255, шаг изменения яркости это за сколько мс изменяется яркость на 1

struct can_frame canMsg;
MCP2515 mcp2515(10); // Нужно указать к какому пину подключен пин CS от MCP2515

// Двери - { Передняя левая, Передняя правая, Задняя левая, Задняя правая }
int closeDoorBrightness = 0;
bool parkingLightEnabled = false;

int doorBrightness[] = {closeDoorBrightness, closeDoorBrightness, closeDoorBrightness, closeDoorBrightness};
bool doorState[] = {0, 0, 0, 0};
int doorPin[] = {FRONT_LEFT, FRONT_RIGHT, REAR_LEFT, REAR_RIGHT};

unsigned long lastChangeBrightness;

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

#if BEEPER_NEEDED
    mcp2515.setFilterMask(MCP2515::MASK0, false, 0x0FFFF000);
    mcp2515.setFilter(MCP2515::RXF0, false, 0x0511AFFF);
    mcp2515.setFilter(MCP2515::RXF1, false, 0x0511AFFF);
#else
    mcp2515.setFilterMask(MCP2515::MASK0, false, 0x7FF);
    mcp2515.setFilter(MCP2515::RXF0, false, 0x470);
    mcp2515.setFilter(MCP2515::RXF1, false, 0x470);
#endif
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

    lastChangeBrightness = millis();
}

void loop()
{
    if (mcp2515.readMessage(&canMsg) == MCP2515::ERROR_OK)
    {
#if TEST_MODE
        Serial.print(canMsg.can_id, HEX); // print ID
        Serial.print(" ");
        Serial.print(canMsg.can_dlc, HEX); // print DLC
        Serial.print(" ");
#endif

        // работа с яркостью подсветки дверей
        if (canMsg.can_id == 0x470)
        {
            // Первый байт состояние дверей, второй - света.
            checkDoors(canMsg.data[1], canMsg.data[2]);
        }

#if BEEPER_NEEDED
        // эмуляция гонга для памяти сиденья, надо подобрать звук
        // Нулевой байт пакета с ID 0x511, при нажатии кнопки сет и запоминании значения прилетают последовательно три пакета где этот байт вида:
        // для кнопки 1 (21 A1 21), для 2 (22 A2 22), для 3 (24 А4 24)
        // Почему-то при побитовом умножении не работает, будто пропускает пакеты. Нужно проверить что с производительностью при этом
        // С указанными значениями пакетов работает лучше, но не идеально почему-то. Возможно не может поймать  вовремя единичный пакет
        if (canMsg.can_id == 0x511 && (canMsg.data[0] == 0xA4 || canMsg.data[0] == 0xA2 || canMsg.data[0] == 0xA1))
        {
            beepSeat();
        }
#endif

#if TEST_MODE
        Serial.println();
#endif
    }
    changeDoorsLigthState();
}

#if BEEPER_NEEDED
void beepSeat()
{
    // Использование функции Tone() помешает использовать ШИМ на портах вход/выхода 3 и 11 (кроме платы Arduino Mega). 
    tone(BEEPER, 1000, 1500);
}
#endif

void changeDoorsLigthState()
{
    if (parkingLightEnabled)
    {
        closeDoorBrightness = 100;
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
        for (byte i = 0; i <= 3; i++)
        {
            if (doorState[i] == 0)
            {
                if (parkingLightEnabled)
                {
                    if (doorBrightness[i] > closeDoorBrightness)
                    {
                        doorBrightness[i] -= brStep;
                    }
                    if (doorBrightness[i] < closeDoorBrightness)
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

void checkDoors(byte dataDoors, byte dataLigths)
{
#if TEST_MODE
    Serial.println('Байт данных для дверей:');
    Serial.println(dataDoors);
    Serial.println('Байт данных для света:');
    Serial.println(dataLigths);
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

    if (dataLigths & (0b01000000))
    {
#if TEST_MODE
        Serial.println("габариты включены");
#endif
        parkingLightEnabled = true;
    }
    else
    {
        parkingLightEnabled = false;
    }
}
