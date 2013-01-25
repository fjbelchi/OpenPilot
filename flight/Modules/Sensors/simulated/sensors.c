/**
 ******************************************************************************
 * @addtogroup OpenPilotModules OpenPilot Modules
 * @{
 * @addtogroup Sensors
 * @brief Acquires sensor data 
 * Specifically updates the the @ref Gyros, @ref Accels, and @ref Magnetometer objects
 * @{
 *
 * @file       sensors.c
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2010.
 * @brief      Module to handle all comms to the AHRS on a periodic basis.
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

/**
 * Input objects: None, takes sensor data via pios
 * Output objects: @ref Gyros @ref Accels @ref Magnetometer
 *
 * The module executes in its own thread.
 *
 * UAVObjects are automatically generated by the UAVObjectGenerator from
 * the object definition XML file.
 *
 * Modules have no API, all communication to other modules is done through UAVObjects.
 * However modules may use the API exposed by shared libraries.
 * See the OpenPilot wiki for more details.
 * http://www.openpilot.org/OpenPilot_Application_Architecture
 *
 */

#include "pios.h"
#include "attitude.h"

#include "accels.h"
#include "actuatordesired.h"
#include "attitudeactual.h"
#include "attitudesimulated.h"
#include "attitudesettings.h"
#include "rawairspeed.h"
#include "baroaltitude.h"
#include "gyros.h"
#include "gyrosbias.h"
#include "flightstatus.h"
#include "gpsposition.h"
#include "gpsvelocity.h"
#include "homelocation.h"
#include "magnetometer.h"
#include "magbias.h"
#include "ratedesired.h"
#include "revocalibration.h"
#include "systemsettings.h"

#include "CoordinateConversions.h"

// Private constants
#define STACK_SIZE_BYTES 1540
#define TASK_PRIORITY (tskIDLE_PRIORITY+3)
#define SENSOR_PERIOD 2

#define F_PI 3.14159265358979323846f
#define PI_MOD(x) (fmod(x + F_PI, F_PI * 2) - F_PI)
// Private types

// Private variables
static xTaskHandle sensorsTaskHandle;

// Private functions
static void SensorsTask(void *parameters);
static void simulateConstant();
static void simulateModelAgnostic();
static void simulateModelQuadcopter();
static void simulateModelAirplane();

static void magOffsetEstimation(MagnetometerData *mag);

static float accel_bias[3];

static float rand_gauss();

enum sensor_sim_type {CONSTANT, MODEL_AGNOSTIC, MODEL_QUADCOPTER, MODEL_AIRPLANE} sensor_sim_type;

#define GRAV 9.81
/**
 * Initialise the module.  Called before the start function
 * \returns 0 on success or -1 if initialisation failed
 */
int32_t SensorsInitialize(void)
{

	accel_bias[0] = rand_gauss() / 10;
	accel_bias[1] = rand_gauss() / 10;
	accel_bias[2] = rand_gauss() / 10;

	AccelsInitialize();
	AttitudeSimulatedInitialize();
	BaroAltitudeInitialize();
	AirspeedSensorInitialize();
	GyrosInitialize();
	GyrosBiasInitialize();
	GPSPositionInitialize();
	GPSVelocityInitialize();
	MagnetometerInitialize();
	MagBiasInitialize();
	RevoCalibrationInitialize();

	return 0;
}

/**
 * Start the task.  Expects all objects to be initialized by this point.
 *pick \returns 0 on success or -1 if initialisation failed
 */
int32_t SensorsStart(void)
{
	// Start main task
	xTaskCreate(SensorsTask, (signed char *)"Sensors", STACK_SIZE_BYTES/4, NULL, TASK_PRIORITY, &sensorsTaskHandle);
	TaskMonitorAdd(TASKINFO_RUNNING_SENSORS, sensorsTaskHandle);
	PIOS_WDG_RegisterFlag(PIOS_WDG_SENSORS);

	return 0;
}

MODULE_INITCALL(SensorsInitialize, SensorsStart)

/**
 * Simulated sensor task.  Run a model of the airframe and produce sensor values
 */
int sensors_count;
static void SensorsTask(void *parameters)
{
	portTickType lastSysTime;

	AlarmsClear(SYSTEMALARMS_ALARM_SENSORS);
	
//	HomeLocationData homeLocation;
//	HomeLocationGet(&homeLocation);
//	homeLocation.Latitude = 0;
//	homeLocation.Longitude = 0;
//	homeLocation.Altitude = 0;
//	homeLocation.Be[0] = 26000;
//	homeLocation.Be[1] = 400;
//	homeLocation.Be[2] = 40000;
//	homeLocation.Set = HOMELOCATION_SET_TRUE;
//	HomeLocationSet(&homeLocation);


	// Main task loop
	lastSysTime = xTaskGetTickCount();
	uint32_t last_time = PIOS_DELAY_GetRaw();
	while (1) {
		PIOS_WDG_UpdateFlag(PIOS_WDG_SENSORS);

		SystemSettingsData systemSettings;
		SystemSettingsGet(&systemSettings);

		switch(systemSettings.AirframeType) {
			case SYSTEMSETTINGS_AIRFRAMETYPE_FIXEDWING:
			case SYSTEMSETTINGS_AIRFRAMETYPE_FIXEDWINGELEVON:
			case SYSTEMSETTINGS_AIRFRAMETYPE_FIXEDWINGVTAIL:
				sensor_sim_type = MODEL_AIRPLANE;
				break;
			case SYSTEMSETTINGS_AIRFRAMETYPE_QUADX:
			case SYSTEMSETTINGS_AIRFRAMETYPE_QUADP:
			case SYSTEMSETTINGS_AIRFRAMETYPE_VTOL:
			case SYSTEMSETTINGS_AIRFRAMETYPE_HEXA:
			case SYSTEMSETTINGS_AIRFRAMETYPE_OCTO:
				sensor_sim_type = MODEL_QUADCOPTER;
				break;
			default:
				sensor_sim_type = MODEL_AGNOSTIC;
		}
		
		static int i;
		i++;
		if (i % 5000 == 0) {
			//float dT = PIOS_DELAY_DiffuS(last_time) / 10.0e6;
			//fprintf(stderr, "Sensor relative timing: %f\n", dT);
			last_time = PIOS_DELAY_GetRaw();
		}
		
		sensors_count++;

		switch(sensor_sim_type) {
			case CONSTANT:
				simulateConstant();
				break;
			case MODEL_AGNOSTIC:
				simulateModelAgnostic();
				break;
			case MODEL_QUADCOPTER:
				simulateModelQuadcopter();
				break;
			case MODEL_AIRPLANE:
				simulateModelAirplane();
		}

		vTaskDelay(2 / portTICK_RATE_MS);

	}
}

static void simulateConstant()
{
	AccelsData accelsData; // Skip get as we set all the fields
	accelsData.x = 0;
	accelsData.y = 0;
	accelsData.z = -GRAV;
	accelsData.temperature = 0;
	AccelsSet(&accelsData);

	GyrosData gyrosData; // Skip get as we set all the fields
	gyrosData.x = 0;
	gyrosData.y = 0;
	gyrosData.z = 0;

	// Apply bias correction to the gyros
	GyrosBiasData gyrosBias;
	GyrosBiasGet(&gyrosBias);
	gyrosData.x += gyrosBias.x;
	gyrosData.y += gyrosBias.y;
	gyrosData.z += gyrosBias.z;

	GyrosSet(&gyrosData);

	BaroAltitudeData baroAltitude;
	BaroAltitudeGet(&baroAltitude);
	baroAltitude.Altitude = 1;
	BaroAltitudeSet(&baroAltitude);

	GPSPositionData gpsPosition;
	GPSPositionGet(&gpsPosition);
	gpsPosition.Latitude = 0;
	gpsPosition.Longitude = 0;
	gpsPosition.Altitude = 0;
	GPSPositionSet(&gpsPosition);

	// Because most crafts wont get enough information from gravity to zero yaw gyro, we try
	// and make it average zero (weakly)
	MagnetometerData mag;
	mag.x = 400;
	mag.y = 0;
	mag.z = 800;
	MagnetometerSet(&mag);
}

static void simulateModelAgnostic()
{
	float Rbe[3][3];
	float q[4];

	// Simulate accels based on current attitude
	AttitudeActualData attitudeActual;
	AttitudeActualGet(&attitudeActual);
	q[0] = attitudeActual.q1;
	q[1] = attitudeActual.q2;
	q[2] = attitudeActual.q3;
	q[3] = attitudeActual.q4;
	Quaternion2R(q,Rbe);

	AccelsData accelsData; // Skip get as we set all the fields
	accelsData.x = -GRAV * Rbe[0][2];
	accelsData.y = -GRAV * Rbe[1][2];
	accelsData.z = -GRAV * Rbe[2][2];
	accelsData.temperature = 30;
	AccelsSet(&accelsData);

	RateDesiredData rateDesired;
	RateDesiredGet(&rateDesired);

	GyrosData gyrosData; // Skip get as we set all the fields
	gyrosData.x = rateDesired.Roll + rand_gauss();
	gyrosData.y = rateDesired.Pitch + rand_gauss();
	gyrosData.z = rateDesired.Yaw + rand_gauss();

	// Apply bias correction to the gyros
	GyrosBiasData gyrosBias;
	GyrosBiasGet(&gyrosBias);
	gyrosData.x += gyrosBias.x;
	gyrosData.y += gyrosBias.y;
	gyrosData.z += gyrosBias.z;

	GyrosSet(&gyrosData);

	BaroAltitudeData baroAltitude;
	BaroAltitudeGet(&baroAltitude);
	baroAltitude.Altitude = 1;
	BaroAltitudeSet(&baroAltitude);

	GPSPositionData gpsPosition;
	GPSPositionGet(&gpsPosition);
	gpsPosition.Latitude = 0;
	gpsPosition.Longitude = 0;
	gpsPosition.Altitude = 0;
	GPSPositionSet(&gpsPosition);

	// Because most crafts wont get enough information from gravity to zero yaw gyro, we try
	// and make it average zero (weakly)
	MagnetometerData mag;
	mag.x = 400;
	mag.y = 0;
	mag.z = 800;
	MagnetometerSet(&mag);
}

float thrustToDegs = 50;
bool overideAttitude = false;
static void simulateModelQuadcopter()
{
	static double pos[3] = {0,0,0};
	static double vel[3] = {0,0,0};
	static double ned_accel[3] = {0,0,0};
	static float q[4] = {1,0,0,0};
	static float rpy[3] = {0,0,0}; // Low pass filtered actuator
	static float baro_offset = 0.0f;
	float Rbe[3][3];
	
	const float ACTUATOR_ALPHA = 0.99;
	const float MAX_THRUST = GRAV * 2;
	const float K_FRICTION = 1;
	const float GPS_PERIOD = 0.1;
	const float MAG_PERIOD = 1.0 / 75.0;
	const float BARO_PERIOD = 1.0 / 20.0;
	
	static uint32_t last_time;
	
	float dT = (PIOS_DELAY_DiffuS(last_time) / 1e6);
	if(dT < 1e-3)
		dT = 2e-3;
	last_time = PIOS_DELAY_GetRaw();
	
	FlightStatusData flightStatus;
	FlightStatusGet(&flightStatus);
	ActuatorDesiredData actuatorDesired;
	ActuatorDesiredGet(&actuatorDesired);

	float thrust = (flightStatus.Armed == FLIGHTSTATUS_ARMED_ARMED) ? actuatorDesired.Throttle * MAX_THRUST : 0;
	if (thrust < 0)
		thrust = 0;
	
	if (thrust != thrust)
		thrust = 0;
	
//	float control_scaling = thrust * thrustToDegs;
//	// In rad/s
//	rpy[0] = control_scaling * actuatorDesired.Roll * (1 - ACTUATOR_ALPHA) + rpy[0] * ACTUATOR_ALPHA;
//	rpy[1] = control_scaling * actuatorDesired.Pitch * (1 - ACTUATOR_ALPHA) + rpy[1] * ACTUATOR_ALPHA;
//	rpy[2] = control_scaling * actuatorDesired.Yaw * (1 - ACTUATOR_ALPHA) + rpy[2] * ACTUATOR_ALPHA;
//	
//	GyrosData gyrosData; // Skip get as we set all the fields
//	gyrosData.x = rpy[0] * 180 / M_PI + rand_gauss();
//	gyrosData.y = rpy[1] * 180 / M_PI + rand_gauss();
//	gyrosData.z = rpy[2] * 180 / M_PI + rand_gauss();
	
	RateDesiredData rateDesired;
	RateDesiredGet(&rateDesired);
	
	rpy[0] = (flightStatus.Armed == FLIGHTSTATUS_ARMED_ARMED) * actuatorDesired.Roll * 250 * (1 - ACTUATOR_ALPHA) + rpy[0] * ACTUATOR_ALPHA;
	rpy[1] = (flightStatus.Armed == FLIGHTSTATUS_ARMED_ARMED) * actuatorDesired.Pitch * 250 * (1 - ACTUATOR_ALPHA) + rpy[1] * ACTUATOR_ALPHA;
	rpy[2] = (flightStatus.Armed == FLIGHTSTATUS_ARMED_ARMED) * actuatorDesired.Yaw * 250 *(1 - ACTUATOR_ALPHA) + rpy[2] * ACTUATOR_ALPHA;
	
	GyrosData gyrosData; // Skip get as we set all the fields
	gyrosData.x = rpy[0] + rand_gauss();
	gyrosData.y = rpy[1] + rand_gauss();
	gyrosData.z = rpy[2] + rand_gauss();
	GyrosSet(&gyrosData);
	
	// Predict the attitude forward in time
	float qdot[4];
	qdot[0] = (-q[1] * rpy[0] - q[2] * rpy[1] - q[3] * rpy[2]) * dT * M_PI / 180 / 2;
	qdot[1] = (q[0] * rpy[0] - q[3] * rpy[1] + q[2] * rpy[2]) * dT * M_PI / 180 / 2;
	qdot[2] = (q[3] * rpy[0] + q[0] * rpy[1] - q[1] * rpy[2]) * dT * M_PI / 180 / 2;
	qdot[3] = (-q[2] * rpy[0] + q[1] * rpy[1] + q[0] * rpy[2]) * dT * M_PI / 180 / 2;
	
	// Take a time step
	q[0] = q[0] + qdot[0];
	q[1] = q[1] + qdot[1];
	q[2] = q[2] + qdot[2];
	q[3] = q[3] + qdot[3];
	
	float qmag = sqrtf(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
	q[0] = q[0] / qmag;
	q[1] = q[1] / qmag;
	q[2] = q[2] / qmag;
	q[3] = q[3] / qmag;
	
	if(overideAttitude){
		AttitudeActualData attitudeActual;
		AttitudeActualGet(&attitudeActual);
		attitudeActual.q1 = q[0];
		attitudeActual.q2 = q[1];
		attitudeActual.q3 = q[2];
		attitudeActual.q4 = q[3];
		AttitudeActualSet(&attitudeActual);
	}
	
	static float wind[3] = {0,0,0};
	wind[0] = wind[0] * 0.95 + rand_gauss() / 10.0;
	wind[1] = wind[1] * 0.95 + rand_gauss() / 10.0;
	wind[2] = wind[2] * 0.95 + rand_gauss() / 10.0;
	
	Quaternion2R(q,Rbe);
	// Make thrust negative as down is positive
	ned_accel[0] = -thrust * Rbe[2][0];
	ned_accel[1] = -thrust * Rbe[2][1];
	// Gravity causes acceleration of 9.81 in the down direction
	ned_accel[2] = -thrust * Rbe[2][2] + GRAV;
	
	// Apply acceleration based on velocity
	ned_accel[0] -= K_FRICTION * (vel[0] - wind[0]);
	ned_accel[1] -= K_FRICTION * (vel[1] - wind[1]);
	ned_accel[2] -= K_FRICTION * (vel[2] - wind[2]);

	// Predict the velocity forward in time
	vel[0] = vel[0] + ned_accel[0] * dT;
	vel[1] = vel[1] + ned_accel[1] * dT;
	vel[2] = vel[2] + ned_accel[2] * dT;

	// Predict the position forward in time
	pos[0] = pos[0] + vel[0] * dT;
	pos[1] = pos[1] + vel[1] * dT;
	pos[2] = pos[2] + vel[2] * dT;

	// Simulate hitting ground
	if(pos[2] > 0) {
		pos[2] = 0;
		vel[2] = 0;
		ned_accel[2] = 0;
	}
		
	// Sensor feels gravity (when not acceleration in ned frame e.g. ned_accel[2] = 0)
	ned_accel[2] -= 9.81;
	
	// Transform the accels back in to body frame
	AccelsData accelsData; // Skip get as we set all the fields
	accelsData.x = ned_accel[0] * Rbe[0][0] + ned_accel[1] * Rbe[0][1] + ned_accel[2] * Rbe[0][2] + accel_bias[0];
	accelsData.y = ned_accel[0] * Rbe[1][0] + ned_accel[1] * Rbe[1][1] + ned_accel[2] * Rbe[1][2] + accel_bias[1];
	accelsData.z = ned_accel[0] * Rbe[2][0] + ned_accel[1] * Rbe[2][1] + ned_accel[2] * Rbe[2][2] + accel_bias[2];
	accelsData.temperature = 30;
	AccelsSet(&accelsData);

	if(baro_offset == 0) {
		// Hacky initialization
		baro_offset = 50;// * rand_gauss();
	} else {
		// Very small drift process
		baro_offset += rand_gauss() / 100;
	}
	// Update baro periodically	
	static uint32_t last_baro_time = 0;
	if(PIOS_DELAY_DiffuS(last_baro_time) / 1.0e6 > BARO_PERIOD) {
		BaroAltitudeData baroAltitude;
		BaroAltitudeGet(&baroAltitude);
		baroAltitude.Altitude = -pos[2] + baro_offset;
		BaroAltitudeSet(&baroAltitude);
		last_baro_time = PIOS_DELAY_GetRaw();
	}
	
	HomeLocationData homeLocation;
	HomeLocationGet(&homeLocation);

	static float gps_vel_drift[3] = {0,0,0};
	gps_vel_drift[0] = gps_vel_drift[0] * 0.65 + rand_gauss() / 5.0;
	gps_vel_drift[1] = gps_vel_drift[1] * 0.65 + rand_gauss() / 5.0;
	gps_vel_drift[2] = gps_vel_drift[2] * 0.65 + rand_gauss() / 5.0;

	// Update GPS periodically	
	static uint32_t last_gps_time = 0;
	if(PIOS_DELAY_DiffuS(last_gps_time) / 1.0e6 > GPS_PERIOD) {
		// Use double precision here as simulating what GPS produces
		double T[3];
		T[0] = homeLocation.Altitude+6.378137E6f * M_PI / 180.0;
		T[1] = cos(homeLocation.Latitude / 10e6 * M_PI / 180.0f)*(homeLocation.Altitude+6.378137E6) * M_PI / 180.0;
		T[2] = -1.0;
		
		static float gps_drift[3] = {0,0,0};
		gps_drift[0] = gps_drift[0] * 0.95 + rand_gauss() / 10.0;
		gps_drift[1] = gps_drift[1] * 0.95 + rand_gauss() / 10.0;
		gps_drift[2] = gps_drift[2] * 0.95 + rand_gauss() / 10.0;

		GPSPositionData gpsPosition;
		GPSPositionGet(&gpsPosition);
		gpsPosition.Latitude = homeLocation.Latitude + ((pos[0] + gps_drift[0]) / T[0] * 10.0e6);
		gpsPosition.Longitude = homeLocation.Longitude + ((pos[1] + gps_drift[1])/ T[1] * 10.0e6);
		gpsPosition.Altitude = homeLocation.Altitude + ((pos[2] + gps_drift[2]) / T[2]);
		gpsPosition.Groundspeed = sqrt(pow(vel[0] + gps_vel_drift[0],2) + pow(vel[1] + gps_vel_drift[1],2));
		gpsPosition.Heading = 180 / M_PI * atan2(vel[1] + gps_vel_drift[1],vel[0] + gps_vel_drift[0]);
		gpsPosition.Satellites = 7;
		gpsPosition.PDOP = 1;
		GPSPositionSet(&gpsPosition);
		last_gps_time = PIOS_DELAY_GetRaw();
	}
	
	// Update GPS Velocity measurements
	static uint32_t last_gps_vel_time = 1000; // Delay by a millisecond
	if(PIOS_DELAY_DiffuS(last_gps_vel_time) / 1.0e6 > GPS_PERIOD) {
		GPSVelocityData gpsVelocity;
		GPSVelocityGet(&gpsVelocity);
		gpsVelocity.North = vel[0] + gps_vel_drift[0];
		gpsVelocity.East = vel[1] + gps_vel_drift[1];
		gpsVelocity.Down = vel[2] + gps_vel_drift[2];
		GPSVelocitySet(&gpsVelocity);
		last_gps_vel_time = PIOS_DELAY_GetRaw();
	}

	// Update mag periodically
	static uint32_t last_mag_time = 0;
	if(PIOS_DELAY_DiffuS(last_mag_time) / 1.0e6 > MAG_PERIOD) {
		MagnetometerData mag;
		mag.x = homeLocation.Be[0] * Rbe[0][0] + homeLocation.Be[1] * Rbe[0][1] + homeLocation.Be[2] * Rbe[0][2];
		mag.y = homeLocation.Be[0] * Rbe[1][0] + homeLocation.Be[1] * Rbe[1][1] + homeLocation.Be[2] * Rbe[1][2];
		mag.z = homeLocation.Be[0] * Rbe[2][0] + homeLocation.Be[1] * Rbe[2][1] + homeLocation.Be[2] * Rbe[2][2];

		// Run the offset compensation algorithm from the firmware
		magOffsetEstimation(&mag);

		MagnetometerSet(&mag);
		last_mag_time = PIOS_DELAY_GetRaw();
	}
	
	AttitudeSimulatedData attitudeSimulated;
	AttitudeSimulatedGet(&attitudeSimulated);
	attitudeSimulated.q1 = q[0];
	attitudeSimulated.q2 = q[1];
	attitudeSimulated.q3 = q[2];
	attitudeSimulated.q4 = q[3];
	Quaternion2RPY(q,&attitudeSimulated.Roll);
	attitudeSimulated.Position[0] = pos[0];
	attitudeSimulated.Position[1] = pos[1];
	attitudeSimulated.Position[2] = pos[2];
	attitudeSimulated.Velocity[0] = vel[0];
	attitudeSimulated.Velocity[1] = vel[1];
	attitudeSimulated.Velocity[2] = vel[2];
	AttitudeSimulatedSet(&attitudeSimulated);
}

/**
 * This method performs a simple simulation of a quadcopter
 * 
 * It takes in the ActuatorDesired command to rotate the aircraft and performs
 * a simple kinetic model where the throttle increases the energy and drag decreases
 * it.  Changing altitude moves energy from kinetic to potential.
 *
 * 1. Update attitude based on ActuatorDesired
 * 2. Update position based on velocity
 */
static void simulateModelAirplane()
{
	static double pos[3] = {0,0,0};
	static double vel[3] = {0,0,0};
	static double ned_accel[3] = {0,0,0};
	static float q[4] = {1,0,0,0};
	static float rpy[3] = {0,0,0}; // Low pass filtered actuator
	static float baro_offset = 0.0f;
	float Rbe[3][3];
	
	const float LIFT_SPEED = 8; // (m/s) where achieve lift for zero pitch
	const float ACTUATOR_ALPHA = 0.8;
	const float MAX_THRUST = 9.81 * 2;
	const float K_FRICTION = 0.2;
	const float GPS_PERIOD = 0.1;
	const float MAG_PERIOD = 1.0 / 75.0;
	const float BARO_PERIOD = 1.0 / 20.0;
	const float ROLL_HEADING_COUPLING = 0.1; // (deg/s) heading change per deg of roll
	const float PITCH_THRUST_COUPLING = 0.2; // (m/s^2) of forward acceleration per deg of pitch
	
	static uint32_t last_time;
	
	float dT = (PIOS_DELAY_DiffuS(last_time) / 1e6);
	if(dT < 1e-3)
		dT = 2e-3;
	last_time = PIOS_DELAY_GetRaw();
	
	FlightStatusData flightStatus;
	FlightStatusGet(&flightStatus);
	ActuatorDesiredData actuatorDesired;
	ActuatorDesiredGet(&actuatorDesired);
	
	float thrust = (flightStatus.Armed == FLIGHTSTATUS_ARMED_ARMED) ? actuatorDesired.Throttle * MAX_THRUST : 0;
	if (thrust < 0)
		thrust = 0;
	
	if (thrust != thrust)
		thrust = 0;
	
	//	float control_scaling = thrust * thrustToDegs;
	//	// In rad/s
	//	rpy[0] = control_scaling * actuatorDesired.Roll * (1 - ACTUATOR_ALPHA) + rpy[0] * ACTUATOR_ALPHA;
	//	rpy[1] = control_scaling * actuatorDesired.Pitch * (1 - ACTUATOR_ALPHA) + rpy[1] * ACTUATOR_ALPHA;
	//	rpy[2] = control_scaling * actuatorDesired.Yaw * (1 - ACTUATOR_ALPHA) + rpy[2] * ACTUATOR_ALPHA;
	//	
	//	GyrosData gyrosData; // Skip get as we set all the fields
	//	gyrosData.x = rpy[0] * 180 / M_PI + rand_gauss();
	//	gyrosData.y = rpy[1] * 180 / M_PI + rand_gauss();
	//	gyrosData.z = rpy[2] * 180 / M_PI + rand_gauss();
	
	/**** 1. Update attitude ****/
	RateDesiredData rateDesired;
	RateDesiredGet(&rateDesired);
	
	// Need to get roll angle for easy cross coupling
	AttitudeActualData attitudeActual;
	AttitudeActualGet(&attitudeActual);
	double roll = attitudeActual.Roll;
	double pitch = attitudeActual.Pitch;

	rpy[0] = (flightStatus.Armed == FLIGHTSTATUS_ARMED_ARMED) * rateDesired.Roll * (1 - ACTUATOR_ALPHA) + rpy[0] * ACTUATOR_ALPHA;
	rpy[1] = (flightStatus.Armed == FLIGHTSTATUS_ARMED_ARMED) * rateDesired.Pitch * (1 - ACTUATOR_ALPHA) + rpy[1] * ACTUATOR_ALPHA;
	rpy[2] = (flightStatus.Armed == FLIGHTSTATUS_ARMED_ARMED) * rateDesired.Yaw * (1 - ACTUATOR_ALPHA) + rpy[2] * ACTUATOR_ALPHA;
	rpy[2] += roll * ROLL_HEADING_COUPLING;
	

	GyrosData gyrosData; // Skip get as we set all the fields
	gyrosData.x = rpy[0] + rand_gauss();
	gyrosData.y = rpy[1] + rand_gauss();
	gyrosData.z = rpy[2] + rand_gauss();
	GyrosSet(&gyrosData);
	
	// Predict the attitude forward in time
	float qdot[4];
	qdot[0] = (-q[1] * rpy[0] - q[2] * rpy[1] - q[3] * rpy[2]) * dT * M_PI / 180 / 2;
	qdot[1] = (q[0] * rpy[0] - q[3] * rpy[1] + q[2] * rpy[2]) * dT * M_PI / 180 / 2;
	qdot[2] = (q[3] * rpy[0] + q[0] * rpy[1] - q[1] * rpy[2]) * dT * M_PI / 180 / 2;
	qdot[3] = (-q[2] * rpy[0] + q[1] * rpy[1] + q[0] * rpy[2]) * dT * M_PI / 180 / 2;
	
	// Take a time step
	q[0] = q[0] + qdot[0];
	q[1] = q[1] + qdot[1];
	q[2] = q[2] + qdot[2];
	q[3] = q[3] + qdot[3];
	
	float qmag = sqrtf(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
	q[0] = q[0] / qmag;
	q[1] = q[1] / qmag;
	q[2] = q[2] / qmag;
	q[3] = q[3] / qmag;
	
	if(overideAttitude){
		AttitudeActualData attitudeActual;
		AttitudeActualGet(&attitudeActual);
		attitudeActual.q1 = q[0];
		attitudeActual.q2 = q[1];
		attitudeActual.q3 = q[2];
		attitudeActual.q4 = q[3];
		AttitudeActualSet(&attitudeActual);
	}
	
	/**** 2. Update position based on velocity ****/
	static float wind[3] = {0,0,0};
	wind[0] = wind[0] * 0.95 + rand_gauss() / 10.0;
	wind[1] = wind[1] * 0.95 + rand_gauss() / 10.0;
	wind[2] = wind[2] * 0.95 + rand_gauss() / 10.0;
	wind[0] = 0;
	wind[1] = 0;
	wind[2] = 0;
	
	// Rbe takes a vector from body to earth.  If we take (1,0,0)^T through this and then dot with airspeed
	// we get forward airspeed		
	Quaternion2R(q,Rbe);

	double airspeed[3] = {vel[0] - wind[0], vel[1] - wind[1], vel[2] - wind[2]};
	double forwardAirspeed = Rbe[0][0] * airspeed[0] + Rbe[0][1] * airspeed[1] + Rbe[0][2] * airspeed[2];
	double sidewaysAirspeed = Rbe[1][0] * airspeed[0] + Rbe[1][1] * airspeed[1] + Rbe[1][2] * airspeed[2];
	double downwardAirspeed = Rbe[2][0] * airspeed[0] + Rbe[2][1] * airspeed[1] + Rbe[2][2] * airspeed[2];
	
	/* Compute aerodynamic forces in body referenced frame.  Later use more sophisticated equations  */
	/* TODO: This should become more accurate.  Use the force equations to calculate lift from the   */
	/* various surfaces based on AoA and airspeed.  From that compute torques and forces.  For later */
	double forces[3]; // X, Y, Z
	forces[0] = thrust - pitch * PITCH_THRUST_COUPLING - forwardAirspeed * K_FRICTION;         // Friction is applied in all directions in NED
	forces[1] = 0 - sidewaysAirspeed * K_FRICTION * 100;      // No side slip
	forces[2] = GRAV * (forwardAirspeed - LIFT_SPEED) + downwardAirspeed * K_FRICTION * 100;    // Stupidly simple, always have gravity lift when straight and level
	
	// Negate force[2] as NED defines down as possitive, aircraft convention is Z up is positive (?)
	ned_accel[0] = forces[0] * Rbe[0][0] + forces[1] * Rbe[1][0] - forces[2] * Rbe[2][0];
	ned_accel[1] = forces[0] * Rbe[0][1] + forces[1] * Rbe[1][1] - forces[2] * Rbe[2][1];
	ned_accel[2] = forces[0] * Rbe[0][2] + forces[1] * Rbe[1][2] - forces[2] * Rbe[2][2];
	// Gravity causes acceleration of 9.81 in the down direction
	ned_accel[2] += 9.81;

	// Apply acceleration based on velocity
	ned_accel[0] -= K_FRICTION * (vel[0] - wind[0]);
	ned_accel[1] -= K_FRICTION * (vel[1] - wind[1]);
	ned_accel[2] -= K_FRICTION * (vel[2] - wind[2]);
	
	// Predict the velocity forward in time
	vel[0] = vel[0] + ned_accel[0] * dT;
	vel[1] = vel[1] + ned_accel[1] * dT;
	vel[2] = vel[2] + ned_accel[2] * dT;
	
	// Predict the position forward in time
	pos[0] = pos[0] + vel[0] * dT;
	pos[1] = pos[1] + vel[1] * dT;
	pos[2] = pos[2] + vel[2] * dT;
	
	// Simulate hitting ground
	if(pos[2] > 0) {
		pos[2] = 0;
		vel[2] = 0;
		ned_accel[2] = 0;
	}
	
	// Sensor feels gravity (when not acceleration in ned frame e.g. ned_accel[2] = 0)
	ned_accel[2] -= GRAV;
	
	// Transform the accels back in to body frame
	AccelsData accelsData; // Skip get as we set all the fields
	accelsData.x = ned_accel[0] * Rbe[0][0] + ned_accel[1] * Rbe[0][1] + ned_accel[2] * Rbe[0][2] + accel_bias[0];
	accelsData.y = ned_accel[0] * Rbe[1][0] + ned_accel[1] * Rbe[1][1] + ned_accel[2] * Rbe[1][2] + accel_bias[1];
	accelsData.z = ned_accel[0] * Rbe[2][0] + ned_accel[1] * Rbe[2][1] + ned_accel[2] * Rbe[2][2] + accel_bias[2];
	accelsData.temperature = 30;
	AccelsSet(&accelsData);
	
	if(baro_offset == 0) {
		// Hacky initialization
		baro_offset = 50;// * rand_gauss();
	} else {
		// Very small drift process
		baro_offset += rand_gauss() / 100;
	}
	// Update baro periodically	
	static uint32_t last_baro_time = 0;
	if(PIOS_DELAY_DiffuS(last_baro_time) / 1.0e6 > BARO_PERIOD) {
		BaroAltitudeData baroAltitude;
		BaroAltitudeGet(&baroAltitude);
		baroAltitude.Altitude = -pos[2] + baro_offset;
		BaroAltitudeSet(&baroAltitude);
		last_baro_time = PIOS_DELAY_GetRaw();
	}
	
	// Update baro airpseed periodically	
	static uint32_t last_airspeed_time = 0;
	if(PIOS_DELAY_DiffuS(last_airspeed_time) / 1.0e6 > BARO_PERIOD) {
		AirspeedSensorData airspeedSensor;
		airspeedSensor.SensorConnected = AIRSPEEDSENSOR_SENSORCONNECTED_TRUE;
		airspeedSensor.CalibratedAirspeed = forwardAirspeed;
		AirspeedSensorSet(&airspeedSensor);
		last_airspeed_time = PIOS_DELAY_GetRaw();
	}
	
	HomeLocationData homeLocation;
	HomeLocationGet(&homeLocation);
	
	static float gps_vel_drift[3] = {0,0,0};
	gps_vel_drift[0] = gps_vel_drift[0] * 0.65 + rand_gauss() / 5.0;
	gps_vel_drift[1] = gps_vel_drift[1] * 0.65 + rand_gauss() / 5.0;
	gps_vel_drift[2] = gps_vel_drift[2] * 0.65 + rand_gauss() / 5.0;
	
	// Update GPS periodically	
	static uint32_t last_gps_time = 0;
	if(PIOS_DELAY_DiffuS(last_gps_time) / 1.0e6 > GPS_PERIOD) {
		// Use double precision here as simulating what GPS produces
		double T[3];
		T[0] = homeLocation.Altitude+6.378137E6f * M_PI / 180.0;
		T[1] = cos(homeLocation.Latitude / 10e6 * M_PI / 180.0f)*(homeLocation.Altitude+6.378137E6) * M_PI / 180.0;
		T[2] = -1.0;
		
		static float gps_drift[3] = {0,0,0};
		gps_drift[0] = gps_drift[0] * 0.95 + rand_gauss() / 10.0;
		gps_drift[1] = gps_drift[1] * 0.95 + rand_gauss() / 10.0;
		gps_drift[2] = gps_drift[2] * 0.95 + rand_gauss() / 10.0;
		
		GPSPositionData gpsPosition;
		GPSPositionGet(&gpsPosition);
		gpsPosition.Latitude = homeLocation.Latitude + ((pos[0] + gps_drift[0]) / T[0] * 10.0e6);
		gpsPosition.Longitude = homeLocation.Longitude + ((pos[1] + gps_drift[1])/ T[1] * 10.0e6);
		gpsPosition.Altitude = homeLocation.Altitude + ((pos[2] + gps_drift[2]) / T[2]);
		gpsPosition.Groundspeed = sqrt(pow(vel[0] + gps_vel_drift[0],2) + pow(vel[1] + gps_vel_drift[1],2));
		gpsPosition.Heading = 180 / M_PI * atan2(vel[1] + gps_vel_drift[1],vel[0] + gps_vel_drift[0]);
		gpsPosition.Satellites = 7;
		gpsPosition.PDOP = 1;
		GPSPositionSet(&gpsPosition);
		last_gps_time = PIOS_DELAY_GetRaw();
	}
	
	// Update GPS Velocity measurements
	static uint32_t last_gps_vel_time = 1000; // Delay by a millisecond
	if(PIOS_DELAY_DiffuS(last_gps_vel_time) / 1.0e6 > GPS_PERIOD) {
		GPSVelocityData gpsVelocity;
		GPSVelocityGet(&gpsVelocity);
		gpsVelocity.North = vel[0] + gps_vel_drift[0];
		gpsVelocity.East = vel[1] + gps_vel_drift[1];
		gpsVelocity.Down = vel[2] + gps_vel_drift[2];
		GPSVelocitySet(&gpsVelocity);
		last_gps_vel_time = PIOS_DELAY_GetRaw();
	}
	
	// Update mag periodically
	static uint32_t last_mag_time = 0;
	if(PIOS_DELAY_DiffuS(last_mag_time) / 1.0e6 > MAG_PERIOD) {
		MagnetometerData mag;
		mag.x = 100+homeLocation.Be[0] * Rbe[0][0] + homeLocation.Be[1] * Rbe[0][1] + homeLocation.Be[2] * Rbe[0][2];
		mag.y = 100+homeLocation.Be[0] * Rbe[1][0] + homeLocation.Be[1] * Rbe[1][1] + homeLocation.Be[2] * Rbe[1][2];
		mag.z = 100+homeLocation.Be[0] * Rbe[2][0] + homeLocation.Be[1] * Rbe[2][1] + homeLocation.Be[2] * Rbe[2][2];
		magOffsetEstimation(&mag);
		MagnetometerSet(&mag);
		last_mag_time = PIOS_DELAY_GetRaw();
	}
	
	AttitudeSimulatedData attitudeSimulated;
	AttitudeSimulatedGet(&attitudeSimulated);
	attitudeSimulated.q1 = q[0];
	attitudeSimulated.q2 = q[1];
	attitudeSimulated.q3 = q[2];
	attitudeSimulated.q4 = q[3];
	Quaternion2RPY(q,&attitudeSimulated.Roll);
	attitudeSimulated.Position[0] = pos[0];
	attitudeSimulated.Position[1] = pos[1];
	attitudeSimulated.Position[2] = pos[2];
	attitudeSimulated.Velocity[0] = vel[0];
	attitudeSimulated.Velocity[1] = vel[1];
	attitudeSimulated.Velocity[2] = vel[2];
	AttitudeSimulatedSet(&attitudeSimulated);
}

static float rand_gauss (void) {
	float v1,v2,s;
	
	do {
		v1 = 2.0 * ((float) rand()/RAND_MAX) - 1;
		v2 = 2.0 * ((float) rand()/RAND_MAX) - 1;
		
		s = v1*v1 + v2*v2;
	} while ( s >= 1.0 );
	
	if (s == 0.0)
		return 0.0;
	else
		return (v1*sqrt(-2.0 * log(s) / s));
}

/**
 * Perform an update of the @ref MagBias based on
 * Magnetometer Offset Cancellation: Theory and Implementation, 
 * revisited William Premerlani, October 14, 2011
 */
static void magOffsetEstimation(MagnetometerData *mag)
{
#if 0
	RevoCalibrationData cal;
	RevoCalibrationGet(&cal);

	// Constants, to possibly go into a UAVO
	static const float MIN_NORM_DIFFERENCE = 50;

	static float B2[3] = {0, 0, 0};

	MagBiasData magBias;
	MagBiasGet(&magBias);

	// Remove the current estimate of the bias
	mag->x -= magBias.x;
	mag->y -= magBias.y;
	mag->z -= magBias.z;

	// First call
	if (B2[0] == 0 && B2[1] == 0 && B2[2] == 0) {
		B2[0] = mag->x;
		B2[1] = mag->y;
		B2[2] = mag->z;
		return;
	}

	float B1[3] = {mag->x, mag->y, mag->z};
	float norm_diff = sqrtf(powf(B2[0] - B1[0],2) + powf(B2[1] - B1[1],2) + powf(B2[2] - B1[2],2));
	if (norm_diff > MIN_NORM_DIFFERENCE) {
		float norm_b1 = sqrtf(B1[0]*B1[0] + B1[1]*B1[1] + B1[2]*B1[2]);
		float norm_b2 = sqrtf(B2[0]*B2[0] + B2[1]*B2[1] + B2[2]*B2[2]);
		float scale = cal.MagBiasNullingRate * (norm_b2 - norm_b1) / norm_diff;
		float b_error[3] = {(B2[0] - B1[0]) * scale, (B2[1] - B1[1]) * scale, (B2[2] - B1[2]) * scale};

		magBias.x += b_error[0];
		magBias.y += b_error[1];
		magBias.z += b_error[2];

		MagBiasSet(&magBias);

		// Store this value to compare against next update
		B2[0] = B1[0]; B2[1] = B1[1]; B2[2] = B1[2];
	}
#else
	HomeLocationData homeLocation;
	HomeLocationGet(&homeLocation);
	
	AttitudeActualData attitude;
	AttitudeActualGet(&attitude);
	
	MagBiasData magBias;
	MagBiasGet(&magBias);
	
	// Remove the current estimate of the bias
	mag->x -= magBias.x;
	mag->y -= magBias.y;
	mag->z -= magBias.z;
	
	const float Rxy = sqrtf(homeLocation.Be[0]*homeLocation.Be[0] + homeLocation.Be[1]*homeLocation.Be[1]);
	const float Rz = homeLocation.Be[2];
	
	const float rate = 0.01;
	float R[3][3];
	float B_e[3];
	float xy[2];
	float delta[3];
	
	// Get the rotation matrix
	Quaternion2R(&attitude.q1, R);
	
	// Rotate the mag into the NED frame
	B_e[0] = R[0][0] * mag->x + R[1][0] * mag->y + R[2][0] * mag->z;
	B_e[1] = R[0][1] * mag->x + R[1][1] * mag->y + R[2][1] * mag->z;
	B_e[2] = R[0][2] * mag->x + R[1][2] * mag->y + R[2][2] * mag->z;
	
	float cy = cosf(attitude.Yaw * M_PI / 180.0f);
	float sy = sinf(attitude.Yaw * M_PI / 180.0f);

	xy[0] =  cy * B_e[0] + sy * B_e[1];
	xy[1] = -sy * B_e[0] + cy * B_e[1];
	
	float xy_norm = sqrtf(xy[0]*xy[0] + xy[1]*xy[1]);
	
	delta[0] = -rate * (xy[0] / xy_norm * Rxy - xy[0]);
	delta[1] = -rate * (xy[1] / xy_norm * Rxy - xy[1]);
	delta[2] = -rate * (Rz - B_e[2]);
	
	magBias.x += delta[0];
	magBias.y += delta[1];
	magBias.z += delta[2];
	MagBiasSet(&magBias);
#endif

}

/**
  * @}
  * @}
  */
