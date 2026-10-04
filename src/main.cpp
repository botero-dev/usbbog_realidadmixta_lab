/*
 * ---------------------------------------------------------------------------
 *  Lectura de 4 entradas digitales + 2 entradas analogicas
 * ---------------------------------------------------------------------------
 *  Placa   : Arduino Pro Micro (ATmega32U4, 5 V / 16 MHz)
 *  Entradas: D5, D6, D7, D9  -> digitales (pull-up: 1 en reposo, 0 pulsado)
 *            A2, A3          -> analogicas (10 bits: 0..1023)
 *  Salida  : una linea JSON por muestra por el puerto serie (USB CDC)
 *
 *  Ejemplo de salida:
 *    {"ms":1234,"d5":1,"d6":0,"d7":1,"d9":1,"a2":512,"a3":287}
 * ---------------------------------------------------------------------------
 */

// Define one of these to configure format to use when sending state

// #define SEND_STATE_TEXT
#define SEND_STATE_DEBUG
// #define SEND_STATE_BINARY
// #define SEND_STATE_JSON
// #define SEND_STATE_USB


#include <Arduino.h>

/* ----------------------------- Configuracion ----------------------------- */

// Pines de las entradas digitales, en el orden en que se reportan.
const uint8_t DIGITAL_PINS[] = {6, 7, 9, 5};
const uint8_t NUM_DIGITAL = sizeof(DIGITAL_PINS) / sizeof(DIGITAL_PINS[0]);

// Pines de las entradas analogicas, en el orden en que se reportan.
const uint8_t ANALOG_PINS[] = {A2, A3};
const uint8_t NUM_ANALOG = sizeof(ANALOG_PINS) / sizeof(ANALOG_PINS[0]);

// Botones cableados de pin a GND => pull-up interno: 1 en reposo, 0 al pulsar.
// Si en cambio se cablean a VCC (activo alto, con resistencia a GND por medio),
// poner false para no pelear con la resistencia externa.
const bool USE_INTERNAL_PULLUP = true;

// Tiempo entre muestras (ms).
const unsigned long SAMPLE_PERIOD_MS = 10;

// Velocidad del puerto serie.
const unsigned long SERIAL_BAUD = 115200;


/* ------------------------------- Variables ------------------------------- */

unsigned long lastSampleMs = 0;

struct State {
	unsigned long timestamp;
	bool btns[NUM_DIGITAL];
	int16_t axes[NUM_ANALOG];
};

#define NUM_STATES 2
State states[NUM_STATES];

unsigned long frame = 0;

/* -------------------------------- Helpers -------------------------------- */


/* --------------------------------- Setup --------------------------------- */

void setup() {
	Serial.begin(SERIAL_BAUD);

	for (uint8_t i = 0; i < NUM_DIGITAL; i++) {
		pinMode(DIGITAL_PINS[i], INPUT_PULLUP);
	}

	for (uint8_t i = 0; i < NUM_ANALOG; i++) {
		pinMode(ANALOG_PINS[i], INPUT);
	}
}


void send_state_text(State* state) {
	
	// packet format example:
	// T12345678 A0 B1 C0 X123 Y456 <new line>
	// 
	// where
	// T is header for timestamp (microseconds since startup)
	// A, B, C are specific buttons, and 0/1 means pressed/released
	// X123 means value 123 for X axis
	// analog values are in range 0-1023, with no calibration

	Serial.print('T');
	Serial.print(state->timestamp);
	Serial.print(' ');

	unsigned char A = 0;

	for (uint8_t i = 0; i < NUM_DIGITAL; i++) {
		char btn_name = 'A' + i;
		Serial.print(btn_name);
		Serial.print(state->btns[i]);
		Serial.print(' ');
	}


	
	for (uint8_t i = 0; i < NUM_ANALOG; i++) {
		char axis_name = 'X' + i;
		short axis_value = state->axes[i];
		Serial.print(axis_name);
		Serial.print(axis_value);
		Serial.print(' ');
	}

	Serial.print('\n');
}

// format that teleplot uses to draw plots
void send_state_debug(State* state) {

	Serial.print(">T:");
	Serial.print(state->timestamp);
	Serial.println();
	
	unsigned char A = 0;

	for (uint8_t i = 0; i < NUM_DIGITAL; i++) {
		char btn_name = 'A' + i;
		Serial.print('>');
		Serial.print(btn_name);
		Serial.print(':');
		Serial.print(state->btns[i]);
		Serial.println();
	}


	
	for (uint8_t i = 0; i < NUM_ANALOG; i++) {
		char axis_name = 'X' + i;
		short axis_value = state->axes[i];
		Serial.print('>');
		Serial.print(axis_name);
		Serial.print(':');
		Serial.print(axis_value);
		Serial.println();
	}

}

void send_state_binary(State* state) {
	
	// packet format:
	// 01234567890123456
	// DATA ABBCC
	// where
	// DATA -> Fixed string header
	// A -> 8 bit value but only 3 bits used for button values in least significant bits
	// BB -> signed 16 bit X axis value, only 10 significant bits.
	// CC -> signed 16 bit Y axis value, only 10 significant bits. 

	// we use "write" to send raw bytes instead of converting to string
	Serial.write("DATA ");

	unsigned char A = 0;

	for (uint8_t i = 0; i < NUM_DIGITAL; i++) {
		A = A | (state->btns[i] << i);
	}
	Serial.print(A);

	
	for (uint8_t i = 0; i < NUM_ANALOG; i++) {
		short BB = state->axes[i] - 511;
		Serial.write(BB);
	}
}

void loop() {

	State* state = &states[frame % NUM_STATES]; 
	state->timestamp = micros();
	// Entradas digitales -> "d5", "d6", "d7", "d9" ...
	for (uint8_t i = 0; i < NUM_DIGITAL; i++) {
		bool value = digitalRead(DIGITAL_PINS[i]);
		state->btns[i] = value;
	}

	for (uint8_t i = 0; i < NUM_ANALOG; i++) {
		int16_t value = analogRead(ANALOG_PINS[i]);
		state->axes[i] = value;
	}

	State* old_state = &states[(frame+NUM_STATES-1)%NUM_STATES];



	
	const int16_t THRESHOLD = 0;

	// noise reduction: snap values to previous value if delta is <= threshold
	for (uint8_t i = 0; i < NUM_ANALOG; i++) {
		int16_t delta = state->axes[i] - old_state->axes[i];
		if (delta < 0) {
			delta = -delta;
		}

		if (delta <= THRESHOLD) {
			state->axes[i] = old_state->axes[i];
		}
	}


	bool is_dirty = false;

	for (uint8_t i = 0; i < NUM_DIGITAL; i++) {
		if (state->btns[i] != old_state->btns[i]) {
			is_dirty = true;
			break;
		}
	}

	for (uint8_t i = 0; i < NUM_ANALOG; i++) {
		if (state->axes[i] != old_state->axes[i]) {
			is_dirty = true;
			break;
		}
	}

	if (is_dirty) {
#ifdef SEND_STATE_BINARY
		send_state_binary(state);
#elif defined(SEND_STATE_TEXT)
		send_state_text(state);
#elif defined(SEND_STATE_DEBUG)
		send_state_debug(state);
#elif defined(SEND_STATE_JSON)
		send_state_json();
#elif defined(SEND_STATE_USB)
		send_state_usb();
#else
		error - no SEND_STATE_XXX constant set.
#endif
	}


	frame += 1;

}

