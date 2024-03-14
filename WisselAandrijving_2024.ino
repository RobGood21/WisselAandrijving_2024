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
byte programtype= 0;  //keuze programmeer type algemeen, of per stepper
byte programfase = 0; 


byte stepcount[4];  //fase waarin een stepper staat
byte lastswitch[2]; //0=homes, 1=switches
byte sws = 0;
byte ledcount = 3; //in shift afgetelde led die dan de focus heeft, brandt bit3~bit7
//bit 0~2 is de kleur bit0-rood, bit1=groen, bit2=blauw
byte ledkleur[5]; //voor de 5 leds
byte kleur = 0;  //tijdelijk

//byte stepcoils[4]; //current spoelen aan/uit 0=stepper 1
byte stepfase[4]; //was is de stepper aan het doen? 0=niks, wachten
unsigned long steptarget[4][2]; //te bereiken doelen stepper~stand 1(0) stand 2(1)
bool stepdir[4]; //richting waarin de stepper beweegt false=naar home (stephome) true = !stephome
bool stephome[4]; //standaard richting naar homeswitch default false=linksom, true =rechtsom 
unsigned long steppos[4]; //current positie van deze stepper
byte stepstand[4]; //huidige stand van deze stepper 0=onbekend 1 of 2
byte stepdoel[4]; //eindbestemming van de beweging
unsigned long speedtime[4];  //counter voor aftellen wachten 
byte speedfactor[4]; //eeprom; getoonde snelhieds waarde 1=minimaal 24=maximaal (bv..)
unsigned int speed[4]; //ingestelde snelheid van de stepper
unsigned long coilsuitcount[4]; //tijd na bereiken doel om de motor stroomloos te zetten

byte steppercount;


void setup() {
	Serial.begin(9600);

	//constructors


	//pins
	DDRB |= (31 << 0); //pins 8,9,10,11,12 as outputs
	DDRD |= (240 << 0); //pins 7,6,5,4 as outputs
	DDRC = 0; //portc PIns A# as inputs
	PORTC = 0; //no pullups to port C

	EepromRead();
	INIT();
}
void EepromRead() {

	for (byte i = 0; i < 4; i++) {
		//data per stepper
		speedfactor[i] = EEPROM.read(10 + 1); //10~13 
		if (speedfactor[i] > 24)speedfactor[i] = 12;
		speed[i] = speedfactor[i] * 50;   //tijdelijk deze waardes nog verder bepalen. nu default dus 600ms

		//targets 
		for (byte b = 0; b < 2; b++) {
			unsigned int _default;
			if (b == 0) {
				_default = 200;
			}
			else {
				_default = 2000;
			}
			EEPROM.get(100 + (i * 20) + (b * 10), steptarget[i][b]);
			if (steptarget[i][b] > 9999) steptarget[i][b] = _default;

			//Serial.print("target: "); Serial.print(steptarget[i][b]); Serial.print("   doel:"); Serial.println(b);


		}
	}
}
void INIT() {

	for (byte i = 0; i < 4; i++) {
		ledkleur[i] = 3; //blauw
	}

	shiftbyte[0] = 0;	//bit0=s1A bit1=s1B bit2=s1C bit3=s1D   bit4=s2A bit5=s2B bit6=s2C bit7=s2D
	shiftbyte[1] = 0;   //bit0=s3A bit1=s3B bit2=s3C bit3=s3D   bit4=s4A bit5=s4B bit6=s4C bit7=s4D
	shiftbyte[2] = 7;
}

void loop() {
	Shift();

	//steppercount++;
	//if(steppercount==0) 
	Stepper_exe(); //800micros is maximale speed

	//slowtimer definieren
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
	Kleur(ledcount - 3); //stel de kleur in voor deze led

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
		shiftbyte[2] &= ~(1 << 2);
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
	//Serial.println(_sw);
	//0~3 homeswitches  4~8 switches
	switch (_sw) {
	case 0: //home 1
		StepStop(0);
		break;
	case 1: //home 2
		StepStop(1);
		break;
	case 2: //home 3
		StepStop(2);
		break;
	case 3: //home 4
		StepStop(3);
		break;

	case 4: //sw1
		StepStart(0);
		break;
	case 5: //sw2
		StepStart(1);
		break;
	case 6: //sw 3
		StepStart(2);
		break;
	case 7:  //sw 4
		StepStart(3);
		break;
	case 8: //sw program
		Program_exe();
		break;

	}
	//Serial.print("Kleur: "); Serial.println(kleur);
}

void Program_exe() {
	programtype++;
	if (programtype > 2)programtype = 0;
	switch (programtype) {
	case 0: //in bedrijf 
		ledkleur[4] = 0;
		break;
	case 1: //instellingen per stepper
		ledkleur[4] = 4;
		break;
	case 2: //algemene instellingen
		ledkleur[4] = 6;
		break;
	}
}

void StepStart(byte _stepper) {
	//start beweging van de stepper

	ledkleur[_stepper] = 3;


	//bepaal eindbestemming, hier nog iets doen met de switch mode, moment of aan/uit
	stepstand[_stepper]++;
	if (stepstand[_stepper] > 2)stepstand[_stepper] = 1; //0>1 1>2 2>1
	//met lastswich bepalen of de home switch momenteel is ingedrukt, zoja dan draaien van de home switch af, anders de home switch gaan zoeken.
	stepdoel[_stepper] = stepstand[_stepper] - 1;
	stepfase[_stepper] = 1;

	if (lastswitch[0] & (1 << _stepper)) { //homeswitch is actief
		StepStop(_stepper);
	}
	else { //homeswitch niet ingedrukt.
		stepdir[_stepper] = stephome[_stepper]; //draairichting naar homeswitch		
	}
}
void StepStop(byte _stepper) {

	if (stepfase[_stepper] == 1) { //opweg naar homeswitch
		stepdir[_stepper] = !stephome[_stepper];
		//Serial.println("stop");
		steppos[_stepper] = 0; //reset positie stepper
		stepfase[_stepper] = 2;
	}
}
void Stepper_exe() {
	for (byte i = 0; i < 4; i++) { //om de beurt de 4 steppers

		if (micros() - speedtime[i] > 2000) {     // speed[i]) { //tijdelijk ff 
			speedtime[i] = micros(); //snelheid van deze stepper

			switch (stepfase[i]) {
			case 0: //in rust 
				break;
			case 1: //opweg naar home
				Steps(i);
				break;

			case 2: //opweg naar doel
				Steps(i); //volgende stap, hierna testen of doel is bereikt.
				if (steppos[i] > steptarget[i][stepdoel[i]]) { //steppos wordt verhoogd in Steps()
					coilsuitcount[i] = millis();
					stepfase[i] = 3;
				}
				break;

			case 3: //doel bereikt wachttijd, daarna spoelen uit
				if (millis() - coilsuitcount[i] > 500) {
					CoilsUit(i);
					stepfase[i] = 0; //ruststand
					switch (stepstand[i]) {
					case 0:
						ledkleur[i] = 3;
						break;
					case 1:
						ledkleur[i] = 1;
						break;
					case 2:
						ledkleur[i] = 2;
						break;
					}
				}
				break;
			}
		}
	}
}
void CoilsUit(byte _stepper) {
	switch (_stepper) {
	case 0:
		shiftbyte[0] &= ~(B00001111 << 0);
		break;
	case 1:
		shiftbyte[0] &= ~(B11110000 << 0);
		break;
	case 2:
		shiftbyte[1] &= ~(B00001111 << 0);
		break;
	case 3:
		shiftbyte[1] &= ~(B11110000 << 0);
		break;
	}
}
void Steps(byte _stepper) {
	if (stepdir[_stepper]) { //richting homeswitch
		stepcount[_stepper]--;
		if (stepcount[_stepper] > 7)stepcount[_stepper] = 7;
	}
	else { //richting naar stepdoel
		stepcount[_stepper]++;
		if (stepcount[_stepper] > 7)stepcount[_stepper] = 0;
	}
	Coils(_stepper);

	if (stepfase[_stepper] == 2)steppos[_stepper]++;
}

void Coils(byte _stepper) {
	byte _bte = 0;
	if (_stepper > 1)_bte = 1;
	byte _nbbl = 0;
	if (_stepper & (1 << 0))_nbbl = 4;
	shiftbyte[_bte] &= ~(B00001111 << _nbbl); //reset bits 0~3

	switch (stepcount[_stepper]) {
	case 0:
		shiftbyte[_bte] |= (1 << _nbbl);  //0001
		break;
	case 1:
		shiftbyte[_bte] |= (3 << _nbbl); //0011
		break;
	case 2:
		shiftbyte[_bte] |= (2 << _nbbl); //0010
		break;
	case 3:
		shiftbyte[_bte] |= (6 << _nbbl); //0110
		break;
	case 4:
		shiftbyte[_bte] |= (4 << _nbbl); //0100
		break;
	case 5:
		shiftbyte[_bte] |= (12 << _nbbl); //1100
		break;
	case 6:
		shiftbyte[_bte] |= (8 << _nbbl); //1000
		break;
	case 7:
		shiftbyte[_bte] |= (9 << _nbbl); //1001
		break;
	}
}

//void Steps1() {
//	//coils stepper 1 A=shiftbyte[0] bit0  B=shiftbyte[0] bit 1 C=sb0 bit2 D=sb0 bit3
//	shiftbyte[0] &= ~(B00001111 << 0); //reset bits 0~3
//	switch (stepcount[0]) {
//	case 0:
//		shiftbyte[0] |= (1 << 0);  //0001
//		break;
//	case 1:
//		shiftbyte[0] |= (3 << 0); //0011
//		break;
//	case 2:
//		shiftbyte[0] |= (2 << 0); //0010
//		break;
//	case 3:
//		shiftbyte[0] |= (6 << 0); //0110
//		break;
//	case 4:
//		shiftbyte[0] |= (4 << 0); //0100
//		break;
//	case 5:
//		shiftbyte[0] |= (12 << 0); //1100
//		break;
//	case 6:
//		shiftbyte[0] |= (8 << 0); //1000
//		break;
//	case 7:
//		shiftbyte[0] |= (9 << 0); //1001
//		break;
//	}
//
//}
//void Steps2() {
//	//coils stepper 2 A=shiftbyte[0] bit4  B=shiftbyte[0] bit 5 C=sb0 bit6 D=sb0 bit7
//	shiftbyte[0] &= ~(B11110000 << 0); //reset bits 4~7
//	switch (stepcount[1]) {
//	case 0:
//		shiftbyte[0] |= (1 << 4);  //0001
//		break;
//	case 1:
//		shiftbyte[0] |= (3 << 4); //0011
//		break;
//	case 2:
//		shiftbyte[0] |= (2 << 4); //0010
//		break;
//	case 3:
//		shiftbyte[0] |= (6 << 4); //0110
//		break;
//	case 4:
//		shiftbyte[0] |= (4 << 4); //0100
//		break;
//	case 5:
//		shiftbyte[0] |= (12 << 4); //1100
//		break;
//	case 6:
//		shiftbyte[0] |= (8 << 4); //1000
//		break;
//	case 7:
//		shiftbyte[0] |= (9 << 4); //1001
//		break;
//	}
//}
//void Steps3() {
//	//coils stepper 1 A=shiftbyte[1] bit0  B=shiftbyte[1] bit 1 C=sb1 bit2 D=sb1 bit3
//	shiftbyte[1] &= ~(B00001111 << 0); //reset bits 0~3
//	switch (stepcount[2]) {
//	case 0:
//		shiftbyte[1] |= (1 << 0);  //0001
//		break;
//	case 1:
//		shiftbyte[1] |= (3 << 0); //0011
//		break;
//	case 2:
//		shiftbyte[1] |= (2 << 0); //0010
//		break;
//	case 3:
//		shiftbyte[1] |= (6 << 0); //0110
//		break;
//	case 4:
//		shiftbyte[1] |= (4 << 0); //0100
//		break;
//	case 5:
//		shiftbyte[1] |= (12 << 0); //1100
//		break;
//	case 6:
//		shiftbyte[1] |= (8 << 0); //1000
//		break;
//	case 7:
//		shiftbyte[1] |= (9 << 0); //1001
//		break;
//	}
//}
//void Steps4() {
//	//coils stepper 2 A=shiftbyte[1] bit4  B=shiftbyte[1] bit 5 C=sb1 bit6 D=sb1 bit7
//}
