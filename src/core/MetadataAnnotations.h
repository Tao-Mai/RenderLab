#pragma once

// The metadata scanner expands these markers into Clang AST annotations.
#ifdef RENDERLAB_METADATA_SCAN
#define RL_RECORD [[clang::annotate("renderlab.record")]]
#define RL_TYPE(key) [[clang::annotate("renderlab.record", key)]]
#define RL_REGISTER_TYPE(base, key) [[clang::annotate("renderlab.register", #base, key)]]
#define RL_ALLOW_UNKNOWN_FIELDS [[clang::annotate("renderlab.allowUnknownFields")]]
#define RL_FIELD(key) [[clang::annotate("renderlab.field", key)]]
#define RL_PASS_SLIDER(key, minimum, maximum) [[clang::annotate("renderlab.field", key, minimum, maximum)]]
#define RL_PASS_UI(...) [[clang::annotate("renderlab.passUi" __VA_OPT__(,) __VA_ARGS__)]]
#define RL_ENUM [[clang::annotate("renderlab.enum")]]
#define RL_FLAGS [[clang::annotate("renderlab.flags")]]
#define RL_ENUM_VALUE(key) [[clang::annotate("renderlab.enumValue", key)]]
#define RL_SHADER_LAYOUT [[clang::annotate("renderlab.shaderLayout")]]
#else
#define RL_RECORD
#define RL_TYPE(key)
#define RL_REGISTER_TYPE(base, key)
#define RL_ALLOW_UNKNOWN_FIELDS
#define RL_FIELD(key)
#define RL_PASS_SLIDER(key, minimum, maximum)
#define RL_PASS_UI(...)
#define RL_ENUM
#define RL_FLAGS
#define RL_ENUM_VALUE(key)
#define RL_SHADER_LAYOUT
#endif
