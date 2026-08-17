#ifndef JMPXX_AUDIT_SCENE_AUDIT_H
#define JMPXX_AUDIT_SCENE_AUDIT_H

// A scene-tree audit whose failure path is jmpxx.
//
// The engine's own headers come first, as they do everywhere in this build, and jmpxx
// arrives afterwards as the released single header. Every fallible step below returns a
// jmpxx result and forwards a failure with one construct; the only place that inspects
// one is the boundary that hands a Dictionary back to script.
//
// The engine defines MIN, MAX, CLAMP, ABS, SIGN, ERR_FAIL_COND, and an Error enum whose
// enumerators are OK and FAILED, and it builds with exceptions and RTTI off. That is
// the environment the public surface has to survive here.

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/dictionary.h"
#include "scene/main/node.h"

#include <jmpxx.hpp>

class JmpxxSceneAudit : public RefCounted {
	GDCLASS(JmpxxSceneAudit, RefCounted);

public:
	// What an audit can find. The reflection layer derives the names a caller reads
	// from these enumerators, so the boundary carries no hand-written table.
	enum class Fault {
		none = 0,
		empty_tree = 1,
		too_deep = 2,
		duplicate_name = 3,
		empty_name = 4,
		reserved_prefix = 5,
	};

	Dictionary audit(Node *p_root);
	int max_depth() const { return max_depth_; }
	void set_max_depth(int p_depth);

protected:
	static void _bind_methods();

private:
	int max_depth_ = 8;

	// The audit proper. Each level returns a result and forwards a failure upward with
	// JMPXX_TRY; nothing between the failing node and the boundary inspects one.
	jmpxx::result<int, jmpxx::rich_error> visit(Node *p_node, int p_depth,
			HashSet<String> &r_seen, String &r_where);
	jmpxx::result<String, jmpxx::rich_error> check_name(Node *p_node,
			HashSet<String> &r_seen);
};

#endif // JMPXX_AUDIT_SCENE_AUDIT_H
