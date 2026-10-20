/*
 * FOC.h
 *
 *  Created on: 25.01.2019
 *      Author: stancecoke
 */

#ifndef FOC_H_
#define FOC_H_

#include "main.h"
//exportetd functions
void FOC_calculation(int16_t int16_i_as, int16_t int16_i_bs, q31_t q31_teta, int16_t int16_i_q_target, MotorState_t* MS_FOC, MotorParams_t* MP_FOC);
q31_t PI_control (PI_control_t* PI_c);
#ifdef BENCH_BUILD
void bench_ol_vector(int16_t int16_i_as, int16_t int16_i_bs, q31_t q31_teta, int16_t int16_u_d, MotorState_t* MS_FOC);
#endif
//q31_t PI_control_i_q (q31_t ist, q31_t soll);
//q31_t PI_control_i_d (q31_t ist, q31_t soll);

// Maximum Voltage applyed

#if defined(U_MAX_BENCH)
#define _U_MAX	U_MAX_BENCH   // build banc (config.h), plafond verifie par le test hote svpwm (CCR max <= DYNAMIC_ADC_THRESHOLD)
#elif defined(DISABLE_DYNAMIC_ADC)
#define _U_MAX	2000L
#else
#define _U_MAX	1920L //scaling ud, uq, u_alpha and u_beta to 2^11 = 2048, stay a little below to have a minimum time for ADC conversion in the middle of the PWM cycle
#endif



// Square Root of 3
#define _SQRT3	28  //1.73205081*16

#define ADC_DUR 250//minimal duration for proper ADC reading deadtime + noise subsiding + sample time


//globals
extern q31_t temp1;
extern q31_t temp2;
extern q31_t temp3;
extern q31_t temp4;
extern q31_t temp5;
extern q31_t temp6;
extern char PI_flag;



extern q31_t e_log[300][6];
extern char Obs_flag;
extern uint8_t ui8_debug_state;



#endif /* FOC_H_ */
