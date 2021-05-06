#include <SPI.h>
#include <mcp2515.h>

#define BEEPER 9

struct can_frame canMsg;
MCP2515 mcp2515(53);

void setup()
{
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
	mcp2515.setFilter(MCP2515::RXF0, false, 0x511);
	mcp2515.setFilter(MCP2515::RXF1, false, 0x511);
	mcp2515.setFilterMask(MCP2515::MASK1, false, 0x7FF);
	mcp2515.setFilter(MCP2515::RXF2, false, 0x511);
	mcp2515.setFilter(MCP2515::RXF3, false, 0x511);
	mcp2515.setFilter(MCP2515::RXF4, false, 0x511);
	mcp2515.setFilter(MCP2515::RXF5, false, 0x511);
	mcp2515.setNormalMode();
}

void loop()
{
	if (mcp2515.readMessage(&canMsg) == MCP2515::ERROR_OK)
	{
		// эмуляция гонга для памяти сиденья, надо подобрать звук
		// Нулевой байт пакета с ID 0x511, при нажатии кнопки сет и запоминании значения прилетают последовательно три пакета где этот байт вида:
		// для кнопки 1 (21 A1 21), для 2 (22 A2 22), для 3 (24 А4 24)
		if (canMsg.can_id == 0x511 && (canMsg.data[0] == 0xA4 || canMsg.data[0] == 0xA2 || canMsg.data[0] == 0xA1))
		{
			beepSeat();
		}
	}
}

void beepSeat()
{
	// Использование функции Tone() помешает использовать ШИМ на портах вход/выхода 3 и 11 (кроме платы Arduino Mega). todo
	tone(BEEPER, 555, 1500); 
}
