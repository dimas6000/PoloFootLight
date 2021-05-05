#include <SPI.h>
#include <mcp2515.h>
// todo: удалить переменную temp
#define BEEPER 9
#define FRONT_LEFT 10
#define FRONT_RIGHT 11
#define REAR_LEFT 12
#define REAR_RIGHT 8

#define PWM_STEP 15 // Яркость это значение от 0 до 255, шаг изменения яркости это за сколько мс изменяется яркость на 1

struct can_frame canMsg;
MCP2515 mcp2515(53);

// Двери - { Передняя левая, Передняя правая, Задняя левая, Задняя правая }
int closeDoorBrightness = 0;
bool parkingLightEnabled = true;

int doorBrightness[] = {closeDoorBrightness, closeDoorBrightness, closeDoorBrightness, closeDoorBrightness};
bool doorState[] = {0, 0, 0, 0};
int doorPin[] = {FRONT_LEFT, FRONT_RIGHT, REAR_LEFT, REAR_RIGHT};
byte startOpenDoorBrightness = 1; // похоже хрень, пока ставлю 1 - потом скорее всего удалить

int temp = 0;

unsigned long lastChangeBrightness;

void setup()
{
  Serial.begin(115200);
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

  mcp2515.setConfigMode();
  mcp2515.setFilterMask(MCP2515::MASK0, false, 0x7FF);
  mcp2515.setFilter(MCP2515::RXF0, false, 0x470);
  mcp2515.setFilter(MCP2515::RXF1, false, 0x470);
  mcp2515.setFilterMask(MCP2515::MASK1, false, 0x7FF);
  mcp2515.setFilter(MCP2515::RXF2, false, 0x470);
  mcp2515.setFilter(MCP2515::RXF3, false, 0x470);
  mcp2515.setFilter(MCP2515::RXF4, false, 0x470);
  mcp2515.setFilter(MCP2515::RXF5, false, 0x511);
  mcp2515.setNormalMode();

  Serial.println("------- CAN Read ----------");
  Serial.println("ID  DLC   DATA");

  pinMode(FRONT_LEFT, OUTPUT);
  pinMode(FRONT_RIGHT, OUTPUT);
  pinMode(REAR_LEFT, OUTPUT);
  pinMode(REAR_RIGHT, OUTPUT);

  //pinMode(BEEPER, OUTPUT);

  lastChangeBrightness = millis();
  temp = millis();
}

void loop()
{
  if (mcp2515.readMessage(&canMsg) == MCP2515::ERROR_OK)
  {
    //Serial.print(canMsg.can_id, HEX); // print ID
    //Serial.print(" ");
    //Serial.print(canMsg.can_dlc, HEX); // print DLC
    //Serial.print(" ");

    // работа с яркостью подсветки дверей
    if (canMsg.can_id == 0x470)
    {
      // Первый байт состояние дверей, второй - света.
      checkDoors(canMsg.data[1], canMsg.data[2]);
    }
    // писк для памяти сиденья
    if (canMsg.can_id == 0x511 && canMsg.data[0] & 0xA == 0xA)
    {
      beepSeat();
    }
    //Serial.println();
  }
  changeDoorsLigthState();
}

void beepSeat()
{
  tone(BEEPER, 1000, 500); // Звук прекратится через 500 мс, о программа останавливаться не будет!
}

void changeDoorsLigthState()
{
  if (parkingLightEnabled)
  {
    closeDoorBrightness = 100;
    /*if (millis() - temp > 10000)
    {doorState[2] = 1;
    closeDoorBrightness = 150;
    }*/
  }
  else
  {
    closeDoorBrightness = 0;
  }

  // Нужно настроить шаг изменения яркости так, чтобы яркость подсветки ног менялась синхронно с плафоном. Плафон разгорается в течении 1.5с после открытия двери.
  // Вроде настроено при PWM_STEP = 20, но возможно для повышения яркости белого без габаритов стоит задавать иное значение, т.к. там получается около 60 шагов, а не 255
  if (millis() - lastChangeBrightness > PWM_STEP)
  {
    byte brStep = 1; //(millis() - lastChangeBrightness) / PWM_STEP;

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
          if (doorBrightness[i] > startOpenDoorBrightness)
          {
            doorBrightness[i] -= brStep;
          }
          else
          {
            doorBrightness[i] = 0;
          }
        }
      }
      else
      {
        if (parkingLightEnabled && doorBrightness[i] < 255)
        {
          doorBrightness[i] += brStep;
        }
        if (!parkingLightEnabled)
        {
          if (doorBrightness[i] < startOpenDoorBrightness)
          {
            doorBrightness[i] = startOpenDoorBrightness;
          }
          else if (doorBrightness[i] < 255)
          {
            doorBrightness[i] += brStep;
          }
        }
      }
      analogWrite(doorPin[i], doorBrightness[i]);
    }
    lastChangeBrightness = millis();
  }
}

void checkDoors(byte dataDoors, byte dataLigths)
{
  Serial.println(dataDoors);
  if (!(dataDoors & 0b00001111))
  {
    Serial.println("Все двери закрыты");
    doorState[0] = 0;
    doorState[1] = 0;
    doorState[2] = 0;
    doorState[3] = 0;
    return;
  }
  else
  {
    if (dataDoors & (0b00000001))
    {
      Serial.println("0 открыли");
      doorState[0] = 1;
    }
    else
    {
      doorState[0] = 0;
    }

    if (dataDoors & (0b00000010))
    {
      Serial.println("1 открыли");
      doorState[1] = 1;
    }
    else
    {
      doorState[1] = 0;
    }

    if (dataDoors & (0b00000100))
    {
      Serial.println("2 открыли");
      doorState[2] = 1;
    }
    else
    {
      doorState[2] = 0;
    }

    if (dataDoors & (0b00001000))
    {
      Serial.println("3 открыли");
      doorState[3] = 1;
    }
    else
    {
      doorState[3] = 0;
    }

    if (dataLigths & (0b01000000))
    {
      Serial.println("габариты включены");
      parkingLightEnabled = true;
    }
    else
    {
      parkingLightEnabled = false;
    }
  }
}
