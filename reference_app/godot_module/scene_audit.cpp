#include "scene_audit.h"

#include "core/templates/hash_set.h"

// What this module holds the library to, checked by the engine's own compiler in the
// engine's own build rather than by the library's suite. A layout or version claim that
// is true in the library's build and false here is a claim about the wrong build.
static_assert(sizeof(jmpxx::result<int, jmpxx::error>) == 12,
		"the transport's documented layout does not hold in this build");
static_assert(sizeof(jmpxx::error) == 8,
		"the minimal error's documented layout does not hold in this build");
static_assert(std::is_trivially_copyable_v<jmpxx::result<int, jmpxx::error>>,
		"the transport is documented as trivially copyable for a trivial value");
static_assert(JMPXX_VERSION >= 104,
		"this module was written against jmpxx 0.1.4 or later");

// The engine's macros are live here. Naming them keeps the check honest: if a future
// engine version stops defining one, this module stops proving what it claims to. This
// engine takes likely and unlikely, two ordinary English words, and SWAP, and it moved
// MIN, MAX, and CLAMP from macros to constexpr templates, which is why they are not in
// the list.
#if !defined(SWAP) || !defined(likely) || !defined(unlikely) || !defined(ERR_FAIL_COND)
#error "the engine's own macros are not in scope; this module is not proving anything"
#endif

namespace {

jmpxx::rich_error fault(JmpxxSceneAudit::Fault f) {
	return jmpxx::rich_error(static_cast<int>(f), 0x6a78);
}

// The derived name as an engine String. enum_name returns a view into a longer
// compiler-generated literal, so it is not null-terminated and its data() pointer alone
// reads past the name; the length has to come with it. Handing data() to a C-style
// string API is the obvious thing to write and it produces the rest of the signature.
String fault_name(JmpxxSceneAudit::Fault f) {
	const std::string_view name = jmpxx::reflect::enum_name(f);
	return String::utf8(name.data(), static_cast<int>(name.size()));
}

} // namespace

void JmpxxSceneAudit::set_max_depth(int p_depth) {
	// The engine's own clamp, on jmpxx's side of the boundary.
	max_depth_ = CLAMP(p_depth, 1, 64);
}

jmpxx::result<String, jmpxx::rich_error> JmpxxSceneAudit::check_name(Node *p_node,
		HashSet<String> &r_seen) {
	const String name = p_node->get_name();
	if (name.is_empty()) {
		return jmpxx::fail(fault(Fault::empty_name));
	}
	// The engine normalizes a node name before this sees it, so a prefix the engine
	// itself rejects can never reach here. This rule names one the engine keeps.
	if (name.begins_with("tmp_")) {
		return jmpxx::fail(fault(Fault::reserved_prefix));
	}
	if (r_seen.has(name)) {
		return jmpxx::fail(fault(Fault::duplicate_name));
	}
	r_seen.insert(name);
	return name;
}

jmpxx::result<int, jmpxx::rich_error> JmpxxSceneAudit::visit(Node *p_node, int p_depth,
		HashSet<String> &r_seen, String &r_where) {
	if (p_depth > max_depth_) {
		r_where = p_node->get_name();
		return jmpxx::fail(fault(Fault::too_deep));
	}
	r_where = p_node->get_name();
	JMPXX_TRYV(check_name(p_node, r_seen));

	int counted = 1;
	for (int i = 0; i < p_node->get_child_count(); i++) {
		// The recursion is where the propagation earns its place: a fault five levels
		// down returns through every level with no check written at any of them.
		JMPXX_TRY(below, visit(p_node->get_child(i), p_depth + 1, r_seen, r_where));
		counted += below;
	}
	return counted;
}

Dictionary JmpxxSceneAudit::audit(Node *p_root) {
	Dictionary out;
	// One landing scope for the whole audit, which is also what bounds the diagnostic
	// context every failure below it records. In an engine build with the layer off it
	// is an empty object, which is why it is marked unused rather than removed.
	[[maybe_unused]] jmpxx::landing landing;

	if (p_root == nullptr) {
		out["ok"] = false;
		out["fault"] = fault_name(Fault::empty_tree);
		out["nodes"] = 0;
		return out;
	}

	HashSet<String> seen;
	String where;
	jmpxx::result<int, jmpxx::rich_error> counted = visit(p_root, 0, seen, where);

	out["ok"] = counted.has_value();
	out["nodes"] = counted.value_or(0);
	if (counted.has_value()) {
		out["fault"] = fault_name(Fault::none);
		return out;
	}

	const Fault f = static_cast<Fault>(counted.error().code);
	out["fault"] = fault_name(f);
	out["where"] = where;

	// Where the failure began and every level it passed through. The guard is not
	// optional: with the diagnostic layer off, jmpxx::diagnostic::context and inspect
	// do not exist, so code that reads a failure's context does not compile rather than
	// compiling to nothing. The engine defines NDEBUG even for an editor build, so this
	// module asks for the layer explicitly in its SCsub.
#if JMPXX_DIAGNOSTICS_ENABLED
	jmpxx::diagnostic::context ctx = jmpxx::diagnostic::inspect(counted.error());
	out["origin_available"] = ctx.available;
	if (ctx.available) {
		out["origin_line"] = static_cast<int>(ctx.origin.line());
		out["hops"] = ctx.hop_count;
	}
#else
	out["origin_available"] = false;
#endif
	return out;
}

void JmpxxSceneAudit::_bind_methods() {
	ClassDB::bind_method(D_METHOD("audit", "root"), &JmpxxSceneAudit::audit);
	ClassDB::bind_method(D_METHOD("set_max_depth", "depth"), &JmpxxSceneAudit::set_max_depth);
	ClassDB::bind_method(D_METHOD("max_depth"), &JmpxxSceneAudit::max_depth);
}
