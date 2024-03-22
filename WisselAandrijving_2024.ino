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

//constructors
NmraDcc  Dcc;


//defines and constants
const byte aantalkleuren = 7;
const byte aantaleffectcounts = 4;




//variables
unsigned int DCCadres = 1; //default=1;
byte Invert[4];   //bit0=step, dus kleur van de led, bit1=invert het DCC command bit2=homerichting bit 3=auto sequence of auto single
byte stepreg; //8xbool bit0 (true)=busy, motor draait naar andere positie
byte shiftbyte[3];
unsigned long slowtimer;
byte programtype = 0;  //keuze programmeer type algemeen, of per stepper
byte programfase = 0;
byte effects = 0; //led effecten
byte effectcount[aantaleffectcounts]; //tellers voor de led effecten

bool scroll = false; //scroll functie 
byte scrollcount[2]; //0=switch 3(2) 1=switch 4(3)

byte stepcount[4];  //fase waarin een stepper staat
byte lastswitch[2]; //0=homes, 1=switches

byte sws = 0;

byte ledcount = 3; //in shift afgetelde led die dan de focus heeft, brandt bit3~bit7
//bit 0~2 is de kleur bit0-rood, bit1=groen, bit2=blauw
byte ledkleur[5]; //voor de 5 leds
byte kleur = 0;  //tijdelijk

byte stepfase[4]; //was is de stepper aan het doen? 0=niks, wachten
unsigned long steptarget[4][2]; //te bereiken doelen stepper~stand 1(0) stand 2(1)
bool stepdir[4]; //richting waarin de stepper beweegt false=naar home (stephome) true = !stephome
//bool stephome[4]; //standaard richting naar homeswitch default false=linksom, true =rechtsom 
unsigned long steppos[4]; //current positie van deze stepper
byte stepstand[4]; //huidige stand van deze stepper 0=onbekend 1 of 2
byte stepdoel[4]; //eindbestemming van de beweging
unsigned long speedtime[4];  //counter voor aftellen wachten 
byte speedfactor[4]; //eeprom; getoonde snelhieds waarde 1=minimaal 24=maximaal (bv..)
unsigned int speed[4]; //ingestelde snelheid van de stepper
unsigned long coilsuitcount[4]; //tijd na bereiken doel om de motor stroomloos te zetten

byte steppercount;
byte stepprogram; //welke stepper wordt er ingesteld

byte stepauto[4]; //auto step type 0=uit 1=heen en weer 2= alleen weer
byte autotimefactor[4]; //bepaald de wacht tijd van het autoeffect
//sequence of single is Invert bit3
unsigned long autotimer; //timer van 1 seconde
unsigned int autocounter[4]; //tellers voor de wachttijden
byte autofocus; //welke van de 4 steppers heeft de focus in de sequence mode



void setup() {
	Serial.begin(9600);

	//constructors
	Dcc.pin(0, 2, 1); //interrupt number 0; pin 2; pullup to pin2
	Dcc.init(MAN_ID_DIY, 10, 0b10000000, 0); //bit7 true maakt accessoire decoder, bit6 false geeft decoder per decoderadres

	//pins
	DDRB |= (31 << 0); //pins 8,9,10,11,12 as outputs
	DDRD |= (240 << 0); //pins 7,6,5,4 as outputs
	DDRC = 0; //portc PIns A# as inputs
	PORTC = 0; //no pullups to port C

	EepromRead();
	INIT();
}
void Factory() {

	Serial.println("factory");
	for (int i = 0; i < EEPROM.length(); i++) {
		EEPROM.update(i, 0xFF);
	}
	setup();
}
void Eeprom_write() {
	EEPROM.put(100, DCCadres);

	for (byte i = 0; i < 4; i++) { //instellingen opslaan per stepper

		EEPROM.update(15 + i, Invert[i]);
		EEPROM.update(20 + i, stepauto[i]);
		EEPROM.update(25 + i, autotimefactor[i]);
		EEPROM.put(200 + (i * 10) + 5, steptarget[i][1]);

		EEPROM.update(10 + i, speedfactor[i]);


	}
}
void EepromRead() {
	EEPROM.get(100, DCCadres);
	if (DCCadres > 512)DCCadres = 1;

	for (byte i = 0; i < 4; i++) {
		//data per stepper

		Invert[i] = EEPROM.read(15 + i);
		if (Invert[i] == 0xFF)Invert[i] = 0; //bit0 kleur invert, bit1=dcc invert

		stepauto[i] = EEPROM.read(20 + i);
		if (stepauto[i] == 0xFF)stepauto[i] = 0;

		autotimefactor[i] = EEPROM.read(25 + i);
		if (autotimefactor[i] == 0xFF)autotimefactor[i] = 0; //is random

		speedfactor[i] = EEPROM.read(10 + i); //10~13 
		StepperSpeed(i);

		//targets 
		for (byte b = 0; b < 2; b++) {
			unsigned int _default;
			if (b == 0) {
				_default = 200;
			}
			else {
				_default = 2000;
			}
			EEPROM.get(200 + (i * 10) + (b * 5), steptarget[i][b]);

			if (steptarget[i][b] > 9999) steptarget[i][b] = _default;
			//Serial.println(steptarget[i][b], DEC);
		}
	}
}
void INIT() {

	for (byte i = 0; i < 4; i++) {
		ledkleur[i] = 3; //blauw
		stepstand[i] = 0;
	}
	ledkleur[4] = 0;

	shiftbyte[0] = 0;	//bit0=s1A bit1=s1B bit2=s1C bit3=s1D   bit4=s2A bit5=s2B bit6=s2C bit7=s2D
	shiftbyte[1] = 0;   //bit0=s3A bit1=s3B bit2=s3C bit3=s3D   bit4=s4A bit5=s4B bit6=s4C bit7=s4D
	shiftbyte[2] = 7;

	effects = 0;
	stepreg = 0;
	programtype = 0;
	programfase = 0;


}
void loop() {
	Dcc.process();
	Shift();

	//steppercount++;
	//if(steppercount==0) 
	Stepper_exe(); //800micros is maximale speed

	//slowtimer definieren
	if (millis() - slowtimer > 10) {
		slowtimer = millis();
		SW_exe();
		if (effects > 0)LedEffect();
	}

	if (millis() - autotimer > 1000) {
		autotimer = millis();
		Auto_exe();
	}
}
//DCC 
void notifyDccAccTurnoutBoard(uint16_t BoardAddr, uint8_t OutputPair, uint8_t Direction, uint8_t OutputPower) {

	//	Serial.print("Decoderadres: ");	Serial.print(BoardAddr);	Serial.print("     Channel:");	Serial.print(OutputPair);	Serial.print("      Port");	Serial.print(Direction);	Serial.print("   ONoff:");	Serial.println(OutputPower);

	if (effects == 1) { //Wachten op DCC command voor instellen adres
		effects = 2; //confirmation effect DCC adres ingesteld
		effectcount[0] = 0;
		LedsAll(2); //4xgroen
		DCCadres = BoardAddr;
		return; //om te voorkomen dat dit command verder wordt verwerkt.
	}


	if (programtype == 0 && DCCadres == BoardAddr) { //alleen tijdens in bedrijf

		//eventueel inverted op het DCC command
		if (Invert[OutputPair & (1 << 1)]) {
			StepStart(OutputPair, (1 - Direction) + 1);
		}
		else {
			StepStart(OutputPair, Direction + 1);
		}
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
void Auto_exe() {
	//called 1 sec from loop
}
void LedsAll(byte _kleur) {
	//Zet alle 4 leds opde zelfde kleur
	for (byte i = 0; i < 4; i++) {
		ledkleur[i] = _kleur;
	}
}
void EffectCountClear() {
	for (byte i = 0; i < aantaleffectcounts; i++) {
		effectcount[i] = 0;
	}
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
	//byte _stand = 0;
	byte _changed = 0;
	byte _stand = PINC;
	_stand &= ~(192 << 0); //clear niet gebruikte bits 6 en 7

	_changed = _stand ^ lastswitch[sws]; //is de stand veranderd

	for (byte i = 0; i < 6; i++) {

		byte _switch = i + (4 * sws);

		if (_changed & (1 << i)) {

			//Serial.println(lastswitch[sws]);


			if (_stand & (1 << i)) { //alleen indrukken van de knoppen iets mee doen
				SWon(_switch);
			}
			else
			{ //loslaten van een knop
				SWoff(_switch);
			}
		}
		//scroll functie alleen eventueel op knop 3 en 4 
		if (scroll == true && (_stand & (1 << i))) {
			switch (_switch) {
			case 6: //knop 3
				if (scrollcount[0] < 20) {
					scrollcount[0]++;
				}
				else {
					SWon(6);
				}
				break;
			case 7: //knop 4;
				if (scrollcount[1] < 20) {
					scrollcount[1]++;
				}
				else {
					SWon(7);
				}
				break;
			}
		}
	}

	lastswitch[sws] = _stand; //onthouden laatste stand

	//instellen volgende lees cyclus
	sws++; if (sws > 1)sws = 0;

	if (sws == 1) { //read switch
		DDRD |= (1 << 7); //pin 7 als output
		PORTD |= (1 << 7);

		DDRD &= ~(1 << 6);  //pin 6 input H-Z
		PORTD &= ~(1 << 6);
	}
	else { //read home 
		DDRD |= (1 << 6); //pin6 als output hoog
		PORTD |= (1 << 6);

		DDRD &= ~(1 << 7); //pin6 als input H-z
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
		Sw_knop(0);
		break;
	case 5: //sw2
		Sw_knop(1);
		break;
	case 6: //sw 3
		Sw_knop(2);
		break;
	case 7:  //sw 4
		Sw_knop(3);
		break;
	case 8: //sw program
		Sw_program();
		break;

	}

}
void SWoff(byte _sw) {
	if (_sw == 6) scrollcount[0] = 0; if (_sw == 7)scrollcount[1] = 0;
}

void StepperSpeed(byte _stepper) {
	if (speedfactor[_stepper] > 200)speedfactor[_stepper] = 12;
	speed[_stepper] = 800 + (speedfactor[_stepper] * 100);   //tijdelijk deze waardes nog verder bepalen. nu default dus 600ms
}
void LedEffect() {
	switch (effects) {
	case 1: //Wacht op DCC adres om in te stellen

		effectcount[0]++;
		if (effectcount[0] > 3)effectcount[0] = 0; //leds 1 voor 1 aflopen
		ledkleur[effectcount[0]] = random(1, 7);
		break;

	case 2:
		effectcount[0]++;
		if (effectcount[0] > 100)ProgramfaseSet(20);
		break;


	case 3: //stepperinstelling start, gekozen stepper animatie

		effectcount[0]++;
		if (effectcount[0] > 10) {
			effectcount[0] = 0;
			effectcount[1] ^= (1 << 0); //toggle bit 1
			if (effectcount[1] & (1 << 0)) {
				StandLed(stepprogram);
			}
			else {
				LedsAll(0);
				effectcount[2]++;
				if (effectcount[2] > 4)effects = 4;

			}
		}
		break;

	case 4: //vervolg keuze stepper in stepper instellingen
		EffectCountClear();
		effects = 5;
		break;

	case 5: //instellen eerste programmeer fase instellingen stepper posities
		//led1 welke instelling lichtblauw voor posities
		programfase = 1;
		ledkleur[0] = 4;
		ProgramfaseSet(40);
		effects = 0; //effecten stoppen	
		break;

	case 6: //stepper instelling speed knipper effect op speed snelheid
		effectcount[0]++;
		if (effectcount[0] > 2 * speedfactor[stepprogram]) {
			effectcount[0] = 0;
			stepreg ^= (1 << 1); //toggle bit 1
			if (stepreg & (1 << 1)) {
				ledkleur[2] = 5; ledkleur[3] = 0;
			}
			else {
				ledkleur[2] = 0; ledkleur[3] = 5;
			}
		}
		break;

	case 50: //Factory reset knipper effect op led 4 
		effectcount[0] = 0;
		effects = 51;
		break;
	case 51:
		byte _led = 3;
		effectcount[0]++;
		if (effectcount[0] > 10) {
			effectcount[0] = 0;
			stepreg ^= (1 << 2);

			if (stepreg & (1 << 3)) { //extra confirmation if true door naar factory reset, false wissel led
				_led = 2;
			}

			if (stepreg & (1 << 2)) {
				ledkleur[_led] = 1;
			}
			else {
				ledkleur[_led] = 4;
			}
		}
		break;

	case 100:
		effectcount[3]++;
		if (effectcount[3] > 40) {
			effectcount == 0;
			effects = 0;
			CoilsUit(stepprogram);
		}
		break;
	}
}
void Sw_program() {
	effects = 0;
	programfase = 0;
	programtype++;
	if (programtype > 2)programtype = 0;
	switch (programtype) {

	case 0: //in bedrijf 
		Eeprom_write();
		ledkleur[4] = 0;

		for (byte i = 0; i < 4; i++) { //kleuren leds herstellen
			StandLed(i);
		}

		break;
	case 1: //instellingen per stepper		
		//instellen programfase 0
		ledkleur[4] = 4;
		LedsAll(7);
		break;


	case 2: //algemene instellingen
		programfase = 0; //DCC adres instellen 
		ProgramfaseSet(20);
		break;
	}
}
void Sw_knop(byte _knop) {
	switch (programtype) {

	case 0: //in bedrijf
		StepStart(_knop, 0);
		break;

	case 1: //instellingen per stepper
		Prg_stepper(_knop);
		break;

	case 2: //algemene instellingen
		Prg_Algemeen(_knop);
		break;
	}
}
void StandLed(byte _stepper) {
	switch (stepstand[_stepper]) {
	case 0:
		ledkleur[_stepper] = 3; //blauw
		break;
	case 1:
		ledkleur[_stepper] = 2; //groen
		if (Invert[_stepper] & (1 << 0))ledkleur[_stepper] = 1;  //rood
		break;
	case 2:
		ledkleur[_stepper] = 1;
		if (Invert[_stepper] & (1 << 0))ledkleur[_stepper] = 2;
		break;
	}
}

void Prg_stepper(byte _knop) {
	switch (programfase) {
	case 0: //**********************************kies in te stellen stepper			
		stepprogram = _knop;
		LedsAll(0); //alle leds uit.
		EffectCountClear();
		effects = 3;
		break;

	case 1: //************************************Instellen posities
		switch (_knop) {
		case 0: //knop1: volgende programmeerfase
			programfase = 2;
			//instellingen voor programfase 2
			ledkleur[0] = 2; //groen
			effects = 6; //knipper effect op led 3 en 4 
			EffectCountClear(); //reset tellers
			break;
		case 1: //knop2: stand aanpassen
			ProgramfaseSet(25);
			break;
		case 2: //knop3: positie stepper verlagen richting home
			if (!stepreg & (1 << 0)) { //alleen als busy bit0 is false			
				stepdir[stepprogram] = Invert[stepprogram] & (1 << 2); //stephome[stepprogram]; //draaien naar de home switch
				Steps(stepprogram);
				steptarget[stepprogram][stepstand[stepprogram] - 1]--; //stepstand=0,1 of 2 0=hier niet meer mogelijk
				effects = 100; //schakeld spoelen weer uit.
				effectcount[3] = 0;
			}
			break;
		case 3://knop4: positie stepper verhogen van home vandaan
			if (!stepreg & (1 << 0)) {

				//if (Invert[stepprogram] & (1 << 2)) {
				//	stepdir[stepprogram] = false;
				//}
				//else {
				//	stepdir[stepprogram] = true;
				//}

				stepdir[stepprogram] = ~Invert[stepprogram] & (1 << 2); //stephome[stepprogram]; //draaien van de homeswitch af

				Steps(stepprogram);
				steptarget[stepprogram][stepstand[stepprogram] - 1]++;
				effects = 100;
				effectcount[3] = 0;
			}
			break;
		}
		break;
	case 2: //*****************************Instellen speed stepper
		switch (_knop) {
		case 0:
			programfase = 3;
			//instellen programfase 3

			effects = 0;
			EffectCountClear();
			ledkleur[0] = 6; //paars			
			ProgramfaseSet(26); //leds instellen			
			break;

		case 1:
			ProgramfaseSet(25); //zet stand en positie stepper om
			break;
		case 2:
			if (speedfactor[stepprogram] < 50) speedfactor[stepprogram]++;
			StepperSpeed(stepprogram);
			break;
		case 3:
			if (speedfactor[stepprogram] > 1) speedfactor[stepprogram]--;
			StepperSpeed(stepprogram);
			break;
		}
		break;

	case 3: //***************************instellen richtingen stepper
		switch (_knop) {
		case 0:
			programfase = 4;
			LedsAll(0); //alle leds uit.
			ledkleur[0] = 5;
			ToonAuto();


			//EffectCountClear();
			//effects = 3;
			break;
		case 1:
			Invert[stepprogram] ^= (1 << 2);
			break;
		case 2:
			Invert[stepprogram] ^= (1 << 1);
			break;
		case 3:
			Invert[stepprogram] ^= (1 << 0);
			break;
		}
		if (_knop > 0) ProgramfaseSet(26);
		break;

	case 4: //************************************instellingen stepper auto

		switch (_knop) {
		case 0:
			LedsAll(0); //alle leds uit.
			EffectCountClear();
			effects = 3;
			break;
		case 1:
			stepauto[stepprogram]++;
			if (stepauto[stepprogram] > 2)stepauto[stepprogram] = 0;
			ToonAuto();
			break;
		case 2:
			autotimefactor[stepprogram]++;
			if (autotimefactor[stepprogram] > 6)autotimefactor[stepprogram] = 0;
			ToonAuto();
			break;
		case 3:
			//single of in sequence auto
			Invert[stepprogram] ^= (1 << 3);
			ToonAuto();
			break;
		}
		break;
	}
}

void ToonAuto() {
	//led1
	switch (stepauto[stepprogram]) {
	case 0: //auto off
		ledkleur[1] = 3;
		break;
	case 1: //auto heen en weer
		ledkleur[1] = 4;
		break;
	case 2: //alleen rood terug naar groen
		ledkleur[1] = 1;
		break;
	}

	if (stepauto[stepprogram] > 0) {
		//ledkleur[2] = autotimefactor[stepprogram];

		switch (autotimefactor[stepprogram]) {
		case 0:
			ledkleur[2] = 7;
			break;
		case 1:
			ledkleur[2] = 5;
			break;
		case 2:
			ledkleur[2] = 3;
			break;
		case 3:
			ledkleur[2] = 6;
			break;
		case 4:
			ledkleur[2] = 1;
			break;
		case 5:
			ledkleur[2] = 4;
			break;
		case 6:
			ledkleur[2] = 2;
			break;			
		}

		if (Invert[stepprogram] & 1 << 3) {
			ledkleur[3] = 6;
		}
		else {
			ledkleur[3] = 4;
		}
	}
	else {
		ledkleur[2] = 0; ledkleur[3] = 0;
	}
}
void Prg_Algemeen(byte _knop) {

	switch (programfase) {
		//***************************programtype algemeen programfase DCC adres instellen
	case 0: //DCC adres instellen
		switch (_knop) {
		case 0: //knop 1: volgende programmeer fase instellen
			programfase = 1;
			ProgramfaseSet(21); //2x10+programfase
			break;

		case 1:  // knop 2: start ingestelde programmeerfase
			effects = 1;			//bit 0 true
			break;

		case 2:
			break;

		case 3:
			break;
		}
		break;
		//***************************programtype algemeen programfase 1
	case 1:  //programfase 1: instellen DCCadres
		switch (_knop) {
		case 0: //knop: 1
			programfase = 2;
			ProgramfaseSet(22);
			break;
		case 1: //knop 2
			break;
		case 2: //knop 3
			break;
		case 3: //knop 4
			break;
		}
		break;
		//***************************programtype algemeen programfase 2 volgende instelling
	case 2: //programfase 2: Factory reset
		switch (_knop) {
		case 0: //knop:1
			programfase = 0;
			effects = 0;
			stepreg &= ~(1 << 3); //reset bit wat vraagt om bevestiging
			ProgramfaseSet(20);
			break;
		case 2: //knop1
			if (stepreg & (1 << 3)) {

				Factory();
			}
			break;
		case 3: //knop 4: vraag om bevestiging  voor factory reset.
			ledkleur[3] = 0;
			stepreg |= (1 << 3);
			break;
		}
		break;
	}

}
void ProgramfaseSet(byte _fase) {
	byte _stand = 0;
	//set diverse led kleuren combies en instellingen op een bepaald moment in het programmeer proces.
	switch (_fase) {

	case 20: //program algemeen DCC instellen start
		ledkleur[4] = 6;
		ledkleur[0] = 6; ledkleur[1] = 1; ledkleur[2] = 0; ledkleur[3] = 0;
		break;

	case 21:
		ledkleur[0] = 4; ledkleur[1] = 0; ledkleur[2] = 0; ledkleur[3] = 0;
		break;

	case 22: //factory reset instellen
		ledkleur[0] = 1; ledkleur[1] = 0; ledkleur[2] = 0; ledkleur[3] = 0;
		effects = 50; //knippereffect op led 4
		break;

	case 25: //in stepper instellingen stand omzetten en stepper verplaatsen
		ledkleur[2] = 0; ledkleur[3] = 0;
		stepreg |= (1 << 0); //busy, motor draait
		scroll = false;
		StepStart(stepprogram, 0);
		break;

	case 26: //richtingen tonen	
		//bit0=step, dus kleur van de led, bit1=invert het DCC command bit2=homerichting
		for (byte i = 0; i < 3; i++) {
			if (Invert[stepprogram] & (1 << i)) {
				ledkleur[3 - i] = 1; //rood
			}
			else {
				ledkleur[3 - i] = 2; //groen
			}
		}
		break;

	case 40: //instellen posities stepper**********************************
		_stand = stepstand[stepprogram];

		if (_stand > 0) {
			ledkleur[2] = 5; ledkleur[3] = 5;
			stepreg &= ~(1 << 0); //motor niet busy
			scroll = true; //scrollen knop 3 en 4 toestaan
		}
		else {
			stepreg |= (1 << 0); //geen stand dus posities niet in te stellen
		}

		switch (_stand) {
		case 0:
			ledkleur[1] = 3;
			break;
		case 1:
			ledkleur[1] = 2;
			if (Invert[stepprogram & (1 << 0)])ledkleur[1] = 1;
			break;
		case 2:
			ledkleur[1] = 1;
			if (Invert[stepprogram] & (1 << 0))ledkleur[1] = 2;
			break;
		}
		break;
	}
}

void StepStart(byte _stepper, byte _stand) {
	//start beweging van de stepper	
	if (programtype == 0) ledkleur[_stepper] = 3; //alleen als in bedrijf
	//bepaal eindbestemming, hier nog iets doen met de switch mode, moment of aan/uit	

	if (_stand == 0) {
		stepstand[_stepper]++;
		if (stepstand[_stepper] > 2)stepstand[_stepper] = 1; //0>1 1>2 2>1	
	}
	else {
		stepstand[_stepper] = _stand;
	}

	//met lastswich bepalen of de home switch momenteel is ingedrukt, zoja dan draaien van de home switch af, anders de home switch gaan zoeken.
	stepdoel[_stepper] = stepstand[_stepper] - 1;
	stepfase[_stepper] = 1;
	steppos[_stepper] = 0; //reset positie

	if (lastswitch[0] & (1 << _stepper)) { //homeswitch is actief
		StepStop(_stepper);
	}
	else { //homeswitch niet ingedrukt.
		stepdir[_stepper] = Invert[_stepper] & (1 << 2); //stephome[_stepper]; //draairichting naar homeswitch		
	}

}
void StepStop(byte _stepper) {

	if (stepfase[_stepper] == 1) { //opweg naar homeswitch

		stepdir[_stepper] = !stepdir[_stepper]; //! Invert[_stepper] & (1 << 2);//! stephome[_stepper];

		steppos[_stepper] = 0; //reset positie stepper
		stepfase[_stepper] = 2;
	}
}
void Stepper_exe() {
	for (byte i = 0; i < 4; i++) { //om de beurt de 4 steppers

		if (micros() - speedtime[i] > speed[i]) {     // speed[i]) { //tijdelijk ff 
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
					switch (programtype) {
					case 0: //in bedrijf
						StandLed(i);
						break;
					case 1: //*****************instellingen per stepper
						switch (programfase) {
						case 1: //instellen posities van een stepper
							ProgramfaseSet(40); //zet de juiste led op de juiste kleur							
							break;
						case 2: //instellen snelheid, speed van de stepper
							//effects = 6;
							ProgramfaseSet(40);
							break;
						}
						break;


					case 2: //******************algemene instellingen
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

	steppos[_stepper]++;
	if (stepfase[_stepper] == 1) { //richting home, auto uitschakelen als home switch wordt gemist
		if (steppos[_stepper] > steptarget[_stepper][0] + steptarget[_stepper][1] + 200) {
			//LedsAll(1);
			stepfase[_stepper] = 0; CoilsUit(_stepper);
		}
	}

	//if (stepfase[_stepper] == 2)steppos[_stepper]++;
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
