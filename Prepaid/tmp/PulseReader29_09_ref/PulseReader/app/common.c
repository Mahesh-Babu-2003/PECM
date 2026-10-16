#include "common.h"
#include "main.h"

uint32_t get_abs_diffrence(uint32_t min_val, uint32_t max_val){
	uint32_t result = 0;
	if(min_val <= max_val){
		result = max_val - min_val;
	}else{
		result = min_val - max_val;
	}
	return result;
}

uint32_t get_time_diffrence(TIME_T min_val, TIME_T max_val){
	uint32_t result = 0;
	if(min_val <= max_val){
		result = max_val - min_val;
	}else{
		result = (UINT64_MAX - max_val) + min_val;
	}
	return result;
}

uint16_t crc16_ccitt(const uint8_t *data, size_t length)
{
	uint16_t crc = 0xFFFF;

	for (size_t i = 0; i < length; i++) {
		crc ^= (uint16_t)data[i] << 8;

		for (uint8_t j = 0; j < 8; j++) {
			if (crc & 0x8000)
				crc = (crc << 1) ^ 0x1021;
			else
				crc <<= 1;
		}
	}
	return crc;
}

#define CRC16_SEED      ((uint16_t)0xFFFF)
#define CRC_GPOLY_MODBUS ((uint16_t)0xA001)
unsigned short CalculatModbus_CRC16(unsigned short cur_crc, unsigned char * Buffer, unsigned short Length )
{
    /* ModBus_CRC16 Calculatd CRC16 with polynome 0xA001 and init value 0xFFFF
Input *Buffer - pointer on data
Input Lenght - number byte in buffer
Output - calculated CRC16
*/
    while (Length > 0)
    {
        unsigned int i = 8;
        cur_crc = cur_crc ^ *Buffer++;
        do
        {
            if (0x0001 & cur_crc)
            {
                cur_crc >>= 1;
                cur_crc ^= CRC_GPOLY_MODBUS;
            }
            else
            {
                cur_crc >>= 1;
            }
        }
        while (--i);
        --Length;
    }
    return cur_crc;
}

uint16_t UpdateModbus_CRC16(uint16_t  usCRC16,  uint8_t *aucData, uint16_t usInputLength)
{
    return CalculatModbus_CRC16( usCRC16, aucData,  usInputLength);
}

uint16_t ModBus_CRC16( uint8_t *aucData, uint16_t usInputLength)
{
    return CalculatModbus_CRC16( CRC16_SEED, aucData,  usInputLength);
}

void fill_crc(uint8_t *data, size_t length)
{
	if(length > sizeof(uint16_t)){
		uint16_t crc = ModBus_CRC16(data, length - sizeof(uint16_t));
		data[length - sizeof(uint16_t)] = crc >> 8;
		data[length - sizeof(uint16_t) + 1] = crc & 0xFF;
//		memcpy(&data[length-sizeof(uint16_t)], &crc, sizeof(uint16_t));
	}
}

void calculate_and_fill_modbus_crc(unsigned char * Buffer, unsigned short Length){

    if(Length > SIZE_OF_CRC){
        unsigned short crc = ModBus_CRC16(Buffer, Length -SIZE_OF_CRC);
        Buffer[Length - SIZE_OF_CRC] = crc & 0xFF;
        Buffer[Length - SIZE_OF_CRC + 1] = crc >> 8;
    }

}

void memcpy_reverse(void* dest, const void* src, uint16_t length){
  uint16_t ix = 0;
  unsigned char* dest_ptr = (unsigned char*)dest;
  unsigned char* src_ptr = (unsigned char*)src;
  while(length > 0){
    --length;
    dest_ptr[ix] = src_ptr[length];
    ++ix;
  }
}


bool validate_time(uint8_t* buffer, bool is_reverse){
	if(is_reverse == true){
		//sec && min && hr
		return ((buffer[0] <= 59) && (buffer[1] <= 59) && (buffer[2] <= 23)) ;
	}else{
		//hr && min && sec
		return ((buffer[0] <= 23) && (buffer[1] <= 59) && (buffer[2] <= 59));
	}
}

bool validate_date(uint8_t* buffer, bool is_reverse){
	if(is_reverse == true){
		//date && month && year
		return (buffer[0] <= 31) && (buffer[0] > 0) && (buffer[1] > 0) && (buffer[1] <= 12) && (buffer[2] < 100);
	}else{
		//year && month && date
		return (buffer[0] < 100) && (buffer[1] > 0) && (buffer[1] <= 12)  && (buffer[2] > 0) && (buffer[2] <= 31);
	}
}

bool validate_date_time(uint8_t* buffer, bool is_reverse){
	if(is_reverse == true){
		//sec && min && hr && date && month && year
		return (validate_time(buffer, is_reverse) && validate_date(&buffer[3], is_reverse));
	}else{
		//year && month && date && hr && min && sec
		return (validate_date (buffer, is_reverse) && validate_time(&buffer[3], is_reverse));
	}
}

uint32_t getTimestamp(int year, int month, int date, int hour, int min, int sec){

	time_t rawtime = 0;
	struct tm *timeinfo = localtime(&rawtime);
	timeinfo->tm_year = 100 + year % 100;
	if(month > 0){
		timeinfo->tm_mon = month - 1;
	}else{
		timeinfo->tm_mon = month;
	}
	timeinfo->tm_mday = date;

	timeinfo->tm_hour = hour;
	timeinfo->tm_min = min;
	timeinfo->tm_sec = sec;

	return mktime(timeinfo);
}
