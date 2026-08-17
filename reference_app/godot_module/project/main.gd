extends SceneTree

# Drive the module that uses jmpxx as its error spine, inside the engine, headless.
# Each case builds a scene tree and asks the audit what it finds. A fault several levels
# down returns through every level with no check written at any of them.

func _build(names: Array, depth: int) -> Node:
	var root := Node.new()
	root.name = "root"
	var at := root
	for i in range(depth):
		var child := Node.new()
		child.name = names[i] if i < names.size() else "level%d" % i
		at.add_child(child)
		at = child
	return root

func _audit(audit, label: String, names: Array, depth: int) -> void:
	var root := _build(names, depth)
	_report(label, audit.audit(root))
	root.free()

func _report(label: String, result: Dictionary) -> void:
	print("  %-14s ok=%s fault=%s nodes=%s where=%s origin=%s hops=%s" % [
		label, result.get("ok"), result.get("fault"), result.get("nodes"),
		result.get("where", "-"), result.get("origin_available", false),
		result.get("hops", "-")])

func _init() -> void:
	var audit := JmpxxSceneAudit.new()
	print("jmpxx scene audit inside the engine, headless")

	_audit(audit, "clean", ["a", "b", "c", "d"], 4)

	# A duplicate name six levels down: the fault the recursion has to carry back up.
	_audit(audit, "duplicate", ["a", "b", "c", "d", "e", "a"], 6)

	# A reserved prefix, found at the deepest level. The engine rewrites a name it
	# considers invalid before the audit sees it, so the rule names a prefix the engine
	# keeps rather than one it sanitizes away.
	_audit(audit, "reserved", ["a", "b", "c", "tmp_scratch"], 4)

	# Deeper than the audit allows, with the limit lowered first.
	audit.set_max_depth(3)
	_audit(audit, "too deep", ["a", "b", "c", "d", "e"], 5)
	audit.set_max_depth(8)

	# The property a failed run must leave behind: the next audit is unaffected.
	_audit(audit, "clean again", ["a", "b", "c", "d"], 4)

	_report("no tree", audit.audit(null))
	quit(0)
