// Author: Andrés Botero

// Select output mode by uncommenting one of the following


// FUNCIONANDO OK

// Text format compatible with teleplot. each variable is written as `>VAR:VALUE\n` 
// #define SEND_STATE_DEBUG

// Communicates inputs as USB HID device.
#define SEND_STATE_USB

// Writes data into JSON objects. Delimits each message with `\r\n`
//#define SEND_STATE_JSON


// NO IMPLEMENTADOS TODAVIA

// Basic text output in format A0 B1 C0 D1 X234 Y221
// #define SEND_STATE_TEXT

// Compact serialization format. consists on a header and raw bytes of data.
// #define SEND_STATE_BINARY


#include <Arduino.h>

#ifdef SEND_STATE_USB
#include <HID.h>
#endif

/* ----------------------------- Configuracion ----------------------------- */

// Pines de las entradas digitales, en el orden en que se reportan.



// Nombres de los botones (pin fisico en el Pro Micro).
#define BTN_FACE_NORTH 6
#define BTN_FACE_EAST  7
#define BTN_FACE_SOUTH 9
#define BTN_STICK_LEFT 5

// Orden de lectura de los botones (coincide con los nombres de arriba).
const uint8_t DIGITAL_PINS[] = {
	BTN_FACE_NORTH,
	BTN_FACE_EAST,
	BTN_FACE_SOUTH,
	BTN_STICK_LEFT,
};



const uint8_t NUM_DIGITAL = sizeof(DIGITAL_PINS) / sizeof(DIGITAL_PINS[0]);

// Pines de las entradas analogicas, en el orden en que se reportan.
const uint8_t ANALOG_PINS[] = {A2, A3};
const uint8_t NUM_ANALOG = sizeof(ANALOG_PINS) / sizeof(ANALOG_PINS[0]);

// Botones cableados de pin a GND => pull-up interno: 1 en reposo, 0 al pulsar.
// Si en cambio se cablean a VCC (activo alto, con resistencia a GND por medio),
// poner false para no pelear con la resistencia externa.
const bool USE_INTERNAL_PULLUP = true;

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


#ifdef SEND_STATE_USB
void usb_init();
void send_state_usb(State* state);
#endif


/* -------------------------------- Helpers -------------------------------- */

// Lee y descarta lo que el host envie por el puerto serie (CDC), para que el
// buffer de entrada no se llene. Acotado para no bloquear el loop si el host
// transmite sin parar.
void discard_serial_input() {
	for (uint16_t i = 0; i < 128 && Serial.available() > 0; i++) {
		Serial.read();
	}
}


/* --------------------------------- Setup --------------------------------- */

void setup() {
	Serial.begin(SERIAL_BAUD);

	for (uint8_t i = 0; i < NUM_DIGITAL; i++) {
		pinMode(DIGITAL_PINS[i], INPUT_PULLUP);
	}

	for (uint8_t i = 0; i < NUM_ANALOG; i++) {
		pinMode(ANALOG_PINS[i], INPUT);
	}


#ifdef SEND_STATE_USB
	usb_init();
#endif
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

// Serializa el estado como un objeto JSON por linea, delimitado por '\r\n'.
// Ejemplo: {"T":123456,"A":0,"B":1,"C":0,"D":1,"X":520,"Y":509}
// Todos los valores son enteros, por lo que no requieren escapado.
void send_state_json(State* state) {

	Serial.print(F("{\"T\":"));
	Serial.print(state->timestamp);

	for (uint8_t i = 0; i < NUM_DIGITAL; i++) {
		char btn_name = 'A' + i;
		Serial.print(F(",\""));
		Serial.print(btn_name);
		Serial.print(F("\":"));
		Serial.print(state->btns[i] ? 1 : 0);
	}

	for (uint8_t i = 0; i < NUM_ANALOG; i++) {
		char axis_name = 'X' + i;
		Serial.print(F(",\""));
		Serial.print(axis_name);
		Serial.print(F("\":"));
		Serial.print(state->axes[i]);
	}

	// Cierra el objeto y agrega CRLF como delimitador entre mensajes.
	Serial.println('}');
}

void loop() {

	// El firmware no usa datos entrantes: los descartamos.
	discard_serial_input();

	State* state = &states[frame % NUM_STATES]; 
	state->timestamp = micros();
	// Entradas digitales -> "d5", "d6", "d7", "d9" ...
	for (uint8_t i = 0; i < NUM_DIGITAL; i++) {
		bool value = !digitalRead(DIGITAL_PINS[i]); // flipped condition because pin is default_pullup
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

	// for testing, maybe remove in the future
	is_dirty = true;


	if (is_dirty) {
#ifdef SEND_STATE_BINARY
		send_state_binary(state);
#elif defined(SEND_STATE_TEXT)
		send_state_text(state);
#elif defined(SEND_STATE_DEBUG)
		send_state_debug(state);
#elif defined(SEND_STATE_JSON)
		send_state_json(state);
#elif defined(SEND_STATE_USB)
		send_state_usb(state);
#else
		error - no SEND_STATE_XXX constant set.
#endif
	}


	frame += 1;

}




/* ------------------------------ Modo USB HID ----------------------------- */
/*
 *  Con SEND_STATE_USB el Pro Micro se presenta al host como un gamepad HID
 *  (ademas del puerto serie CDC, ya que la placa soporta dispositivo compuesto).
 *
 *  El descriptor de reporte define:
 *    - 16 botones (2 bytes, 1 = pulsado). Solo se usan los usages 1, 2, 4 y 14,
 *      que el kernel expone como BTN_SOUTH, BTN_EAST, BTN_NORTH y BTN_THUMBL.
 *    - eje X y eje Y       (16 bits, valor crudo del ADC: 0..1023)
 *
 *  Se usa el ID de reporte 0x03 para no chocar con 0x01/0x02, reservados por el
 *  core de Arduino para los HID de teclado y raton.
 */

#ifdef SEND_STATE_USB

#define GAMEPAD_REPORT_ID 0x03

// Bit del reporte HID para cada boton estandar (bit = numero de boton HID - 1).
// El kernel los traduce a: BTN_SOUTH, BTN_EAST, BTN_NORTH y BTN_THUMBL.
#define HID_BTN_SOUTH  0    // Button 1
#define HID_BTN_EAST   1    // Button 2
#define HID_BTN_NORTH  3    // Button 4
#define HID_BTN_THUMBL 13   // Button 14

// Correspondencia pin -> bit HID, en el orden de DIGITAL_PINS[].
static const uint8_t USB_BUTTON_BITS[NUM_DIGITAL] = {
	HID_BTN_NORTH,   // BTN_FACE_NORTH
	HID_BTN_EAST,    // BTN_FACE_EAST
	HID_BTN_SOUTH,   // BTN_FACE_SOUTH
	HID_BTN_THUMBL,  // BTN_STICK_LEFT
};

static const uint8_t GAMEPAD_REPORT_DESCRIPTOR[] PROGMEM = {
	0x05, 0x01,               // USAGE_PAGE (Generic Desktop)
	0x09, 0x05,               // USAGE (Game Pad)
	0xA1, 0x01,               // COLLECTION (Application)
	0x85, GAMEPAD_REPORT_ID,  //   REPORT_ID (3)
	// --- 16 botones (2 bytes) ---
	0x05, 0x09,               //   USAGE_PAGE (Button)
	0x19, 0x01,               //   USAGE_MINIMUM (Button 1)
	0x29, 0x10,               //   USAGE_MAXIMUM (Button 16)
	0x15, 0x00,               //   LOGICAL_MINIMUM (0)
	0x25, 0x01,               //   LOGICAL_MAXIMUM (1)
	0x75, 0x01,               //   REPORT_SIZE (1)
	0x95, 0x10,               //   REPORT_COUNT (16)
	0x81, 0x02,               //   INPUT (Data,Var,Abs)
	// --- Ejes X e Y (16 bits, valor crudo 0..1023) ---
	0x05, 0x01,               //   USAGE_PAGE (Generic Desktop)
	0x09, 0x30,               //   USAGE (X)
	0x09, 0x31,               //   USAGE (Y)
	0x15, 0x00,               //   LOGICAL_MINIMUM (0)
	0x26, 0xFF, 0x03,         //   LOGICAL_MAXIMUM (1023)
	0x75, 0x10,               //   REPORT_SIZE (16)
	0x95, 0x02,               //   REPORT_COUNT (2)
	0x81, 0x02,               //   INPUT (Data,Var,Abs)
	0xC0                      // END_COLLECTION
};

// Disposicion en memoria del reporte que viaja por el endpoint HID.
struct __attribute__((packed)) GamepadReport {
	uint16_t buttons;  // bits 0..15 (ver USB_BUTTON_BITS)
	uint16_t x;        // eje X (0..1023)
	uint16_t y;        // eje Y (0..1023)
};

// Registra el descriptor en el stack USB durante la inicializacion estatica.
// Es importante que ocurra antes de que el core levante el USB en setup(), para
// que la interfaz HID forme parte de la enumeracion inicial del host.
class UsbGamepad {
public:
	UsbGamepad() {
		static HIDSubDescriptor node(GAMEPAD_REPORT_DESCRIPTOR, sizeof(GAMEPAD_REPORT_DESCRIPTOR));
		HID().AppendDescriptor(&node);
	}
};

static UsbGamepad usbGamepad;


void usb_init() {
	// Reporte neutro inicial (botones sueltos, ejes al centro ~512) para que el
	// host arranque con un estado conocido antes del primer cambio real.
	const GamepadReport neutral = {0, 512, 512};
	HID().SendReport(GAMEPAD_REPORT_ID, &neutral, sizeof(neutral));

}

// La lectura ADC ya viene en 0..1023; se recorta por si cambia la resolucion.
static uint16_t clamp_axis(int16_t adc) {
	if (adc < 0) return 0;
	if (adc > 1023) return 1023;
	return (uint16_t)adc;
}

// Envia el estado actual como un reporte HID de gamepad.
void send_state_usb(State* state) {
	GamepadReport report;
	report.buttons = 0;

	// Pull-up: 0 == pulsado -> ponemos su bit HID a 1.
	for (uint8_t i = 0; i < NUM_DIGITAL; i++) {
		if (!state->btns[i]) {
			report.buttons |= (uint16_t)(1u << USB_BUTTON_BITS[i]);
		}
	}

	report.x = clamp_axis(NUM_ANALOG > 0 ? state->axes[0] : 512);
	report.y = clamp_axis(NUM_ANALOG > 1 ? state->axes[1] : 512);

	// stick is flipped, so we flip values
	report.x = 1023 - report.x;
	report.y = 1023 - report.y;

	HID().SendReport(GAMEPAD_REPORT_ID, &report, sizeof(report));
}

#endif  // SEND_STATE_USB

