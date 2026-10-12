#define CAN_ID_TIMESTAMP 0x555
// data 0: tm_sec
// data 1: tm_min
// data 2: tm_hour
// data 3: tm_wday 0=Sunday

#define CAN_ID_STROMZAEHLER_INFO_BASIS 0x600
// raw data from info-interface n-bytes spread over CAN-IDs BASIS, BASIS+1,...
// first two bytes contain the message length (lsb first)
// an empty 0x600 message means alive
// The id toggles between 0x600 and 0x700 for alternate messages
