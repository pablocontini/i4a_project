#include <device.h>

static bool transport_contains(uint32_t network, uint32_t mask, uint32_t destination) {
    /* An unconfigured 0/0 is not a transport subnet. Treating it as one
     * intercepts every packet while a free radio is searching for an AP. */
    return mask != 0 && (destination & mask) == network;
}

bool device_is_point_to_point_message(DevicePtr device, uint32_t destination) {
    if (!device) return false;
    if (device->mode == AP) {
        AccessPointPtr ap = device->access_point_ptr;
        if (!ap || ap->stopping) return false;
        uint32_t mask = ap->ap_mask;
        uint32_t network = ap->ap_subnet;
        return transport_contains(network, mask, destination);
    }
    if (device->mode == STATION) {
        StationPtr station = device->station_ptr;
        if (!station || !station->is_fully_connected || station->stopping) return false;
        uint32_t mask = station->sta_mask;
        uint32_t network = station->sta_subnet;
        return transport_contains(network, mask, destination);
    }
    return false;
}
