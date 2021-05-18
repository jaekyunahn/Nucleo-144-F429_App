/*
 * app.h
 *
 *  Created on: May 3, 2021
 *      Author: ajg1079
 */

#ifndef INC_APP_H_
#define INC_APP_H_

// SW 인터럽트 누를때마다 바뀔때 적용되는 변수. 동작모드
extern int iMode;

/*
 *	Entry Application Function
 */
void main_app_function(void);

//get Rx Data From interrupt
void getRxBuffer(uint8_t data);

#endif /* INC_APP_H_ */
