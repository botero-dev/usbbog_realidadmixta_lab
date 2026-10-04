extends CharacterBody2D

class_name Ship

@export var projectile_template: PackedScene
@export var speed: float = 80

var input: Vector2

func _ready() -> void:
	pass

func steer(direction: Vector2):
	if direction.length() > 0.1:
		rotation = direction.angle()
	input = direction

func fire():
	print("fire")
	var projectile: Projectile = projectile_template.instantiate()
	projectile.position = position
	projectile.rotation = rotation
	
	get_parent().add_child(projectile)
	

func boost():
	print("boost")

func spin():
	print("spin")

func _physics_process(delta: float) -> void:
	velocity = input * speed
	move_and_slide()
