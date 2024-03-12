/*
 Name:		WisselAandrijving_2024.ino
 Created:	2/15/2024 3:42:05 PM
 Author:	Rob Antonisse


 Arduino project voor een wisselaandrijving op basis van 48byj-28 stappenmotoren

 PINS:
 2 (PORTD1)=DCC in
 4 (PORTD3)=bezet1
 5 (PORTD4)=bezet2
 6 (PORTD6)=Home, common home switches
 7 (PORTD7)=Switch, common switches
 8 (PORTB0)=RCLK register clock latches data in the shiftregisters
 9 (PORTB1)=SRCLK shift register clock shifts data
 10(PORTB2)=Serial out
 11(PORTB3)=Bezet3
 12(PORTB4)=Bezet4
 A0 (PORTC0)=switch/home1
 A1 (PORTC1)=sw/ho2
 A2 (PORTC2)=sw/ho3
 A3 (PORTC3)=sw/ho4
 A4 (PORTC4)=switch program
 A5 (PORTC5)=nc with 10K pulldown.

 */

 //Libraries
#include <EEPROM.h>
#include <NmraDcc.h>


//defines and constants
const byte aantalkleuren = 7;

//variables
byte shiftbyte[3];
unsigned long slowtimer;
byte stepcount[4];  //fase waarin een stepper staat
byte lastswitch[2]; //0=switches, 1=home
byte sws = 0;
byte ledcount = 3; //in shift afgetelde led die dan de focus heeft, brandt bit3~bit7
//bit 0~2 is de kleur bit0-rood, bit1=groen, bit2=blauw
byte ledkleur[5]; //voor de 5 leds
byte kleur=0;  //tijdelijk

byte stepcoils[4]; //current spoelen aan/uit 0=stepper 1
byte stepfase[4]; //was is de stepper aan het doen? 0=niks, wachten
unsigned long stepeen[4]; //aantal steps positie 1
unsigned long steptwee[4];//aantalsteps positie 2
bool stepdir[4]; //richting waarin de stepper beweegt false=naar home (stephome) true = !stephome
bool stephome[4]; //standaard richting naar homeswitch default false=linksom, true =rechtsom 
byte steppos[4]; //current positie van deze stepper


void setup() {
	Serial.begin(9600);

	//constructors


	//pins
	DDRB |= (31 << 0); //pins 8,9,10,11,12 as outputs
	DDRD |= (240 << 0); //pins 7,6,5,4 as outputs
	DDRC = 0; //portc PIns A# as inputs
	PORTC = 0; //no pullups to port C


	//temp&debug
	//shiftbyte[0] = B00000111;
	INIT();
}
void INIT() {

	for (byte i = 0; i < 5; i++) {
		ledkleur[i] = 0;
	}
	shiftbyte[0] = 0;	//bit0=s1A bit1=s1B bit2=s1C bit3=s1D   bit4=s2A bit5=s2B bit6=s2C bit7=s2D
	shiftbyte[1] = 0;   //bit0=s3A bit1=s3B bit2=s3C bit3=s3D   bit4=s4A bit5=s4B bit6=s4C bit7=s4D
	shiftbyte[2] = 7;
}

void loop() {
	Shift();
	Stepper_exe(); //800micros is maximale speed
	//slowtimer
	if (millis() - slowtimer > 20) {
		slowtimer = millis();

	
		SW_exe();
	}
}


void Shift() {

	ledcount++; if (ledcount > 7)ledcount = 3; //bits 3~6	//leds om de beurt even aan
	shiftbyte[2] = 7;//=B00000111  zet alle leds off , hier moet shiftbyte[2] worden 
	shiftbyte[2] |= (1 << ledcount); //zet 1 led aan.
	//
	Kleur(ledcount-3); //stel de kleur in voor deze led

	//plaats de shiftbytes in de schuifregisters
	//d4=ser, d5=srclk d6=sclk
	for (byte _byte = 2; _byte < 3; _byte--) {
		for (byte _bit = 7; _bit < 8; _bit--) {

			if (shiftbyte[_byte] & (1 << _bit)) { //bit is true
				PORTB |= (1 << 2); //set SER 
			}
			else {
				PORTB &= ~(1 << 2); //clear SER
			}
			PINB |= (1 << 1); PINB |= (1 << 1); //maak een puls op SRCLK pin 5, alles doorschuiven
		}
	}
	PINB |= (1 << 0); PINB |= (1 << 0); //maak een puls op RCLK 'klok' de beide bytes in de schuif registers.
}
void Kleur(byte _led) {
	shiftbyte[2] |= (7 << 0); //zet 3 kleuren uit  0=rood 1=groen 2=blauw
	switch (ledkleur[_led]) {
	case 0: //zwart, uit
		break;
	case 1: //rood
		shiftbyte[2] &= ~(1 << 0); 
		break;
	case 2: //groen
		shiftbyte[2] &= ~(1 << 1);
		break;
	case 3: //blauw
		shiftbyte[2] &= ~(1 <<2);
		break;
	case 4: //geel
		shiftbyte[2] &= ~(3 << 0);
		break;
	case 5: //lichtblauw
		shiftbyte[2] &= ~(6 << 0);
		break;
	case 6: //purper
		shiftbyte[2] &= ~(5 << 0);
		break;
	case 7: //wit
		shiftbyte[2] &= ~(7 << 0);
		break;
	}
}
void SW_exe() {
	byte _stand = 0;
	byte _changed = 0;

	_stand = PINC;
	_stand &= ~(192 << 0); //clear niet gebruikte bits 6 en 7
	_changed = _stand ^ lastswitch[sws]; //is de stand veranderd

	if (_changed > 0) { //niet nodig, maar 100x is er niks veranderd dus speeds up de boel
		for (byte i = 0; i < 6; i++) {
			if (_stand & (1 << i)) { //alleen indrukken van de knoppen iets mee doen
				 SWon(i + (4 * sws));
			}
		}
	}
	lastswitch[sws] = _stand; //onthouden laatste stand

	//instellen volgende lees cyclus
	sws++; if (sws > 1)sws = 0;
	if (sws == 1) { //read switch
		PORTD |= (1 << 7);
		PORTD &= ~(1 << 6);

	}
	else { //read home 
		PORTD |= (1 << 6);
		PORTD &= ~(1 << 7);
	}
}
void SWon(byte _sw) {
	Serial.println(_sw);
	//0~3 homeswitches  4~8 switches
		switch (_sw) {
		case 0:
			ledkleur[0] = 1;
			break;
		case 1:
			ledkleur[0] = 2;
			break;
		case 2:
			ledkleur[0] = 3;
			break;
		case 3:
			ledkleur[0] = 4;
			break;

		case 4:
			ledkleur[0] = kleur;
			break;
		case 5:
			ledkleur[1] = kleur;
			break;
		case 6:
			ledkleur[2] = kleur;
			break;
		case 7:
			ledkleur[3] = kleur;
			break;
		case 8:
			kleur++;
			if (kleur > aantalkleuren)kleur = 0;
			ledkleur[4] = kleur;
			break;

		}
		//Serial.print("Kleur: "); Serial.println(kleur);
	}

void Stepper_exe() { 	

	for (byte i = 0; i < 4; i++) { //om de beurt de 4 steppers

		switch (stepfase[i]) {
		case 0: //in rust 
			break;
		case 1: //doel omzetten en beweging starten richting home
			steppos[i] ++;
			if (steppos[i] > 2)steppos[i] = 1; //0>1 1>2 2>1
			stepdir[i] = stephome[i];

			break;
		case 2:
			break;
		}

	}

}



void Steps() {
	shiftbyte[0] = 0;
	stepcount[0]++;
	byte _b = 0;
	if (stepcount[0] > 7)stepcount[0] = 0;

	switch (stepcount[0]) {
	case 0:
		_b = 1;
		break;
	case 1:
		_b = 3;
		break;
	case 2:
		_b = 2;
		break;
	case 3:
		_b = 6;
		break;
	case 4:
		_b = 4;
		break;
	case 5:
		_b = 12;
		break;
	case 6:
		_b = 8;
		break;
	case 7:
		_b = 9;
		break;
	}

	//in shiftbyte zetten
	shiftbyte[0] |= (_b << 0);   //voor de tweede stepper dus _b<<4
}