extends Node

@onready var serial_gamepad: Node = $"../SerialGamepad"


var input_left: float = 0
var input_right: float = 0
var input_up: float = 0
var input_down: float = 0


func _ready() -> void:
	serial_gamepad.input_received.connect(on_input_received)

func on_input_received(evt: InputEvent):
	if evt.is_action("move_right"):
		input_right = evt.get_action_strength("move_right")
	if evt.is_action("move_left"):
		input_left = evt.get_action_strength("move_left")
	if evt.is_action("move_up"):
		input_up = evt.get_action_strength("move_up")
	if evt.is_action("move_down"):
		input_down = evt.get_action_strength("move_down")
	
	if evt.is_action_pressed("fire"):
		
		print("fire!")
	if evt.is_action_pressed("boost"):
		print("boost!")
	if evt.is_action_pressed("spin"):
		print("spin!")
	
	
func _process(delta: float) -> void:
	var move_horizontal := input_right - input_left
	#print("Horizontal: ", move_horizontal)
	
	var move_vertical := input_up - input_down
	#print("Vertical: ", move_vertical)
