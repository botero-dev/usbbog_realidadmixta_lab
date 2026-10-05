extends Node

# --- CONFIGURATION CONSTANTS ---
const PARITY_NONE = 0
const PARITY_ODD = 1
const PARITY_EVEN = 2

const STOP_BITS_ONE = 1
const STOP_BITS_TWO = 2

const FLOW_CONTROL_NONE = 0
const FLOW_CONTROL_SOFTWARE = 1
const FLOW_CONTROL_HARDWARE = 2

enum ReportFormat {
	TEXT,
	DEBUG,
	BINARY,
	JSON,
}

signal input_received(evt: InputEvent)

var serial: GdSerial
@export var port_to_connect = "/dev/ttyACM0"
@export var report_format: ReportFormat

# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	pass # Replace with function body.
	serial = GdSerial.new()
	
	# List all available COM ports with detailed info
	print("Available COM ports:")
	var ports: Dictionary = serial.list_ports()
	var found := false
	for i: int in ports:
		var port_info: Dictionary = ports[i]
		print("Port: ", port_info["port_name"], " (", port_info["device_name"], ") - Type: ", port_info["port_type"])
		if port_info.port_name == port_to_connect:
			found = true
			
	if not found:
		print("port:", port_to_connect, "not found in available ports")
		return

	print("connecting to:", port_to_connect)
	
	# Configure serial port settings
	serial.set_port("/dev/ttyACM0") # Change this to your actual port
	serial.set_baud_rate(115200)
	serial.set_data_bits(8)
	serial.set_parity(PARITY_NONE)
	serial.set_stop_bits(STOP_BITS_ONE)
	serial.set_flow_control(FLOW_CONTROL_NONE)
	serial.set_timeout(1000) # 1 second timeout
		
	# Open the port
	var success := serial.open()
	if success:
		print("Serial port opened successfully!")
		
		# read everything currently queued in buffer
		var available := serial.bytes_available()
		if available > 0:
			serial.read(available)
		
		

var buffer := ""

# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(delta: float) -> void:
	if serial.is_open():
		var available := serial.bytes_available()
		if available > 0:
			var response := serial.read_string(available)
			buffer += response
		
		var found_end_of_buffer := false
		var its = 0
		while not found_end_of_buffer:
			found_end_of_buffer = read_message()
			its += 1
		#print("its:", its)


var separador := "\r\n"
# retorna true cuando no encuentra un mensaje completo para decodificar
func read_message() -> bool:
	var final: int = buffer.find(separador)
	if final == -1:
		return true
	
	var msg = buffer.left(final)
	buffer = buffer.right(buffer.length() - final - separador.length()) # 1 extra del tamaño del separador
	
	match report_format:
		ReportFormat.TEXT:
			unpack_text(msg)
		ReportFormat.DEBUG:
			unpack_debug(msg)
		ReportFormat.BINARY:
			unpack_binary(msg)
		ReportFormat.JSON:
			unpack_json(msg)
	
	
	return false
	
const INPUT_BUTTONS := ["A", "B", "C", "D"]

const INPUT_AXES := ["X", "Y"]
const INPUT_FLIPPED := [true, true]
const INPUT_CENTER := [518, 506]


const INPUT_MAP := {
	"A": JOY_BUTTON_Y,
	"B": JOY_BUTTON_B,
	"C": JOY_BUTTON_A,
	"D": JOY_BUTTON_LEFT_STICK,
	"X": JOY_AXIS_LEFT_X,
	"Y": JOY_AXIS_LEFT_Y,
}



func unpack_text(msg: String):
	pass

func unpack_debug(msg: String):
	# message should have format `>var:value`.
	# if its something else just skip it
	var pos_gt := msg.find(">")
	var pos_colon := msg.find(":")
	if pos_gt == -1 || pos_colon == -1 || pos_colon	< pos_gt:
		# malformed msg
		return
	
	var key := msg.substr(pos_gt+1, pos_colon - pos_gt - 1)
	var id = INPUT_MAP.get(key)
	if id == null:
		return
		

	var value_str := msg.substr(pos_colon + 1)
	var value_int := int(value_str)
	
	if key in INPUT_BUTTONS:
		push_button(id, value_int)
	
	else:
		var axis_idx := INPUT_AXES.find(key)
		var center: int = INPUT_CENTER[axis_idx]
		var value_offset: int = value_int - center
		var value: float
		if value_offset > 0:
			value = float(value_offset) / (1023-center)
		else: # if value_offset <= 0:
			value = float(value_offset) / center
		if INPUT_FLIPPED[axis_idx]:
			value *= -1
		push_axis(id, value)


func unpack_binary(msg: String):
	pass


func unpack_json(msg: String):
	# message should be a JSON object with keys A/B/C/D (buttons) and X/Y (axes).
	# example: {"T":123,"A":0,"B":1,"C":0,"D":0,"X":520,"Y":509}
	var data = JSON.parse_string(msg)
	if typeof(data) != TYPE_DICTIONARY:
		# malformed msg
		return

	for key in data:
		var id = INPUT_MAP.get(key)
		if id == null:
			continue

		var value_int := int(data[key])
		if key in INPUT_BUTTONS:
			push_button(id, value_int)
		else:
			var axis_idx: int = INPUT_AXES.find(key)
			if axis_idx == -1:
				continue
			var center: int = INPUT_CENTER[axis_idx]
			var value_offset: int = value_int - center
			var value: float
			if value_offset > 0:
				value = float(value_offset) / (1023 - center)
			else:
				value = float(value_offset) / center
			if INPUT_FLIPPED[axis_idx]:
				value *= -1
			push_axis(id, value)



var last_state_btn := {}
var last_state_axis := {}

func push_button(id: int, state: bool):
	var last = last_state_btn.get(id)
	if last != null && last == state:
		return
	last_state_btn[id] = state
	var event = InputEventJoypadButton.new()
	event.button_index = id
	event.pressed = state
	input_received.emit(event)


func push_axis(id: int, state: float):
	var last = last_state_axis.get(id)
	if last != null && last == state:
		return
	last_state_axis[id] = state
	var event = InputEventJoypadMotion.new()
	event.axis = id
	event.axis_value = state
	input_received.emit(event)
