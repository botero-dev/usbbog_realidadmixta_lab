extends CharacterBody2D

class_name Projectile

@export var speed: float = 0
@export var randomize_rotation: bool = false

func _ready() -> void:
	if randomize_rotation:
		rotate(randf() * TAU)

func _physics_process(delta: float) -> void:
	var motion = transform.x * speed * delta
	var collision = move_and_collide(motion)
	
	if collision:
		var target = collision.get_collider()
		if target is CharacterBody2D:
			target.queue_free()
		else:
			queue_free()
