/* SIREN 0.9.1: each OberVave program's level into SIREN (Q8 of x 8: 256 = +18 dB), so that its layer's trim stays
 * small: measured by tests/siren/level_cal.c at 256 (OberVave mixes its voices after its effects and a master) */
#pragma once
static const uint16_t OBV_GAIN[OBV_NPROG] = {86, 194, 114, 190, 144, 322, 71, 113, 461, 567, 396, 87, 291, 1031, 118, 50, 77, 65, 71, 156, 83, 113, 223, 108, 137, 93, 410};
