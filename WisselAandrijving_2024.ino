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


//variables
byte shiftbyte[3];
unsigned long slowtimer;
byte stepcount[4];  //fase waarin een stepper staat
byte lastswitch[2]; //0=switches, 1=home
byte sws = 0;
byte ledcount = 3;
byte ledkleur[4]; //voor de 4 leds

byte kleur=4;  //tijdelijk om met een knop een kleur te kunnen kiezen


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

	for (byte i = 0; i < 4; i++) {
		ledkleur[i] = 0;
	}
}

void loop() {
	Shift();

	//slowtimer
	if (millis() - slowtimer > 20) {
		slowtimer = millis();

		//	Stepper_exe(); //800micros is maximale speed
		SW_exe();

	}
}


void Shift() {

	//leds om de beurt even aan
	ledcount++; if (ledcount > 6)ledcount = 3; //bits 3~6

	shiftbyte[0] = 7;//=B00000111  zet alle leds off , hier moet shiftbyte[2] worden 
	shiftbyte[0] |= (1 << ledcount); //zet 1 led aan.

	//if (ledcount == 3) {
	//shiftbyte[0] &= ~(1 << 0);
	//}

	
	Kleur(ledcount-3); //stel de kleur in voor deze led

	//plaats
	//  de shiftbytes in de schuifregisters
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
	//shiftbyte[0] |= (7 << 0); //zet 3 kleuren uit  0=rood 1=groen 2=blauw
	

	switch (ledkleur[_led]) {
	case 0: //zwart, uit
		break;
	case 1: //rood
		shiftbyte[0] &= ~(1 << 0); 
		break;
	case 2: //groen
		shiftbyte[0] &= ~(1 << 1);
		break;
	case 3: //blauw
		shiftbyte[0] &= ~(1 <<2);
		break;
	case 4: //wit
		shiftbyte[0] &= ~(7 << 0);
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
	//Serial.println(_sw);
	//0~3 homeswitches  4~8 switches
	if (_sw > 3) { //switches
		if (ledkleur[_sw - 4] == 0) {
			ledkleur[_sw - 4] = kleur;
		}
		else {
			ledkleur[_sw - 4] = 0;
		}
	}
	else { //home
		switch (_sw) {
		case 0:
			kleur++;
			break;
		case 1:
			kleur--;
			break;
		}
		Serial.print("Kleur: "); Serial.println(kleur);
	}
}

void Stepper_exe() {

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