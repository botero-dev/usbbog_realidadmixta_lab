extends Node

@onready var serial_gamepad: Node = $"../SerialGamepad"


var input_left: float = 0
var input_right: float = 0
var input_up: float = 0
var input_down: float = 0

@onready var ship: Ship = $"../Ship"

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
		if ship:
			ship.fire()
		
	if evt.is_action_pressed("boost"):
		if ship:
			ship.boost()
		
	if evt.is_action_pressed("spin"):
		if ship:
			ship.spin()
	
	
func _physics_process(delta: float) -> void:
	var move_horizontal := input_right - input_left
	var move_vertical := input_down - input_up
	
	if ship:
		ship.steer(Vector2(move_horizontal, move_vertical))
