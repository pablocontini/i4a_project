#ifndef P2P_TEST_DEVICE_H
#define P2P_TEST_DEVICE_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { AP, STATION, NAN } Device_Mode;
typedef struct { uint32_t ap_subnet, ap_mask; bool stopping; } AccessPoint;
typedef AccessPoint *AccessPointPtr;
typedef struct { uint32_t sta_subnet, sta_mask; bool is_fully_connected, stopping; } Station;
typedef Station *StationPtr;
typedef struct { Device_Mode mode; AccessPointPtr access_point_ptr; StationPtr station_ptr; } Device;
typedef Device *DevicePtr;
bool device_is_point_to_point_message(DevicePtr device, uint32_t destination);
#endif
