#include "register_types.h"

#include "core/object/class_db.h"
#include "scene_audit.h"

void initialize_jmpxx_audit_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(JmpxxSceneAudit);
}

void uninitialize_jmpxx_audit_module(ModuleInitializationLevel p_level) {
}
