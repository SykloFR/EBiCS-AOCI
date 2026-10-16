/*
 * CAN_Display.h
 *
 *  Created on: 19.10.2025
 *      Author: stancecoke
	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */

#ifndef INC_CAN_DISPLAY_H_
#define INC_CAN_DISPLAY_H_
#include "main.h"

/* Adresses des noeuds (5 bits). Protocole AOCI : meme format de trame que Bafang, noeuds decales de 0x10
   (controleur 0x12, afficheur 0x13, outil PC BESST 0x15, diffusion 0x1F ; Bafang : 0x02 / 0x03 / 0x05 / 0x1F).
   Cible Bafang d'origine : compiler avec -DCAN_NODE_BAFANG. */
#ifdef CAN_NODE_BAFANG
#define CAN_NODE_CONTROLLER 0x02
#define CAN_NODE_DISPLAY    0x03
#define CAN_NODE_BESST      0x05
#else
#define CAN_NODE_CONTROLLER 0x12
#define CAN_NODE_DISPLAY    0x13
#define CAN_NODE_BESST      0x15
#endif
#define CAN_NODE_BROADCAST  0x1F
/* ID etendu d'une trame de diffusion emise par le controleur (operation 0 = ecriture) */
#define CAN_EFID_BCAST(cmd) ((((uint32_t)CAN_NODE_CONTROLLER)<<24)|(((uint32_t)CAN_NODE_BROADCAST)<<19)|(cmd))

void processCAN_Rx(MotorParams_t* MP, MotorState_t* MS);
void sendCAN_Tx(MotorParams_t* MP, MotorState_t* MS);
void sendCAN_Poll(MotorParams_t* MP, MotorState_t* MS, uint16_t command);
void update_checksum(void);

extern uint8_t Para0[64];
extern uint8_t Para1[64];
extern uint8_t Para2[64];
extern uint16_t k;

typedef struct
{

	uint8_t       	target;
	uint8_t       	source;
	uint8_t       	operation;
	uint16_t       	command;


}Ext_ID_t;



#endif /* INC_CAN_DISPLAY_H_ */


