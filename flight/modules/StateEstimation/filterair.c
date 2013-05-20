/**
 ******************************************************************************
 * @addtogroup OpenPilotModules OpenPilot Modules
 * @{
 * @addtogroup State Estimation
 * @brief Acquires sensor data and computes state estimate
 * @{
 *
 * @file       filterair.c
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2013.
 * @brief      Airspeed filter, calculates true airspeed based on indicated
 *             airspeed and uncorrected barometric altitude
 *             NOTE: This Sensor uses UNCORRECTED barometric altitude for
 *             correction --  run before barometric bias correction!
 *
 * @see        The GNU Public License (GPL) Version 3
 *
 ******************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "inc/stateestimation.h"

// Private constants

// simple IAS to TAS aproximation - 2% increase per 1000ft
// since we do not have flowing air temperature information
#define IAS2TAS(alt) (1.0f + (0.02f * (alt) / 304.8f))

// Private types

// Private variables
static float altitude = 0.0f;

// Private functions

static int32_t init(void);
static int32_t filter(stateEstimation *state);


void filterAirInitialize(stateFilter *handle)
{
    handle->init   = &init;
    handle->filter = &filter;
}

static int32_t init(void)
{
    altitude = 0.0f;
    return 0;
}

static int32_t filter(stateEstimation *state)
{
    // take static pressure altitude estimation for
    if (ISSET(state->updated, bar_UPDATED)) {
        altitude = state->bar[0];
    }
    // calculate true airspeed estimation
    if (ISSET(state->updated, air_UPDATED)) {
        state->air[1] = state->air[0] * IAS2TAS(altitude);
    }

    return 0;
}


/**
 * @}
 * @}
 */
